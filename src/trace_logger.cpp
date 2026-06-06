#include "trace_logger.h"

#include <algorithm>
#include <iostream>

bool g_enabled = true;
int g_indent = 0;
LifecycleStats g_stats;

std::string IndentPrefix() {
    return std::string(static_cast<size_t>(std::max(g_indent, 0)) * 2, ' ');
}

void TraceLogger::SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool TraceLogger::Enabled() {
    return g_enabled;
}

void TraceLogger::ResetStats() {
    g_stats = LifecycleStats();
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

void TraceLogger::Log(const std::string& category, const std::string& message) {
    if (!g_enabled) {
        return;
    }

    std::cout << "[" << category << "] " << IndentPrefix() << message << '\n';
}

void TraceLogger::PushIndent() {
    ++g_indent;
}

void TraceLogger::PopIndent() {
    g_indent = std::max(g_indent - 1, 0);
}

TraceScope::TraceScope(const std::string& category, const std::string& message)
    : category_(category) {
    TraceLogger::Log(category_, "ENTER " + message);
    TraceLogger::PushIndent();
}

TraceScope::~TraceScope() {
    TraceLogger::PopIndent();
    TraceLogger::Log(category_, "EXIT");
}
