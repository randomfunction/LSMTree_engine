#include "lsm_engine.h"
#include "trace_logger.h"

#include <bits/stdc++.h>
using namespace std;

void PrintGet(LSMEngine& engine, const string& key) {
    string value;
    bool found = engine.Get(key, value);
    cout << "[GET] " << key << " -> " << (found ? value : "Not Found") << '\n';
}

int main() {
    const string data_directory = "demo_data";
    TraceLogger::ResetStats();
    filesystem::remove_all(data_directory);

    cout << "=== LSM-Tree Demo Start ===\n";
    cout << "[STARTUP] Demo reset directory=" << data_directory << '\n';

    {
        cout << "\n=== Phase A: Initial ingest before simulated crash ===\n";
        LSMEngine engine(data_directory, 5, 512);

        for (int i = 0; i < 37; ++i) {
            engine.Set("seq_key_" + to_string(i), "value_" + to_string(i));
        }

        engine.Delete("seq_key_3");
        engine.Delete("seq_key_11");
        engine.Set("profile_user", "alice");
        engine.Set("profile_region", "apac");

        PrintGet(engine, "seq_key_8");
        PrintGet(engine, "seq_key_11");
        engine.PrintState();

        cout << "\n[SHUTDOWN] Simulating crash boundary by destroying engine object\n";
    }

    {
        cout << "\n=== Phase B: Recovery and continued ingestion ===\n";
        LSMEngine recovered_engine(data_directory, 5, 512);

        PrintGet(recovered_engine, "seq_key_34");
        PrintGet(recovered_engine, "profile_user");
        PrintGet(recovered_engine, "seq_key_3");

        mt19937 rng(42);
        uniform_int_distribution<int> key_distribution(0, 29);

        for (int i = 0; i < 35; ++i) {
            string key = "rand_key_" + to_string(key_distribution(rng));
            string value = "rand_value_" + to_string(i);
            recovered_engine.Set(key, value);

            if (i % 9 == 4) {
                recovered_engine.Delete("seq_key_" + to_string(i % 20));
            }
        }

        for (int i = 37; i < 70; ++i) {
            recovered_engine.Set("seq_key_" + to_string(i),
                                 "value_" + to_string(i));
        }

        recovered_engine.FlushMemTable();
        recovered_engine.PrintState();

        vector<string> probe_keys;
        probe_keys.push_back("seq_key_2");
        probe_keys.push_back("seq_key_3");
        probe_keys.push_back("seq_key_52");
        probe_keys.push_back("profile_region");
        probe_keys.push_back("rand_key_7");
        probe_keys.push_back("missing_key");

        cout << "\n=== Final point lookups ===\n";
        for (size_t i = 0; i < probe_keys.size(); ++i) {
            PrintGet(recovered_engine, probe_keys[i]);
        }

        recovered_engine.PrintLifecycleSummary();
    }

    cout << "\n=== LSM-Tree Demo Complete ===\n";
    return 0;
}
