#include "wal.h"

#include "trace_logger.h"

#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

WAL::WAL(const string& file_path) : file_path_(file_path) {
    TraceScope scope("WAL", "WAL::WAL file=" +
                                filesystem::path(file_path_).filename().string());
    filesystem::path wal_path(file_path_);
    if (wal_path.has_parent_path()) {
        filesystem::create_directories(wal_path.parent_path());
        TraceLogger::Log("WAL", "Ensured WAL directory exists path=" +
                                    wal_path.parent_path().string());
    }

    OpenForAppend();
}

WAL::~WAL() {
    TraceScope scope("WAL", "WAL::~WAL file=" +
                                filesystem::path(file_path_).filename().string());
    if (file_descriptor_ >= 0) {
        close(file_descriptor_);
        file_descriptor_ = -1;
        TraceLogger::Log("WAL", "Closed WAL file descriptor");
    }
}

void WAL::AppendSet(const string& key, const string& value) {
    TraceScope scope("WAL", "WAL::AppendSet key=" + key + " value=" + value);
    AppendLine("SET\t" + Escape(key) + '\t' + Escape(value));
}

void WAL::AppendDelete(const string& key) {
    TraceScope scope("WAL", "WAL::AppendDelete key=" + key);
    AppendLine("DELETE\t" + Escape(key));
}

vector<WALRecord> WAL::Replay() const {
    TraceScope scope("RECOVERY", "WAL::Replay file=" +
                                     filesystem::path(file_path_).filename().string());
    ifstream input_stream(file_path_.c_str());
    if (!input_stream.is_open()) {
        TraceLogger::Log("RECOVERY", "WAL missing => replay yields 0 records");
        return vector<WALRecord>();
    }

    vector<WALRecord> records;
    string line;
    while (getline(input_stream, line)) {
        if (line.empty()) {
            continue;
        }

        vector<string> parts = SplitTabSeparatedLine(line);
        if (parts.empty()) {
            continue;
        }

        if (parts[0] == "SET" && parts.size() == 3) {
            records.push_back(
                {WALOperationType::Set, Unescape(parts[1]), Unescape(parts[2])});
            TraceLogger::Log("RECOVERY", "Replayed WAL SET key=" +
                                             records.back().key + " value=" +
                                             records.back().value);
        } else if (parts[0] == "DELETE" && parts.size() == 2) {
            records.push_back({WALOperationType::Delete, Unescape(parts[1]), ""});
            TraceLogger::Log("RECOVERY", "Replayed WAL DELETE key=" +
                                             records.back().key);
        }
    }

    TraceLogger::Log("RECOVERY", "WAL replay complete count=" +
                                     to_string(records.size()));
    return records;
}

void WAL::Reset() {
    TraceScope scope("WAL", "WAL::Reset file=" +
                                filesystem::path(file_path_).filename().string());
    if (file_descriptor_ >= 0) {
        close(file_descriptor_);
        file_descriptor_ = -1;
    }

    ofstream truncate_stream(file_path_.c_str(), ios::trunc);
    truncate_stream.close();
    TraceLogger::Log("WAL", "Truncated WAL file on flush boundary");

    OpenForAppend();
}

const string& WAL::FilePath() const {
    return file_path_;
}

void WAL::AppendLine(const string& line) {
    string payload = line + '\n';
    ssize_t written =
        write(file_descriptor_, payload.data(), static_cast<size_t>(payload.size()));
    if (written != static_cast<ssize_t>(payload.size())) {
        throw runtime_error("Failed to append to WAL file: " + file_path_);
    }

    if (fsync(file_descriptor_) != 0) {
        throw runtime_error("Failed to fsync WAL file: " + file_path_);
    }

    TraceLogger::IncrementWALAppends();
    TraceLogger::Log("WAL", "Appended record bytes=" + to_string(payload.size()) +
                                " file=" +
                                filesystem::path(file_path_).filename().string());
    TraceLogger::Log("WAL", "fsync complete file=" +
                                filesystem::path(file_path_).filename().string());
}

string WAL::Escape(const string& input) {
    string escaped;
    escaped.reserve(input.size());

    for (string::const_iterator it = input.begin(); it != input.end(); ++it) {
        char ch = *it;
        if (ch == '\\') {
            escaped += "\\\\";
        } else if (ch == '\t') {
            escaped += "\\t";
        } else if (ch == '\n') {
            escaped += "\\n";
        } else {
            escaped += ch;
        }
    }

    return escaped;
}

string WAL::Unescape(const string& input) {
    string unescaped;
    unescaped.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            char next = input[i + 1];
            if (next == 't') {
                unescaped += '\t';
                ++i;
                continue;
            }
            if (next == 'n') {
                unescaped += '\n';
                ++i;
                continue;
            }
            if (next == '\\') {
                unescaped += '\\';
                ++i;
                continue;
            }
        }

        unescaped += input[i];
    }

    return unescaped;
}

vector<string> WAL::SplitTabSeparatedLine(const string& line) {
    vector<string> parts;
    stringstream stream(line);
    string token;

    while (getline(stream, token, '\t')) {
        parts.push_back(token);
    }

    return parts;
}

void WAL::OpenForAppend() {
    file_descriptor_ = open(file_path_.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (file_descriptor_ < 0) {
        throw runtime_error("Failed to open WAL file: " + file_path_);
    }
    TraceLogger::Log("WAL", "Opened WAL for append file=" +
                                filesystem::path(file_path_).filename().string());
}
