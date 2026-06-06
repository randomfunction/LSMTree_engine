#pragma once

#include "memtable.h"
#include "sstable.h"
#include "trace_logger.h"
#include "wal.h"

#include <string>
#include <vector>

class LSMEngine {
public:
    explicit LSMEngine(const std::string& data_directory,
                       size_t memtable_flush_threshold = 5,
                       size_t bloom_filter_bits = 256,
                       bool verbose = true);
    ~LSMEngine();

    void Set(const std::string& key, const std::string& value);
    void Delete(const std::string& key);
    bool Get(const std::string& key, std::string& value) const;

    void Recover();
    void FlushMemTable();
    void PrintState() const;
    void PrintLifecycleSummary() const;

private:
    void LoadManifest();
    void SaveManifest() const;
    void RecoverMemTableFromWAL();
    void MaybeFlushMemTable();
    void MaybeCompact();
    void CompactAllSSTables();
    std::string NextSSTablePath();
    uint64_t DetectNextFileNumber() const;
    static std::string BaseName(const std::string& path);
    static std::string ShowValue(bool found, const std::string& value);

    std::string data_directory_;
    std::string wal_path_;
    std::string manifest_path_;
    size_t memtable_flush_threshold_;
    size_t bloom_filter_bits_;
    uint64_t next_file_number_;
    bool verbose_;

    MemTable memtable_;
    WAL* wal_;
    std::vector<SSTable> sstables_;
};
