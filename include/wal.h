#pragma once

#include <bits/stdc++.h>
using namespace std;

enum class WALOperationType {
    Set,
    Delete,
};

struct WALRecord {
    WALOperationType type;
    string key;
    string value;
};

class WAL {
public:
    explicit WAL(const string& file_path);
    ~WAL();

    void AppendSet(const string& key, const string& value);
    void AppendDelete(const string& key);
    vector<WALRecord> Replay() const;
    void Reset();

    const string& FilePath() const;

private:
    void AppendLine(const string& line);
    static string Escape(const string& input);
    static string Unescape(const string& input);
    static vector<string> SplitTabSeparatedLine(const string& line);
    void OpenForAppend();

    string file_path_;
    int file_descriptor_ = -1;
};
