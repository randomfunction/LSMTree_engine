#include "wal.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace {

vector<string> SplitTabSeparatedLine(const string& line) {
    vector<string> parts;
    stringstream stream(line);
    string token;

    while (getline(stream, token, '\t')) {
        parts.push_back(token);
    }

    return parts;
}

}  // namespace

WAL::WAL(string file_path) : file_path_(move(file_path)) {
    filesystem::path wal_path(file_path_);
    if (wal_path.has_parent_path()) {
        filesystem::create_directories(wal_path.parent_path());
    }

    output_stream_.open(file_path_, ios::app);
    if (!output_stream_.is_open()) {
        throw runtime_error("Failed to open WAL file: " + file_path_);
    }
}

WAL::~WAL() {
    if (output_stream_.is_open()) {
        output_stream_.close();
    }
}

void WAL::AppendSet(const string& key, const string& value) {
    AppendLine("SET\t" + Escape(key) + '\t' + Escape(value));
}

void WAL::AppendDelete(const string& key) {
    AppendLine("DELETE\t" + Escape(key));
}

vector<WALRecord> WAL::Replay() const {
    ifstream input_stream(file_path_);
    if (!input_stream.is_open()) {
        return {};
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
        } else if (parts[0] == "DELETE" && parts.size() == 2) {
            records.push_back({WALOperationType::Delete, Unescape(parts[1]), ""});
        }
    }

    return records;
}

void WAL::Reset() {
    if (output_stream_.is_open()) {
        output_stream_.close();
    }

    ofstream truncate_stream(file_path_, ios::trunc);
    truncate_stream.close();

    output_stream_.open(file_path_, ios::app);
    if (!output_stream_.is_open()) {
        throw runtime_error("Failed to reopen WAL file: " + file_path_);
    }
}

const string& WAL::FilePath() const {
    return file_path_;
}

void WAL::AppendLine(const string& line) {
    output_stream_ << line << '\n';
    output_stream_.flush();

    if (!output_stream_) {
        throw runtime_error("Failed to append to WAL file: " + file_path_);
    }
}

string WAL::Escape(const string& input) {
    string escaped;
    escaped.reserve(input.size());

    for (char ch : input) {
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
