#pragma once

#include "memtable.h"
#include "sstable.h"
#include "wal.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace std;

class LSMEngine {
public:
    explicit LSMEngine(const string& data_directory,
                       size_t memtable_flush_threshold = 5,
                       size_t bloom_filter_bits = 256,
                       bool verbose = true);

    void Set(const string& key, const string& value);
    void Delete(const string& key);
    optional<string> Get(const string& key) const;

    void Recover();
    void FlushMemTable();
    void PrintState() const;

private:
    void LoadManifest();
    void SaveManifest() const;
    void RecoverMemTableFromWAL();
    void MaybeFlushMemTable();
    void MaybeCompact();
    void CompactAllSSTables();
    string NextSSTablePath();
    uint64_t DetectNextFileNumber() const;

    string data_directory_;
    string wal_path_;
    string manifest_path_;
    size_t memtable_flush_threshold_;
    size_t bloom_filter_bits_;
    uint64_t next_file_number_;
    bool verbose_;

    MemTable memtable_;
    unique_ptr<WAL> wal_;
    vector<shared_ptr<SSTable>> sstables_;
};
