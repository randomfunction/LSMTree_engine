#include "memtable.h"

#include <map>
#include <string>

using namespace std;

const string MemTable::kTombstone = "__LSM_TOMBSTONE__";

MemTable::MemTable() : table_(new Table(MemEntryComparator())), size_(0) {}

void MemTable::Add(const string& key, const string& value, uint64_t seq, uint8_t type) {
    MemEntry entry;
    entry.user_key = key;
    entry.sequence = seq;
    entry.type = type;
    entry.value = value;
    table_->Insert(entry);
    ++size_;
}

void MemTable::Set(const string& key, const string& value, uint64_t seq) {
    Add(key, value, seq, kTypeValue);
}

void MemTable::Delete(const string& key, uint64_t seq) {
    Add(key, string(), seq, kTypeDeletion);
}

MemTable::LookupResult MemTable::Get(const string& key, uint64_t snapshot,
                                     string& value) const {
    MemEntry target;
    target.user_key = key;
    target.sequence = snapshot;
    target.type = kTypeValue;

    Table::Iterator it(table_.get());
    it.Seek(target);
    if (it.Valid() && it.key().user_key == key) {
        if (it.key().type == kTypeValue) {
            value = it.key().value;
            return LookupResult::kFound;
        }
        return LookupResult::kDeleted;
    }
    return LookupResult::kNotFound;
}

map<string, string> MemTable::SnapshotForFlush() const {
    map<string, string> result;
    Table::Iterator it(table_.get());
    it.SeekToFirst();

    string last_key;
    bool have_last = false;
    while (it.Valid()) {
        const string& user_key = it.key().user_key;
        if (!have_last || user_key != last_key) {
            if (it.key().type == kTypeValue) {
                result[user_key] = it.key().value;
            } else {
                result[user_key] = kTombstone;
            }
            last_key = user_key;
            have_last = true;
        }
        it.Next();
    }
    return result;
}

size_t MemTable::Size() const {
    return size_;
}

bool MemTable::Empty() const {
    return size_ == 0;
}

void MemTable::Clear() {
    table_.reset(new Table(MemEntryComparator()));
    size_ = 0;
}
