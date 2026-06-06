#pragma once

#include "bloom_filter.h"
#include "memtable.h"

#include <bits/stdc++.h>
using namespace std;

struct SSTableRecord {
    string key;
    string value;
    bool is_tombstone = false;
};

class SSTable {
public:
    static SSTable CreateFromMap(
        const string& file_path,
        const map<string, string>& sorted_data,
        size_t bloom_filter_bits);

    static SSTable LoadFromFile(const string& file_path);

    explicit SSTable(const string& file_path);

    bool Get(const string& key, SSTableRecord& record) const;
    vector<SSTableRecord> ReadAllRecords() const;
    bool MightContain(const string& key) const;

    const string& FilePath() const;
    size_t RecordCount() const;

private:
    void LoadMetadata();
    bool FindOffset(const string& key, uint64_t& offset) const;
    SSTableRecord ReadRecordAtOffset(uint64_t offset) const;
    string FileNameForLog() const;
    static void WriteString(ofstream& output, const string& value);
    static string ReadString(ifstream& input, uint32_t length);
    static bool IndexEntryKeyLessThan(const pair<string, uint64_t>& entry,
                                      const string& target_key);

    static void WriteUint32(ofstream& output, uint32_t value);
    static void WriteUint64(ofstream& output, uint64_t value);
    static uint32_t ReadUint32(ifstream& input);
    static uint64_t ReadUint64(ifstream& input);

    string file_path_;
    uint64_t index_offset_ = 0;
    uint64_t index_count_ = 0;
    uint64_t bloom_offset_ = 0;
    uint64_t bloom_byte_count_ = 0;
    uint64_t bloom_bit_count_ = 0;
    vector<pair<string, uint64_t> > index_entries_;
    BloomFilter bloom_filter_;
};
