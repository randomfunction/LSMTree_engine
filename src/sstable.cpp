#include "sstable.h"

#include "trace_logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

const char kSSTableFooterMagic[8] = {'L', 'S', 'M', 'F', 'T', 'R', '0', '1'};
const std::streamoff kSSTableFooterSize =
    static_cast<std::streamoff>(sizeof(uint64_t) * 5 + sizeof(kSSTableFooterMagic));

SSTable SSTable::CreateFromMap(const std::string& file_path,
                               const std::map<std::string, std::string>& sorted_data,
                               size_t bloom_filter_bits) {
    TraceScope scope("SSTABLE", "SSTable::CreateFromMap file=" +
                                    std::filesystem::path(file_path).filename().string() +
                                    " records=" + std::to_string(sorted_data.size()));
    std::ofstream output(file_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to create SSTable: " + file_path);
    }

    TraceLogger::IncrementSSTablesCreated();
    BloomFilter filter(bloom_filter_bits);
    std::vector<std::pair<std::string, uint64_t> > index_entries;
    index_entries.reserve(sorted_data.size());

    // The MemTable uses std::map, so walking it here naturally writes keys in
    // sorted order and produces a searchable immutable SSTable on disk.
    std::map<std::string, std::string>::const_iterator data_it;
    for (data_it = sorted_data.begin(); data_it != sorted_data.end(); ++data_it) {
        const std::string& key = data_it->first;
        const std::string& value = data_it->second;
        uint64_t record_offset = static_cast<uint64_t>(output.tellp());
        bool is_tombstone = value == MemTable::kTombstone;

        filter.Add(key);
        index_entries.push_back(std::make_pair(key, record_offset));

        WriteUint32(output, static_cast<uint32_t>(key.size()));
        WriteString(output, key);
        WriteUint32(output, static_cast<uint32_t>(value.size()));
        WriteString(output, value);

        uint8_t tombstone_byte = 0;
        if (is_tombstone) {
            tombstone_byte = 1;
        }
        output.write(reinterpret_cast<const char*>(&tombstone_byte), sizeof(tombstone_byte));
    }

    uint64_t index_offset = static_cast<uint64_t>(output.tellp());
    for (size_t i = 0; i < index_entries.size(); ++i) {
        const std::string& key = index_entries[i].first;
        uint64_t offset = index_entries[i].second;
        WriteUint32(output, static_cast<uint32_t>(key.size()));
        WriteString(output, key);
        WriteUint64(output, offset);
    }

    std::vector<uint8_t> bloom_bytes = filter.Serialize();
    uint64_t bloom_offset = static_cast<uint64_t>(output.tellp());
    if (!bloom_bytes.empty()) {
        output.write(reinterpret_cast<const char*>(&bloom_bytes[0]),
                     static_cast<std::streamsize>(bloom_bytes.size()));
    }

    WriteUint64(output, index_offset);
    WriteUint64(output, static_cast<uint64_t>(index_entries.size()));
    WriteUint64(output, bloom_offset);
    WriteUint64(output, static_cast<uint64_t>(bloom_bytes.size()));
    WriteUint64(output, static_cast<uint64_t>(filter.BitCount()));
    output.write(kSSTableFooterMagic, static_cast<std::streamsize>(sizeof(kSSTableFooterMagic)));
    output.close();

    return LoadFromFile(file_path);
}

SSTable SSTable::LoadFromFile(const std::string& file_path) {
    TraceScope scope("SSTABLE", "SSTable::LoadFromFile file=" +
                                    std::filesystem::path(file_path).filename().string());
    SSTable table(file_path);
    table.LoadMetadata();
    return table;
}

SSTable::SSTable(const std::string& file_path)
    : file_path_(file_path), bloom_filter_(8) {
    TraceLogger::Log("SSTABLE", "Constructed SSTable handle file=" + FileNameForLog());
}

bool SSTable::Get(const std::string& key, SSTableRecord& record) const {
    TraceScope scope("READ", "SSTable::Get file=" + FileNameForLog() + " key=" + key);
    if (!MightContain(key)) {
        return false;
    }

    uint64_t offset = 0;
    if (!FindOffset(key, offset)) {
        return false;
    }

    record = ReadRecordAtOffset(offset);
    return true;
}

std::vector<SSTableRecord> SSTable::ReadAllRecords() const {
    TraceScope scope("SSTABLE", "SSTable::ReadAllRecords file=" + FileNameForLog());
    std::vector<SSTableRecord> records;
    records.reserve(index_entries_.size());

    // Compaction scans every record so it can rebuild the newest visible value
    // for each key before writing one replacement SSTable.
    for (size_t i = 0; i < index_entries_.size(); ++i) {
        records.push_back(ReadRecordAtOffset(index_entries_[i].second));
    }

    return records;
}

bool SSTable::MightContain(const std::string& key) const {
    return bloom_filter_.MightContain(key);
}

const std::string& SSTable::FilePath() const {
    return file_path_;
}

size_t SSTable::RecordCount() const {
    return static_cast<size_t>(index_count_);
}

void SSTable::LoadMetadata() {
    TraceScope scope("SSTABLE", "SSTable::LoadMetadata file=" + FileNameForLog());
    std::ifstream input(file_path_.c_str(), std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open SSTable: " + file_path_);
    }

    input.seekg(0, std::ios::end);
    std::streamoff file_size = input.tellg();
    if (file_size < kSSTableFooterSize) {
        throw std::runtime_error("SSTable footer missing: " + file_path_);
    }

    input.seekg(file_size - kSSTableFooterSize);
    index_offset_ = ReadUint64(input);
    index_count_ = ReadUint64(input);
    bloom_offset_ = ReadUint64(input);
    bloom_byte_count_ = ReadUint64(input);
    bloom_bit_count_ = ReadUint64(input);

    char magic[8];
    input.read(magic, static_cast<std::streamsize>(sizeof(magic)));
    for (size_t i = 0; i < sizeof(magic); ++i) {
        if (magic[i] != kSSTableFooterMagic[i]) {
            throw std::runtime_error("SSTable footer magic mismatch: " + file_path_);
        }
    }

    input.seekg(static_cast<std::streamoff>(index_offset_));
    index_entries_.clear();
    index_entries_.reserve(static_cast<size_t>(index_count_));
    for (uint64_t i = 0; i < index_count_; ++i) {
        uint32_t key_length = ReadUint32(input);
        std::string key = ReadString(input, key_length);
        uint64_t offset = ReadUint64(input);
        index_entries_.push_back(std::make_pair(key, offset));
    }

    input.seekg(static_cast<std::streamoff>(bloom_offset_));
    std::vector<uint8_t> bloom_bytes(static_cast<size_t>(bloom_byte_count_), 0);
    if (!bloom_bytes.empty()) {
        input.read(reinterpret_cast<char*>(&bloom_bytes[0]),
                   static_cast<std::streamsize>(bloom_bytes.size()));
    }
    bloom_filter_ = BloomFilter::Deserialize(static_cast<size_t>(bloom_bit_count_),
                                             bloom_bytes);
}

bool SSTable::FindOffset(const std::string& key, uint64_t& offset) const {
    std::vector<std::pair<std::string, uint64_t> >::const_iterator it =
        std::lower_bound(index_entries_.begin(), index_entries_.end(), key,
                         SSTable::IndexEntryKeyLessThan);

    if (it == index_entries_.end() || it->first != key) {
        return false;
    }

    offset = it->second;
    return true;
}

SSTableRecord SSTable::ReadRecordAtOffset(uint64_t offset) const {
    std::ifstream input(file_path_.c_str(), std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open SSTable record reader: " + file_path_);
    }

    input.seekg(static_cast<std::streamoff>(offset));

    uint32_t key_length = ReadUint32(input);
    std::string key = ReadString(input, key_length);
    uint32_t value_length = ReadUint32(input);
    std::string value = ReadString(input, value_length);

    uint8_t tombstone_byte = 0;
    input.read(reinterpret_cast<char*>(&tombstone_byte), sizeof(tombstone_byte));

    SSTableRecord record;
    record.key = key;
    record.value = value;
    record.is_tombstone = tombstone_byte == 1;
    return record;
}

std::string SSTable::FileNameForLog() const {
    return std::filesystem::path(file_path_).filename().string();
}

void SSTable::WriteString(std::ofstream& output, const std::string& value) {
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

std::string SSTable::ReadString(std::ifstream& input, uint32_t length) {
    std::string value(length, '\0');
    if (length > 0) {
        input.read(&value[0], static_cast<std::streamsize>(length));
    }
    return value;
}

bool SSTable::IndexEntryKeyLessThan(const std::pair<std::string, uint64_t>& entry,
                                    const std::string& target_key) {
    return entry.first < target_key;
}

void SSTable::WriteUint32(std::ofstream& output, uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void SSTable::WriteUint64(std::ofstream& output, uint64_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t SSTable::ReadUint32(std::ifstream& input) {
    uint32_t value = 0;
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint64_t SSTable::ReadUint64(std::ifstream& input) {
    uint64_t value = 0;
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}
