#pragma once

#include <bits/stdc++.h>
using namespace std;

class MemTable {
    
private:
    map<string, string> table_;

public:
    static const string kTombstone;

    void Set(const string& key, const string& value);
    void Delete(const string& key);

    bool Contains(const string& key) const;
    bool IsDeleted(const string& key) const;
    bool Get(const string& key, string& value) const;

    size_t Size() const;
    bool Empty() const;
    void Clear();
    const map<string, string>& Data() const;
};
