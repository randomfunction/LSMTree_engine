#include "memtable.h"
#include<bits/stdc++.h>

using namespace std;

const string MemTable::kTombstone = "__LSM_TOMBSTONE__";

void MemTable::Set(const string& key, const string& value) {
    table_[key] = value;
}

void MemTable::Delete(const string& key) {
    table_[key] = kTombstone;
}

bool MemTable::Contains(const string& key) const {
    return table_.find(key) != table_.end();
}

bool MemTable::IsDeleted(const string& key) const {
    map<string, string>::const_iterator it = table_.find(key);
    return it != table_.end() && it->second == kTombstone;
}

bool MemTable::Get(const string& key, string& value) const {
    map<string, string>::const_iterator it = table_.find(key);
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

const map<string, string>& MemTable::Data() const {
    return table_;
}
