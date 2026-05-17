#include "lsm_engine.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace std;

LSMEngine::LSMEngine(const string& data_directory,
                     size_t memtable_flush_threshold,
                     size_t bloom_filter_bits,
                     bool verbose)
    : data_directory_(data_directory),
      wal_path_(data_directory + "/active.wal"),
      manifest_path_(data_directory + "/manifest.txt"),
      memtable_flush_threshold_(memtable_flush_threshold),
      bloom_filter_bits_(bloom_filter_bits),
      next_file_number_(1),
      verbose_(verbose),
      wal_(make_unique<WAL>(wal_path_)) {
    filesystem::create_directories(data_directory_);
    Recover();
}

void LSMEngine::Set(const string& key, const string& value) {
    wal_->AppendSet(key, value);
    if (verbose_) {
        cout << "[WAL] Appended SET for key " << key << '\n';
    }

    memtable_.Set(key, value);
    if (verbose_) {
        cout << "[MEMTABLE] Stored key " << key << " in memory\n";
    }

    MaybeFlushMemTable();
}

void LSMEngine::Delete(const string& key) {
    wal_->AppendDelete(key);
    if (verbose_) {
        cout << "[WAL] Appended DELETE for key " << key << '\n';
    }

    memtable_.Delete(key);
    if (verbose_) {
        cout << "[MEMTABLE] Marked key " << key << " as deleted\n";
    }

    MaybeFlushMemTable();
}

optional<string> LSMEngine::Get(const string& key) const {
    if (memtable_.Contains(key)) {
        if (memtable_.IsDeleted(key)) {
            return nullopt;
        }
        return memtable_.Get(key);
    }

    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        if (!(*it)->MightContain(key)) {
            continue;
        }

        optional<SSTableRecord> record = (*it)->Get(key);
        if (!record.has_value()) {
            continue;
        }

        if (record->is_tombstone) {
            return nullopt;
        }

        return record->value;
    }

    return nullopt;
}

void LSMEngine::Recover() {
    if (verbose_) {
        cout << "[RECOVERY] Starting engine recovery from " << data_directory_ << '\n';
    }
    LoadManifest();
    next_file_number_ = DetectNextFileNumber();
    RecoverMemTableFromWAL();
    if (verbose_) {
        cout << "[RECOVERY] Loaded " << sstables_.size() << " SSTable(s) and "
             << memtable_.Size() << " in-memory key(s)\n";
    }
}

void LSMEngine::FlushMemTable() {
    if (memtable_.Empty()) {
        return;
    }

    string sstable_path = NextSSTablePath();
    auto sstable =
        SSTable::CreateFromMap(sstable_path, memtable_.Data(), bloom_filter_bits_);

    sstables_.push_back(sstable);
    if (verbose_) {
        cout << "[FLUSH] MemTable flushed to " << sstable_path << " with "
             << sstable->RecordCount() << " record(s)\n";
    }

    memtable_.Clear();
    wal_->Reset();
    if (verbose_) {
        cout << "[WAL] Truncated " << wal_->FilePath()
             << " after successful flush\n";
    }

    SaveManifest();
    MaybeCompact();
}

void LSMEngine::PrintState() const {
    cout << "\n[STATE] Current engine snapshot\n";
    cout << "  MemTable keys: " << memtable_.Size() << '\n';
    cout << "  SSTables: " << sstables_.size() << '\n';
    for (const auto& table : sstables_) {
        cout << "    - " << table->FilePath() << " (" << table->RecordCount()
             << " records)\n";
    }
}

void LSMEngine::LoadManifest() {
    sstables_.clear();

    ifstream manifest(manifest_path_);
    if (!manifest.is_open()) {
        return;
    }

    string line;
    while (getline(manifest, line)) {
        if (line.empty()) {
            continue;
        }

        if (!filesystem::exists(line)) {
            if (verbose_) {
                cout << "[MANIFEST] Skipping missing SSTable " << line << '\n';
            }
            continue;
        }

        sstables_.push_back(SSTable::LoadFromFile(line));
    }
}

void LSMEngine::SaveManifest() const {
    string temp_manifest_path = manifest_path_ + ".tmp";
    ofstream manifest(temp_manifest_path, ios::trunc);
    if (!manifest.is_open()) {
        throw runtime_error("Failed to write manifest: " + temp_manifest_path);
    }

    for (const auto& table : sstables_) {
        manifest << table->FilePath() << '\n';
    }

    manifest.close();
    filesystem::rename(temp_manifest_path, manifest_path_);
}

void LSMEngine::RecoverMemTableFromWAL() {
    memtable_.Clear();

    vector<WALRecord> records = wal_->Replay();
    for (const WALRecord& record : records) {
        if (record.type == WALOperationType::Set) {
            memtable_.Set(record.key, record.value);
        } else {
            memtable_.Delete(record.key);
        }
    }

    if (!records.empty()) {
        if (verbose_) {
            cout << "[RECOVERY] Replayed " << records.size() << " WAL record(s)\n";
        }
    }
}

void LSMEngine::MaybeFlushMemTable() {
    if (memtable_.Size() >= memtable_flush_threshold_) {
        FlushMemTable();
    }
}

void LSMEngine::MaybeCompact() {
    if (sstables_.size() > 3) {
        CompactAllSSTables();
    }
}

void LSMEngine::CompactAllSSTables() {
    if (sstables_.empty()) {
        return;
    }

    if (verbose_) {
        cout << "[COMPACTION] Merging " << sstables_.size() << " SSTables\n";
    }
    map<string, string> newest_versions;
    for (auto table_it = sstables_.rbegin(); table_it != sstables_.rend();
         ++table_it) {
        vector<SSTableRecord> records = (*table_it)->ReadAllRecords();
        for (const SSTableRecord& record : records) {
            if (newest_versions.find(record.key) != newest_versions.end()) {
                continue;
            }

            if (record.is_tombstone) {
                newest_versions[record.key] = MemTable::kTombstone;
                continue;
            }

            newest_versions[record.key] = record.value;
        }
    }

    map<string, string> compacted_records;
    for (const auto& [key, value] : newest_versions) {
        if (value == MemTable::kTombstone) {
            continue;
        }
        compacted_records[key] = value;
    }

    string compacted_path = NextSSTablePath();
    auto compacted_table =
        SSTable::CreateFromMap(compacted_path, compacted_records, bloom_filter_bits_);

    for (const auto& table : sstables_) {
        filesystem::remove(table->FilePath());
    }

    sstables_.clear();
    sstables_.push_back(compacted_table);
    SaveManifest();

    if (verbose_) {
        cout << "[COMPACTION] Created " << compacted_path << " with "
             << compacted_table->RecordCount() << " merged record(s)\n";
    }
}

string LSMEngine::NextSSTablePath() {
    ostringstream name_builder;
    name_builder << data_directory_ << '/' << setw(5) << setfill('0')
                 << next_file_number_++ << ".sst";
    return name_builder.str();
}

uint64_t LSMEngine::DetectNextFileNumber() const {
    uint64_t max_file_number = 0;

    if (!filesystem::exists(data_directory_)) {
        return 1;
    }

    for (const auto& entry : filesystem::directory_iterator(data_directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sst") {
            continue;
        }

        string stem = entry.path().stem().string();
        try {
            max_file_number = max<uint64_t>(max_file_number, stoull(stem));
        } catch (const exception&) {
            continue;
        }
    }

    return max_file_number + 1;
}
