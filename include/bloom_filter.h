#pragma once

#include <bits/stdc++.h>
using namespace std;

class BloomFilter {
private:
    static size_t HashDJB2(const string& key);
    static size_t HashFNV1a(const string& key);
    static size_t HashRotate(const string& key);
    vector<size_t> HashIndices(const string& key) const;
    string IndicesForLog(const vector<size_t>& indices) const;

    size_t bit_count_;
    vector<bool> bits_;

public:
    explicit BloomFilter(size_t bit_count = 256);

    void Add(const string& key);
    bool MightContain(const string& key) const;

    vector<uint8_t> Serialize() const;
    static BloomFilter Deserialize(size_t bit_count, const vector<uint8_t>& bytes);

    size_t BitCount() const;
};
