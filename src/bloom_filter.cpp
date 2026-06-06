#include "bloom_filter.h"
#include "trace_logger.h"

#include <algorithm>
#include <sstream>

size_t BloomFilter::HashDJB2(const std::string& key) {
    size_t hash = 5381;
    for (std::string::const_iterator it = key.begin(); it != key.end(); ++it) {
        unsigned char ch = static_cast<unsigned char>(*it);
        hash = ((hash << 5U) + hash) + ch;
    }
    return hash;
}

size_t BloomFilter::HashFNV1a(const std::string& key) {
    size_t hash = 1469598103934665603ULL;
    for (std::string::const_iterator it = key.begin(); it != key.end(); ++it) {
        unsigned char ch = static_cast<unsigned char>(*it);
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t BloomFilter::HashRotate(const std::string& key) {
    size_t hash = 0;
    for (std::string::const_iterator it = key.begin(); it != key.end(); ++it) {
        unsigned char ch = static_cast<unsigned char>(*it);
        hash = (hash << 7U) ^ (hash >> 3U) ^ ch;
    }
    return hash;
}

BloomFilter::BloomFilter(size_t bit_count)
    : bit_count_(std::max<size_t>(bit_count, 8)), bits_(bit_count_, false) {
    TraceLogger::Log("BLOOM", "Initialized BloomFilter bit_count=" +
                                  std::to_string(bit_count_) + " minimum_bits=8");
}

void BloomFilter::Add(const std::string& key) {
    std::vector<size_t> indices = HashIndices(key);
    TraceLogger::Log("BLOOM", "Insert key=" + key + " indices=" +
                                  IndicesForLog(indices));
    for (size_t i = 0; i < indices.size(); ++i) {
        bits_[indices[i]] = true;
    }
}

bool BloomFilter::MightContain(const std::string& key) const {
    std::vector<size_t> indices = HashIndices(key);
    TraceLogger::Log("BLOOM", "Check key=" + key + " indices=" +
                                  IndicesForLog(indices));
    for (size_t i = 0; i < indices.size(); ++i) {
        size_t index = indices[i];
        if (!bits_[index]) {
            TraceLogger::Log("BLOOM", "negative => missing bit at index=" +
                                          std::to_string(index));
            return false;
        }
    }

    TraceLogger::Log("BLOOM", "possible hit => all hash bits present");
    return true;
}

std::vector<uint8_t> BloomFilter::Serialize() const {
    std::vector<uint8_t> bytes((bit_count_ + 7) / 8, 0);

    for (size_t i = 0; i < bit_count_; ++i) {
        if (bits_[i]) {
            bytes[i / 8] |= static_cast<uint8_t>(1U << (i % 8));
        }
    }

    TraceLogger::Log("BLOOM", "Serialized Bloom filter bytes=" +
                                  std::to_string(bytes.size()) + " bit_count=" +
                                  std::to_string(bit_count_));
    return bytes;
}

BloomFilter BloomFilter::Deserialize(size_t bit_count, const std::vector<uint8_t>& bytes) {
    BloomFilter filter(bit_count);
    TraceLogger::Log("BLOOM", "Deserializing Bloom filter bytes=" +
                                  std::to_string(bytes.size()) + " bit_count=" +
                                  std::to_string(bit_count));
    for (size_t i = 0; i < filter.bit_count_; ++i) {
        bool is_set =
            i / 8 < bytes.size() &&
            (bytes[i / 8] & static_cast<uint8_t>(1U << (i % 8))) != 0;
        filter.bits_[i] = is_set;
    }

    return filter;
}

size_t BloomFilter::BitCount() const {
    return bit_count_;
}

std::vector<size_t> BloomFilter::HashIndices(const std::string& key) const {
    std::vector<size_t> indices;
    indices.push_back(HashDJB2(key) % bit_count_);
    indices.push_back(HashFNV1a(key) % bit_count_);
    indices.push_back(HashRotate(key) % bit_count_);
    return indices;
}

std::string BloomFilter::IndicesForLog(const std::vector<size_t>& indices) const {
    std::ostringstream stream;
    stream << "[";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << indices[i];
    }
    stream << "]";
    return stream.str();
}
