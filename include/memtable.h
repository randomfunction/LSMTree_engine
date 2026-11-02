#pragma once

#include "skiplist.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class MemTable {
public:
    static constexpr uint8_t kTypeDeletion = 0;
    static constexpr uint8_t kTypeValue = 1;
    static constexpr uint64_t kMaxSequenceNumber = (1ull << 56) - 1;
    static const std::string kTombstone;

    enum class LookupResult { kFound, kDeleted, kNotFound };

    MemTable();

    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;

    void Set(const std::string& key, const std::string& value, uint64_t seq);
    void Delete(const std::string& key, uint64_t seq);
    LookupResult Get(const std::string& key, uint64_t snapshot, std::string& value) const;

    std::map<std::string, std::string> SnapshotForFlush() const;

    size_t Size() const;
    bool Empty() const;
    void Clear();

private:
    struct MemEntry {
        std::string user_key;
        uint64_t sequence = 0;
        uint8_t type = kTypeValue;
        std::string value;
    };

    struct MemEntryComparator {
        int operator()(const MemEntry& a, const MemEntry& b) const {
            if (a.user_key < b.user_key) return -1;
            if (a.user_key > b.user_key) return 1;
            if (a.sequence > b.sequence) return -1;
            if (a.sequence < b.sequence) return 1;
            return 0;
        }
    };

    using Table = SkipList<MemEntry, MemEntryComparator>;

    void Add(const std::string& key, const std::string& value, uint64_t seq, uint8_t type);

    std::unique_ptr<Table> table_;
    size_t size_;
};
