#pragma once

#include "bloom_filter.h"
#include "memtable.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

struct SSTableRecord {
    std::string key;
    std::string value;
    bool is_tombstone = false;
};

class SSTable {
public:
    static SSTable CreateFromMap(
        const std::string& file_path,
        const std::map<std::string, std::string>& sorted_data,
        size_t bloom_filter_bits);

    static SSTable LoadFromFile(const std::string& file_path);

    explicit SSTable(const std::string& file_path);

    bool Get(const std::string& key, SSTableRecord& record) const;
    std::vector<SSTableRecord> ReadAllRecords() const;
    bool MightContain(const std::string& key) const;

    const std::string& FilePath() const;
    size_t RecordCount() const;

private:
    void LoadMetadata();
    bool FindOffset(const std::string& key, uint64_t& offset) const;
    SSTableRecord ReadRecordAtOffset(uint64_t offset) const;
    std::string FileNameForLog() const;
    static void WriteString(std::ofstream& output, const std::string& value);
    static std::string ReadString(std::ifstream& input, uint32_t length);
    static bool IndexEntryKeyLessThan(const std::pair<std::string, uint64_t>& entry,
                                      const std::string& target_key);

    static void WriteUint32(std::ofstream& output, uint32_t value);
    static void WriteUint64(std::ofstream& output, uint64_t value);
    static uint32_t ReadUint32(std::ifstream& input);
    static uint64_t ReadUint64(std::ifstream& input);

    std::string file_path_;
    uint64_t index_offset_ = 0;
    uint64_t index_count_ = 0;
    uint64_t bloom_offset_ = 0;
    uint64_t bloom_byte_count_ = 0;
    uint64_t bloom_bit_count_ = 0;
    std::vector<std::pair<std::string, uint64_t> > index_entries_;
    BloomFilter bloom_filter_;
};
