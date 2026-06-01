#pragma once

#include <cstdint>
#include <string>

using namespace std;

struct LifecycleStats {
    uint64_t wal_appends = 0;
    uint64_t flushes = 0;
    uint64_t sstables_created = 0;
    uint64_t compactions = 0;
    uint64_t tombstones_removed = 0;
};

class TraceLogger {
public:
    static void SetEnabled(bool enabled);
    static bool Enabled();

    static void ResetStats();
    static LifecycleStats Stats();

    static void IncrementWALAppends();
    static void IncrementFlushes();
    static void IncrementSSTablesCreated();
    static void IncrementCompactions();
    static void AddTombstonesRemoved(uint64_t count);

    static void Log(const string& category, const string& message);
    static void PushIndent();
    static void PopIndent();
};

class TraceScope {
public:
    TraceScope(string category, string message);
    ~TraceScope();

private:
    string category_;
};
