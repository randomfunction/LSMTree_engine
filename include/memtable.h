#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>

using namespace std;

class MemTable {
public:
    static const string kTombstone;

    void Set(const string& key, const string& value);
    void Delete(const string& key);

    bool Contains(const string& key) const;
    bool IsDeleted(const string& key) const;
    optional<string> Get(const string& key) const;

    size_t Size() const;
    bool Empty() const;
    void Clear();
    const map<string, string>& Data() const;

private:
    map<string, string> table_;
};
