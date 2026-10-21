#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class BloomFilter {
private:
    static size_t HashDJB2(const std::string& key);
    static size_t HashFNV1a(const std::string& key);
    static size_t HashRotate(const std::string& key);
    std::vector<size_t> HashIndices(const std::string& key) const;
    std::string IndicesForLog(const std::vector<size_t>& indices) const;

    size_t bit_count_;
    std::vector<bool> bits_;

public:
    explicit BloomFilter(size_t bit_count = 256);

    void Add(const std::string& key);
    bool MightContain(const std::string& key) const;

    std::vector<uint8_t> Serialize() const;
    static BloomFilter Deserialize(size_t bit_count, const std::vector<uint8_t>& bytes);

    size_t BitCount() const;
};
