#pragma once

#include <string>
#include <vector>

enum class WALOperationType {
    Set,
    Delete,
};

struct WALRecord {
    WALOperationType type;
    std::string key;
    std::string value;
};

class WAL {
public:
    explicit WAL(const std::string& file_path);
    ~WAL();

    void AppendSet(const std::string& key, const std::string& value);
    void AppendDelete(const std::string& key);
    std::vector<WALRecord> Replay() const;
    void Reset();

    const std::string& FilePath() const;

private:
    void AppendLine(const std::string& line);
    static std::string Escape(const std::string& input);
    static std::string Unescape(const std::string& input);
    static std::vector<std::string> SplitTabSeparatedLine(const std::string& line);
    void OpenForAppend();

    std::string file_path_;
    int file_descriptor_ = -1;
};
