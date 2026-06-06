#pragma once

#include <cstddef>
#include <map>
#include <string>

class MemTable {
public:
    static const std::string kTombstone;

    void Set(const std::string& key, const std::string& value);
    void Delete(const std::string& key);

    bool Contains(const std::string& key) const;
    bool IsDeleted(const std::string& key) const;
    bool Get(const std::string& key, std::string& value) const;

    size_t Size() const;
    bool Empty() const;
    void Clear();
    const std::map<std::string, std::string>& Data() const;

private:
    std::map<std::string, std::string> table_;
};
