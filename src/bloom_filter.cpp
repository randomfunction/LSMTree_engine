#include "bloom_filter.h"

#include <algorithm>

using namespace std;

namespace {

size_t HashDJB2(const string& key) {
    size_t hash = 5381;
    for (unsigned char ch : key) {
        hash = ((hash << 5U) + hash) + ch;
    }
    return hash;
}

size_t HashFNV1a(const string& key) {
    size_t hash = 1469598103934665603ULL;
    for (unsigned char ch : key) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t HashRotate(const string& key) {
    size_t hash = 0;
    for (unsigned char ch : key) {
        hash = (hash << 7U) ^ (hash >> 3U) ^ ch;
    }
    return hash;
}

}  // namespace

BloomFilter::BloomFilter(size_t bit_count)
    : bit_count_(max<size_t>(bit_count, 8)), bits_(bit_count_, false) {}

void BloomFilter::Add(const string& key) {
    for (size_t index : HashIndices(key)) {
        bits_[index] = true;
    }
}

bool BloomFilter::MightContain(const string& key) const {
    for (size_t index : HashIndices(key)) {
        if (!bits_[index]) {
            return false;
        }
    }

    return true;
}

vector<uint8_t> BloomFilter::Serialize() const {
    vector<uint8_t> bytes((bit_count_ + 7) / 8, 0);

    for (size_t i = 0; i < bit_count_; ++i) {
        if (bits_[i]) {
            bytes[i / 8] |= static_cast<uint8_t>(1U << (i % 8));
        }
    }

    return bytes;
}

BloomFilter BloomFilter::Deserialize(size_t bit_count,
                                     const vector<uint8_t>& bytes) {
    BloomFilter filter(bit_count);
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

vector<size_t> BloomFilter::HashIndices(const string& key) const {
    return {
        HashDJB2(key) % bit_count_,
        HashFNV1a(key) % bit_count_,
        HashRotate(key) % bit_count_,
    };
}
