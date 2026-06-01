#include "lsm_engine.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace {

string BaseName(const string& path) {
    return filesystem::path(path).filename().string();
}

string ShowValue(const optional<string>& value) {
    return value.has_value() ? *value : "Not Found";
}

}  // namespace

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
      wal_(nullptr) {
    TraceLogger::SetEnabled(verbose_);
    TraceScope scope("STARTUP", "LSMEngine::LSMEngine data_directory=" +
                                    data_directory_ + " flush_threshold=" +
                                    to_string(memtable_flush_threshold_) +
                                    " bloom_bits=" +
                                    to_string(bloom_filter_bits_));
    TraceLogger::Log("STARTUP", "Initializing storage directory path=" +
                                    data_directory_);
    filesystem::create_directories(data_directory_);
    TraceLogger::Log("STARTUP", "Directory ready path=" + data_directory_);
    wal_ = make_unique<WAL>(wal_path_);
    Recover();
}

LSMEngine::~LSMEngine() {
    TraceScope scope("SHUTDOWN", "LSMEngine::~LSMEngine data_directory=" +
                                     data_directory_);
    TraceLogger::Log("SHUTDOWN", "Final in-memory keys=" +
                                      to_string(memtable_.Size()) +
                                      " active_sstables=" +
                                      to_string(sstables_.size()));
}

void LSMEngine::Set(const string& key, const string& value) {
    TraceScope scope("WRITE", "LSMEngine::Set key=" + key + " value=" + value);
    wal_->AppendSet(key, value);
    TraceLogger::Log("WAL", "Durability record appended for SET key=" + key +
                                " value=" + value);

    memtable_.Set(key, value);
    TraceLogger::Log("MEMTABLE", "Applied SET key=" + key + " value=" + value +
                                     " memtable_size=" +
                                     to_string(memtable_.Size()));

    MaybeFlushMemTable();
}

void LSMEngine::Delete(const string& key) {
    TraceScope scope("WRITE", "LSMEngine::Delete key=" + key);
    wal_->AppendDelete(key);
    TraceLogger::Log("WAL", "Durability record appended for DELETE key=" + key);

    memtable_.Delete(key);
    TraceLogger::Log("MEMTABLE", "Applied DELETE tombstone key=" + key +
                                     " memtable_size=" +
                                     to_string(memtable_.Size()));

    MaybeFlushMemTable();
}

optional<string> LSMEngine::Get(const string& key) const {
    TraceScope scope("READ", "LSMEngine::Get key=" + key);
    TraceLogger::Log("READ", "Checking MemTable for key=" + key);
    if (memtable_.Contains(key)) {
        TraceLogger::Log("READ", "MemTable hit for key=" + key);
        if (memtable_.IsDeleted(key)) {
            TraceLogger::Log("READ", "Tombstone detected in MemTable key=" + key +
                                         " => not found");
            return nullopt;
        }
        optional<string> value = memtable_.Get(key);
        TraceLogger::Log("READ", "Returning MemTable value key=" + key +
                                     " value=" + ShowValue(value));
        return value;
    }
    TraceLogger::Log("READ", "MemTable miss for key=" + key);

    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        TraceLogger::Log("READ", "Checking SSTable " +
                                     BaseName((*it)->FilePath()) + " for key=" +
                                     key);
        if (!(*it)->MightContain(key)) {
            TraceLogger::Log("READ", "Bloom filter negative => skipping " +
                                         BaseName((*it)->FilePath()));
            continue;
        }

        optional<SSTableRecord> record = (*it)->Get(key);
        if (!record.has_value()) {
            TraceLogger::Log("READ", "Index lookup miss in " +
                                         BaseName((*it)->FilePath()) +
                                         " key=" + key);
            continue;
        }

        if (record->is_tombstone) {
            TraceLogger::Log("READ", "Tombstone detected in " +
                                         BaseName((*it)->FilePath()) +
                                         " key=" + key + " => not found");
            return nullopt;
        }

        TraceLogger::Log("READ", "Returning SSTable value key=" + key +
                                     " value=" + record->value + " source=" +
                                     BaseName((*it)->FilePath()));
        return record->value;
    }

    TraceLogger::Log("READ", "Key=" + key +
                                 " not found in MemTable or any SSTable");
    return nullopt;
}

void LSMEngine::Recover() {
    TraceScope scope("RECOVERY", "LSMEngine::Recover directory=" +
                                       data_directory_);
    LoadManifest();
    next_file_number_ = DetectNextFileNumber();
    TraceLogger::Log("RECOVERY", "Next SSTable file number=" +
                                     to_string(next_file_number_));
    RecoverMemTableFromWAL();
    TraceLogger::Log("RECOVERY", "Recovery complete sstables=" +
                                     to_string(sstables_.size()) +
                                     " memtable_keys=" +
                                     to_string(memtable_.Size()));
}

void LSMEngine::FlushMemTable() {
    TraceScope scope("FLUSH", "LSMEngine::FlushMemTable");
    if (memtable_.Empty()) {
        TraceLogger::Log("FLUSH", "MemTable empty => nothing to flush");
        return;
    }

    TraceLogger::IncrementFlushes();
    TraceLogger::Log("FLUSH", "MemTable persistence pipeline starting keys=" +
                                  to_string(memtable_.Size()));
    string sstable_path = NextSSTablePath();
    TraceLogger::Log("FLUSH", "Creating SSTable file=" +
                                  BaseName(sstable_path));
    auto sstable =
        SSTable::CreateFromMap(sstable_path, memtable_.Data(), bloom_filter_bits_);

    sstables_.push_back(sstable);
    TraceLogger::Log("FLUSH", "Flush complete file=" + BaseName(sstable_path) +
                                  " records=" +
                                  to_string(sstable->RecordCount()) +
                                  " active_sstables=" +
                                  to_string(sstables_.size()));

    memtable_.Clear();
    TraceLogger::Log("MEMTABLE", "Cleared MemTable after flush size=0");
    wal_->Reset();
    TraceLogger::Log("WAL", "Reset active WAL file=" +
                                BaseName(wal_->FilePath()) +
                                " after successful flush");

    SaveManifest();
    MaybeCompact();
}

void LSMEngine::PrintState() const {
    TraceScope scope("STATE", "LSMEngine::PrintState");
    cout << "\n[STATE] Current engine snapshot\n";
    cout << "  MemTable keys: " << memtable_.Size() << '\n';
    cout << "  SSTables: " << sstables_.size() << '\n';
    for (const auto& table : sstables_) {
        cout << "    - " << table->FilePath() << " (" << table->RecordCount()
             << " records)\n";
    }
}

void LSMEngine::PrintLifecycleSummary() const {
    TraceScope scope("SUMMARY", "LSMEngine::PrintLifecycleSummary");
    LifecycleStats stats = TraceLogger::Stats();
    cout << "\n=== End-to-End Lifecycle Summary ===\n";
    cout << "total WAL appends      : " << stats.wal_appends << '\n';
    cout << "total flushes          : " << stats.flushes << '\n';
    cout << "total SSTables created : " << stats.sstables_created << '\n';
    cout << "total compactions      : " << stats.compactions << '\n';
    cout << "total tombstones removed: " << stats.tombstones_removed << '\n';
    cout << "final active SSTables  : " << sstables_.size() << '\n';
    cout << "final MemTable size    : " << memtable_.Size() << '\n';
    for (const auto& table : sstables_) {
        cout << "  - " << BaseName(table->FilePath()) << " records="
             << table->RecordCount() << '\n';
    }
}

void LSMEngine::LoadManifest() {
    TraceScope scope("MANIFEST", "LSMEngine::LoadManifest file=" +
                                        manifest_path_);
    sstables_.clear();

    ifstream manifest(manifest_path_);
    if (!manifest.is_open()) {
        TraceLogger::Log("MANIFEST", "Manifest missing => starting with no SSTables");
        return;
    }
    TraceLogger::Log("MANIFEST", "Loading manifest entries from " +
                                    BaseName(manifest_path_));

    string line;
    while (getline(manifest, line)) {
        if (line.empty()) {
            continue;
        }

        if (!filesystem::exists(line)) {
            TraceLogger::Log("MANIFEST", "Skipping missing SSTable path=" + line);
            continue;
        }

        TraceLogger::Log("MANIFEST", "Loading SSTable from manifest path=" + line);
        sstables_.push_back(SSTable::LoadFromFile(line));
    }
    TraceLogger::Log("MANIFEST", "Manifest load complete active_sstables=" +
                                    to_string(sstables_.size()));
}

void LSMEngine::SaveManifest() const {
    TraceScope scope("MANIFEST", "LSMEngine::SaveManifest file=" +
                                        manifest_path_);
    string temp_manifest_path = manifest_path_ + ".tmp";
    ofstream manifest(temp_manifest_path, ios::trunc);
    if (!manifest.is_open()) {
        throw runtime_error("Failed to write manifest: " + temp_manifest_path);
    }

    for (const auto& table : sstables_) {
        TraceLogger::Log("MANIFEST", "Writing entry " + table->FilePath());
        manifest << table->FilePath() << '\n';
    }

    manifest.close();
    filesystem::rename(temp_manifest_path, manifest_path_);
    TraceLogger::Log("MANIFEST", "Manifest rewritten via temp file=" +
                                    BaseName(temp_manifest_path) +
                                    " active_sstables=" +
                                    to_string(sstables_.size()));
}

void LSMEngine::RecoverMemTableFromWAL() {
    TraceScope scope("RECOVERY", "LSMEngine::RecoverMemTableFromWAL file=" +
                                       wal_path_);
    memtable_.Clear();
    TraceLogger::Log("MEMTABLE", "Cleared MemTable before WAL replay");

    vector<WALRecord> records = wal_->Replay();
    TraceLogger::Log("RECOVERY", "Applying replayed WAL records count=" +
                                     to_string(records.size()));
    for (const WALRecord& record : records) {
        if (record.type == WALOperationType::Set) {
            memtable_.Set(record.key, record.value);
            TraceLogger::Log("MEMTABLE", "Replayed SET key=" + record.key +
                                             " value=" + record.value +
                                             " memtable_size=" +
                                             to_string(memtable_.Size()));
        } else {
            memtable_.Delete(record.key);
            TraceLogger::Log("MEMTABLE", "Replayed DELETE key=" + record.key +
                                             " memtable_size=" +
                                             to_string(memtable_.Size()));
        }
    }
}

void LSMEngine::MaybeFlushMemTable() {
    TraceScope scope("MEMTABLE", "LSMEngine::MaybeFlushMemTable");
    TraceLogger::Log("MEMTABLE", "Threshold check size=" +
                                     to_string(memtable_.Size()) +
                                     " threshold=" +
                                     to_string(memtable_flush_threshold_));
    if (memtable_.Size() >= memtable_flush_threshold_) {
        TraceLogger::Log("FLUSH", "MemTable threshold exceeded => triggering flush");
        FlushMemTable();
        return;
    }
    TraceLogger::Log("MEMTABLE", "Threshold not reached => staying in memory");
}

void LSMEngine::MaybeCompact() {
    TraceScope scope("COMPACTION", "LSMEngine::MaybeCompact");
    TraceLogger::Log("COMPACTION", "Compaction check active_sstables=" +
                                       to_string(sstables_.size()) +
                                       " threshold=4");
    if (sstables_.size() > 3) {
        TraceLogger::Log("COMPACTION", "Threshold exceeded => starting compaction");
        CompactAllSSTables();
        return;
    }
    TraceLogger::Log("COMPACTION", "Compaction not needed");
}

void LSMEngine::CompactAllSSTables() {
    TraceScope scope("COMPACTION", "LSMEngine::CompactAllSSTables");
    if (sstables_.empty()) {
        TraceLogger::Log("COMPACTION", "No SSTables present => skipping");
        return;
    }

    TraceLogger::IncrementCompactions();
    TraceLogger::Log("COMPACTION", "Reading SSTables newest -> oldest count=" +
                                       to_string(sstables_.size()));
    map<string, string> newest_versions;
    for (auto table_it = sstables_.rbegin(); table_it != sstables_.rend();
         ++table_it) {
        TraceLogger::Log("COMPACTION", "Scanning " +
                                         BaseName((*table_it)->FilePath()));
        vector<SSTableRecord> records = (*table_it)->ReadAllRecords();
        for (const SSTableRecord& record : records) {
            if (newest_versions.find(record.key) != newest_versions.end()) {
                TraceLogger::Log("COMPACTION",
                                 "Dropping overwritten version key=" +
                                     record.key + " from older SSTable");
                continue;
            }

            if (record.is_tombstone) {
                newest_versions[record.key] = MemTable::kTombstone;
                TraceLogger::Log("COMPACTION",
                                 "Keeping latest tombstone key=" + record.key);
                continue;
            }

            newest_versions[record.key] = record.value;
            TraceLogger::Log("COMPACTION",
                             "Keeping latest version key=" + record.key +
                                 " value=" + record.value);
        }
    }

    map<string, string> compacted_records;
    uint64_t removed_tombstones = 0;
    for (const auto& [key, value] : newest_versions) {
        if (value == MemTable::kTombstone) {
            ++removed_tombstones;
            TraceLogger::Log("COMPACTION", "Removing tombstoned key=" + key);
            continue;
        }
        compacted_records[key] = value;
    }
    TraceLogger::AddTombstonesRemoved(removed_tombstones);

    string compacted_path = NextSSTablePath();
    TraceLogger::Log("COMPACTION", "Writing compacted SSTable file=" +
                                       BaseName(compacted_path) +
                                       " merged_records=" +
                                       to_string(compacted_records.size()));
    auto compacted_table =
        SSTable::CreateFromMap(compacted_path, compacted_records, bloom_filter_bits_);

    for (const auto& table : sstables_) {
        TraceLogger::Log("COMPACTION", "Removing old SSTable file=" +
                                         BaseName(table->FilePath()));
        filesystem::remove(table->FilePath());
    }

    sstables_.clear();
    sstables_.push_back(compacted_table);
    SaveManifest();
    TraceLogger::Log("COMPACTION", "Compaction complete new_file=" +
                                       BaseName(compacted_path) +
                                       " records=" +
                                       to_string(compacted_table->RecordCount()) +
                                       " tombstones_removed=" +
                                       to_string(removed_tombstones));
}

string LSMEngine::NextSSTablePath() {
    ostringstream name_builder;
    name_builder << data_directory_ << '/' << setw(5) << setfill('0')
                 << next_file_number_++ << ".sst";
    string path = name_builder.str();
    TraceLogger::Log("SSTABLE", "Allocated next SSTable path=" +
                                  BaseName(path));
    return path;
}

uint64_t LSMEngine::DetectNextFileNumber() const {
    TraceScope scope("RECOVERY", "LSMEngine::DetectNextFileNumber");
    uint64_t max_file_number = 0;

    if (!filesystem::exists(data_directory_)) {
        TraceLogger::Log("RECOVERY", "Data directory missing => next file number=1");
        return 1;
    }

    for (const auto& entry : filesystem::directory_iterator(data_directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sst") {
            continue;
        }

        string stem = entry.path().stem().string();
        try {
            max_file_number = max<uint64_t>(max_file_number, stoull(stem));
            TraceLogger::Log("RECOVERY", "Discovered SSTable file=" +
                                             entry.path().filename().string() +
                                             " file_number=" + stem);
        } catch (const exception&) {
            continue;
        }
    }

    return max_file_number + 1;
}
