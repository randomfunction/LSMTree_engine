#include "memtable.h"

const std::string MemTable::kTombstone = "__LSM_TOMBSTONE__";

void MemTable::Set(const std::string& key, const std::string& value) {
    table_[key] = value;
}

void MemTable::Delete(const std::string& key) {
    table_[key] = kTombstone;
}

bool MemTable::Contains(const std::string& key) const {
    return table_.find(key) != table_.end();
}

bool MemTable::IsDeleted(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = table_.find(key);
    return it != table_.end() && it->second == kTombstone;
}

bool MemTable::Get(const std::string& key, std::string& value) const {
    std::map<std::string, std::string>::const_iterator it = table_.find(key);
    if (it == table_.end() || it->second == kTombstone) {
        return false;
    }

    value = it->second;
    return true;
}

size_t MemTable::Size() const {
    return table_.size();
}

bool MemTable::Empty() const {
    return table_.empty();
}

void MemTable::Clear() {
    table_.clear();
}

const std::map<std::string, std::string>& MemTable::Data() const {
    return table_;
}
