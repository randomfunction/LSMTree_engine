#include "trace_logger.h"

#include <algorithm>
#include <iostream>
#include <utility>

using namespace std;

namespace {

bool g_enabled = true;
int g_indent = 0;
LifecycleStats g_stats;

string IndentPrefix() {
    return string(static_cast<size_t>(max(g_indent, 0)) * 2, ' ');
}

}  // namespace

void TraceLogger::SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool TraceLogger::Enabled() {
    return g_enabled;
}

void TraceLogger::ResetStats() {
    g_stats = {};
}

LifecycleStats TraceLogger::Stats() {
    return g_stats;
}

void TraceLogger::IncrementWALAppends() {
    ++g_stats.wal_appends;
}

void TraceLogger::IncrementFlushes() {
    ++g_stats.flushes;
}

void TraceLogger::IncrementSSTablesCreated() {
    ++g_stats.sstables_created;
}

void TraceLogger::IncrementCompactions() {
    ++g_stats.compactions;
}

void TraceLogger::AddTombstonesRemoved(uint64_t count) {
    g_stats.tombstones_removed += count;
}

void TraceLogger::Log(const string& category, const string& message) {
    if (!g_enabled) {
        return;
    }

    cout << "[" << category << "] " << IndentPrefix() << message << '\n';
}

void TraceLogger::PushIndent() {
    ++g_indent;
}

void TraceLogger::PopIndent() {
    g_indent = max(g_indent - 1, 0);
}

TraceScope::TraceScope(string category, string message)
    : category_(move(category)) {
    TraceLogger::Log(category_, "ENTER " + message);
    TraceLogger::PushIndent();
}

TraceScope::~TraceScope() {
    TraceLogger::PopIndent();
    TraceLogger::Log(category_, "EXIT");
}
