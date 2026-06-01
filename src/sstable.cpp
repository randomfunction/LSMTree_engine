#include "sstable.h"

#include "trace_logger.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace {

constexpr array<char, 8> kFooterMagic = {'L', 'S', 'M', 'F', 'T', 'R', '0', '1'};
constexpr streamoff kFooterSize =
    static_cast<streamoff>(sizeof(uint64_t) * 5 + kFooterMagic.size());

void WriteString(ofstream& output, const string& value) {
    output.write(value.data(), static_cast<streamsize>(value.size()));
}

string ReadString(ifstream& input, uint32_t length) {
    string value(length, '\0');
    if (length > 0) {
        input.read(&value[0], static_cast<streamsize>(length));
    }
    return value;
}

}  // namespace

shared_ptr<SSTable> SSTable::CreateFromMap(const string& file_path,
                                           const map<string, string>& sorted_data,
                                           size_t bloom_filter_bits) {
    TraceScope scope("SSTABLE", "SSTable::CreateFromMap file=" +
                                    filesystem::path(file_path).filename().string() +
                                    " records=" + to_string(sorted_data.size()));
    ofstream output(file_path, ios::binary | ios::trunc);
    if (!output.is_open()) {
        throw runtime_error("Failed to create SSTable: " + file_path);
    }

    TraceLogger::IncrementSSTablesCreated();
    TraceLogger::Log("FLUSH", "Writing sorted records to " +
                                  filesystem::path(file_path).filename().string());
    BloomFilter filter(bloom_filter_bits);
    vector<pair<string, uint64_t>> index_entries;
    index_entries.reserve(sorted_data.size());

    for (const auto& [key, value] : sorted_data) {
        uint64_t record_offset = static_cast<uint64_t>(output.tellp());
        bool is_tombstone = value == MemTable::kTombstone;

        filter.Add(key);
        TraceLogger::Log("BLOOM", "Inserted key=" + key + " into " +
                                      filesystem::path(file_path).filename().string());
        index_entries.push_back({key, record_offset});
        TraceLogger::Log("INDEX", "Added key=" + key + " offset=" +
                                      to_string(record_offset));

        WriteUint32(output, static_cast<uint32_t>(key.size()));
        WriteString(output, key);
        WriteUint32(output, static_cast<uint32_t>(value.size()));
        WriteString(output, value);
        uint8_t tombstone_byte = is_tombstone ? 1 : 0;
        output.write(reinterpret_cast<const char*>(&tombstone_byte),
                     sizeof(tombstone_byte));
    }

    uint64_t index_offset = static_cast<uint64_t>(output.tellp());
    TraceLogger::Log("INDEX", "Writing index section offset=" +
                                  to_string(index_offset) + " entries=" +
                                  to_string(index_entries.size()));
    for (const auto& [key, offset] : index_entries) {
        WriteUint32(output, static_cast<uint32_t>(key.size()));
        WriteString(output, key);
        WriteUint64(output, offset);
    }

    vector<uint8_t> bloom_bytes = filter.Serialize();
    uint64_t bloom_offset = static_cast<uint64_t>(output.tellp());
    TraceLogger::Log("BLOOM", "Writing Bloom filter offset=" +
                                  to_string(bloom_offset) + " bytes=" +
                                  to_string(bloom_bytes.size()));
    if (!bloom_bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bloom_bytes.data()),
                     static_cast<streamsize>(bloom_bytes.size()));
    }

    WriteUint64(output, index_offset);
    WriteUint64(output, static_cast<uint64_t>(index_entries.size()));
    WriteUint64(output, bloom_offset);
    WriteUint64(output, static_cast<uint64_t>(bloom_bytes.size()));
    WriteUint64(output, static_cast<uint64_t>(filter.BitCount()));
    output.write(kFooterMagic.data(), static_cast<streamsize>(kFooterMagic.size()));
    TraceLogger::Log("SSTABLE", "Footer written index_offset=" +
                                    to_string(index_offset) +
                                    " bloom_offset=" + to_string(bloom_offset));

    output.close();
    return LoadFromFile(file_path);
}

shared_ptr<SSTable> SSTable::LoadFromFile(const string& file_path) {
    TraceScope scope("SSTABLE", "SSTable::LoadFromFile file=" +
                                    filesystem::path(file_path).filename().string());
    auto table = make_shared<SSTable>(file_path);
    table->LoadMetadata();
    return table;
}

SSTable::SSTable(string file_path)
    : file_path_(move(file_path)), bloom_filter_(8) {
    TraceLogger::Log("SSTABLE", "Constructed SSTable handle file=" + FileNameForLog());
}

optional<SSTableRecord> SSTable::Get(const string& key) const {
    TraceScope scope("READ", "SSTable::Get file=" + FileNameForLog() + " key=" +
                                 key);
    if (!MightContain(key)) {
        TraceLogger::Log("READ", "Bloom filter rejected key=" + key +
                                     " file=" + FileNameForLog());
        return nullopt;
    }

    optional<uint64_t> offset = FindOffset(key);
    if (!offset.has_value()) {
        TraceLogger::Log("INDEX", "binary search miss key=" + key + " file=" +
                                      FileNameForLog());
        return nullopt;
    }
    TraceLogger::Log("INDEX", "binary search found offset=" +
                                  to_string(*offset) + " key=" + key +
                                  " file=" + FileNameForLog());

    return ReadRecordAtOffset(*offset);
}

vector<SSTableRecord> SSTable::ReadAllRecords() const {
    TraceScope scope("SSTABLE", "SSTable::ReadAllRecords file=" + FileNameForLog());
    ifstream input(file_path_, ios::binary);
    if (!input.is_open()) {
        throw runtime_error("Failed to open SSTable for scan: " + file_path_);
    }

    vector<SSTableRecord> records;
    records.reserve(index_entries_.size());
    TraceLogger::Log("SSTABLE", "Sequential scan records=" +
                                  to_string(index_entries_.size()) + " file=" +
                                  FileNameForLog());

    for (const auto& [key, offset] : index_entries_) {
        (void)key;
        records.push_back(ReadRecordAtOffset(offset));
    }

    return records;
}

bool SSTable::MightContain(const string& key) const {
    TraceLogger::Log("READ", "Bloom filter probe file=" + FileNameForLog() +
                                 " key=" + key);
    return bloom_filter_.MightContain(key);
}

const string& SSTable::FilePath() const {
    return file_path_;
}

size_t SSTable::RecordCount() const {
    return static_cast<size_t>(index_count_);
}

void SSTable::LoadMetadata() {
    TraceScope scope("SSTABLE", "SSTable::LoadMetadata file=" + FileNameForLog());
    ifstream input(file_path_, ios::binary);
    if (!input.is_open()) {
        throw runtime_error("Failed to open SSTable: " + file_path_);
    }

    input.seekg(0, ios::end);
    streamoff file_size = input.tellg();
    if (file_size < kFooterSize) {
        throw runtime_error("SSTable footer missing: " + file_path_);
    }

    input.seekg(file_size - kFooterSize);
    index_offset_ = ReadUint64(input);
    index_count_ = ReadUint64(input);
    bloom_offset_ = ReadUint64(input);
    bloom_byte_count_ = ReadUint64(input);
    bloom_bit_count_ = ReadUint64(input);
    TraceLogger::Log("SSTABLE", "Footer metadata index_offset=" +
                                    to_string(index_offset_) +
                                    " index_count=" + to_string(index_count_) +
                                    " bloom_offset=" + to_string(bloom_offset_) +
                                    " bloom_bytes=" +
                                    to_string(bloom_byte_count_) +
                                    " bloom_bits=" +
                                    to_string(bloom_bit_count_));

    array<char, 8> magic{};
    input.read(magic.data(), static_cast<streamsize>(magic.size()));
    if (magic != kFooterMagic) {
        throw runtime_error("SSTable footer magic mismatch: " + file_path_);
    }
    TraceLogger::Log("SSTABLE", "Footer magic verified file=" + FileNameForLog());

    input.seekg(static_cast<streamoff>(index_offset_));
    index_entries_.clear();
    index_entries_.reserve(static_cast<size_t>(index_count_));
    for (uint64_t i = 0; i < index_count_; ++i) {
        uint32_t key_length = ReadUint32(input);
        string key = ReadString(input, key_length);
        uint64_t offset = ReadUint64(input);
        index_entries_.push_back({key, offset});
        TraceLogger::Log("INDEX", "Loaded key=" + key + " offset=" +
                                      to_string(offset) + " from " +
                                      FileNameForLog());
    }

    input.seekg(static_cast<streamoff>(bloom_offset_));
    vector<uint8_t> bloom_bytes(static_cast<size_t>(bloom_byte_count_), 0);
    if (bloom_byte_count_ > 0) {
        input.read(reinterpret_cast<char*>(bloom_bytes.data()),
                   static_cast<streamsize>(bloom_bytes.size()));
    }
    bloom_filter_ = BloomFilter::Deserialize(static_cast<size_t>(bloom_bit_count_),
                                             bloom_bytes);
    TraceLogger::Log("BLOOM", "Loaded Bloom filter file=" + FileNameForLog());
}

optional<uint64_t> SSTable::FindOffset(const string& key) const {
    TraceLogger::Log("INDEX", "binary search index entries=" +
                                  to_string(index_entries_.size()) + " key=" +
                                  key + " file=" + FileNameForLog());
    auto it = lower_bound(
        index_entries_.begin(), index_entries_.end(), key,
        [](const pair<string, uint64_t>& entry, const string& target_key) {
            return entry.first < target_key;
        });

    if (it == index_entries_.end() || it->first != key) {
        return nullopt;
    }

    return it->second;
}

SSTableRecord SSTable::ReadRecordAtOffset(uint64_t offset) const {
    TraceScope scope("READ", "SSTable::ReadRecordAtOffset file=" +
                                 FileNameForLog() + " offset=" +
                                 to_string(offset));
    ifstream input(file_path_, ios::binary);
    if (!input.is_open()) {
        throw runtime_error("Failed to open SSTable record reader: " + file_path_);
    }

    input.seekg(static_cast<streamoff>(offset));

    uint32_t key_length = ReadUint32(input);
    string key = ReadString(input, key_length);
    uint32_t value_length = ReadUint32(input);
    string value = ReadString(input, value_length);

    uint8_t tombstone_byte = 0;
    input.read(reinterpret_cast<char*>(&tombstone_byte), sizeof(tombstone_byte));
    TraceLogger::Log("READ", "Loaded record key=" + key + " value=" + value +
                                 " tombstone=" +
                                 string(tombstone_byte == 1 ? "true" : "false"));
    return {key, value, tombstone_byte == 1};
}

void SSTable::WriteUint32(ofstream& output, uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void SSTable::WriteUint64(ofstream& output, uint64_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t SSTable::ReadUint32(ifstream& input) {
    uint32_t value = 0;
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint64_t SSTable::ReadUint64(ifstream& input) {
    uint64_t value = 0;
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

string SSTable::FileNameForLog() const {
    return filesystem::path(file_path_).filename().string();
}
