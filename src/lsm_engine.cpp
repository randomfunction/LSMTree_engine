#include "lsm_engine.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

LSMEngine::LSMEngine(const std::string& data_directory,
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
      wal_(0) {
    TraceLogger::SetEnabled(verbose_);
    std::filesystem::create_directories(data_directory_);
    wal_ = new WAL(wal_path_);
    Recover();
}

LSMEngine::~LSMEngine() {
    delete wal_;
    wal_ = 0;
}

void LSMEngine::Set(const std::string& key, const std::string& value) {
    wal_->AppendSet(key, value);

    // The WAL is written first so a crash can replay the change before the
    // MemTable is flushed into an SSTable.
    memtable_.Set(key, value);
    MaybeFlushMemTable();
}

void LSMEngine::Delete(const std::string& key) {
    wal_->AppendDelete(key);
    memtable_.Delete(key);
    MaybeFlushMemTable();
}

bool LSMEngine::Get(const std::string& key, std::string& value) const {
    if (memtable_.Contains(key)) {
        if (memtable_.IsDeleted(key)) {
            return false;
        }
        return memtable_.Get(key, value);
    }

    // Newer SSTables are checked first because they contain more recent
    // versions of a key than older immutable files.
    std::vector<SSTable>::const_reverse_iterator it;
    for (it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        if (!it->MightContain(key)) {
            continue;
        }

        SSTableRecord record;
        if (!it->Get(key, record)) {
            continue;
        }

        if (record.is_tombstone) {
            return false;
        }

        value = record.value;
        return true;
    }

    return false;
}

void LSMEngine::Recover() {
    LoadManifest();
    next_file_number_ = DetectNextFileNumber();
    RecoverMemTableFromWAL();
}

void LSMEngine::FlushMemTable() {
    if (memtable_.Empty()) {
        return;
    }

    std::string sstable_path = NextSSTablePath();
    SSTable sstable =
        SSTable::CreateFromMap(sstable_path, memtable_.Data(), bloom_filter_bits_);
    sstables_.push_back(sstable);

    memtable_.Clear();
    wal_->Reset();
    SaveManifest();
    MaybeCompact();
}

void LSMEngine::PrintState() const {
    std::cout << "\n[STATE] Current engine snapshot\n";
    std::cout << "  MemTable keys: " << memtable_.Size() << '\n';
    std::cout << "  SSTables: " << sstables_.size() << '\n';

    for (size_t i = 0; i < sstables_.size(); ++i) {
        std::cout << "    - " << sstables_[i].FilePath() << " ("
                  << sstables_[i].RecordCount() << " records)\n";
    }
}

void LSMEngine::PrintLifecycleSummary() const {
    LifecycleStats stats = TraceLogger::Stats();
    std::cout << "\n=== End-to-End Lifecycle Summary ===\n";
    std::cout << "total WAL appends      : " << stats.wal_appends << '\n';
    std::cout << "total flushes          : " << stats.flushes << '\n';
    std::cout << "total SSTables created : " << stats.sstables_created << '\n';
    std::cout << "total compactions      : " << stats.compactions << '\n';
    std::cout << "total tombstones removed: " << stats.tombstones_removed << '\n';
    std::cout << "final active SSTables  : " << sstables_.size() << '\n';
    std::cout << "final MemTable size    : " << memtable_.Size() << '\n';

    for (size_t i = 0; i < sstables_.size(); ++i) {
        std::cout << "  - " << BaseName(sstables_[i].FilePath()) << " records="
                  << sstables_[i].RecordCount() << '\n';
    }
}

void LSMEngine::LoadManifest() {
    sstables_.clear();

    std::ifstream manifest(manifest_path_.c_str());
    if (!manifest.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty()) {
            continue;
        }

        if (!std::filesystem::exists(line)) {
            continue;
        }

        sstables_.push_back(SSTable::LoadFromFile(line));
    }
}

void LSMEngine::SaveManifest() const {
    std::string temp_manifest_path = manifest_path_ + ".tmp";
    std::ofstream manifest(temp_manifest_path.c_str(), std::ios::trunc);
    if (!manifest.is_open()) {
        throw std::runtime_error("Failed to write manifest: " + temp_manifest_path);
    }

    for (size_t i = 0; i < sstables_.size(); ++i) {
        manifest << sstables_[i].FilePath() << '\n';
    }

    manifest.close();
    std::filesystem::rename(temp_manifest_path, manifest_path_);
}

void LSMEngine::RecoverMemTableFromWAL() {
    memtable_.Clear();

    std::vector<WALRecord> records = wal_->Replay();
    for (size_t i = 0; i < records.size(); ++i) {
        const WALRecord& record = records[i];
        if (record.type == WALOperationType::Set) {
            memtable_.Set(record.key, record.value);
        } else {
            memtable_.Delete(record.key);
        }
    }
}

void LSMEngine::MaybeFlushMemTable() {
    if (memtable_.Size() >= memtable_flush_threshold_) {
        TraceLogger::IncrementFlushes();
        FlushMemTable();
    }
}

void LSMEngine::MaybeCompact() {
    if (sstables_.size() > 3) {
        TraceLogger::IncrementCompactions();
        CompactAllSSTables();
    }
}

void LSMEngine::CompactAllSSTables() {
    if (sstables_.empty()) {
        return;
    }

    std::map<std::string, std::string> newest_versions;

    // Reading newest to oldest keeps the first version we see for each key,
    // which matches the visible state of the LSM tree at compaction time.
    std::vector<SSTable>::reverse_iterator table_it;
    for (table_it = sstables_.rbegin(); table_it != sstables_.rend(); ++table_it) {
        std::vector<SSTableRecord> records = table_it->ReadAllRecords();
        for (size_t i = 0; i < records.size(); ++i) {
            const SSTableRecord& record = records[i];
            if (newest_versions.find(record.key) != newest_versions.end()) {
                continue;
            }

            if (record.is_tombstone) {
                newest_versions[record.key] = MemTable::kTombstone;
            } else {
                newest_versions[record.key] = record.value;
            }
        }
    }

    std::map<std::string, std::string> compacted_records;
    uint64_t removed_tombstones = 0;
    std::map<std::string, std::string>::const_iterator value_it;
    for (value_it = newest_versions.begin(); value_it != newest_versions.end(); ++value_it) {
        if (value_it->second == MemTable::kTombstone) {
            ++removed_tombstones;
            continue;
        }
        compacted_records[value_it->first] = value_it->second;
    }
    TraceLogger::AddTombstonesRemoved(removed_tombstones);

    std::string compacted_path = NextSSTablePath();
    SSTable compacted_table =
        SSTable::CreateFromMap(compacted_path, compacted_records, bloom_filter_bits_);

    for (size_t i = 0; i < sstables_.size(); ++i) {
        std::filesystem::remove(sstables_[i].FilePath());
    }

    sstables_.clear();
    sstables_.push_back(compacted_table);
    SaveManifest();
}

std::string LSMEngine::NextSSTablePath() {
    std::ostringstream name_builder;
    name_builder << data_directory_ << '/' << std::setw(5) << std::setfill('0')
                 << next_file_number_++ << ".sst";
    return name_builder.str();
}

uint64_t LSMEngine::DetectNextFileNumber() const {
    uint64_t max_file_number = 0;

    if (!std::filesystem::exists(data_directory_)) {
        return 1;
    }

    std::filesystem::directory_iterator end_it;
    for (std::filesystem::directory_iterator it(data_directory_); it != end_it; ++it) {
        if (!it->is_regular_file() || it->path().extension() != ".sst") {
            continue;
        }

        std::string stem = it->path().stem().string();
        try {
            uint64_t file_number = static_cast<uint64_t>(std::stoull(stem));
            if (file_number > max_file_number) {
                max_file_number = file_number;
            }
        } catch (const std::exception&) {
        }
    }

    return max_file_number + 1;
}

std::string LSMEngine::BaseName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::string LSMEngine::ShowValue(bool found, const std::string& value) {
    if (found) {
        return value;
    }
    return "Not Found";
}
