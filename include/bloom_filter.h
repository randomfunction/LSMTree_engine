#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace std;

class BloomFilter {
    
private:
        vector<size_t> HashIndices(const string& key) const;
        string IndicesForLog(const vector<size_t>& indices) const;
    
        size_t bit_count_;
        vector<bool> bits_;

public:
    // prevents compiler form using constructor for implicit conversions
    explicit BloomFilter(size_t bit_count = 256);

    void Add(const string& key);
    bool MightContain(const string& key) const;

    vector<uint8_t> Serialize() const;
    static BloomFilter Deserialize(size_t bit_count,const vector<uint8_t>& bytes);

    size_t BitCount() const;
};
