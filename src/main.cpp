#include "lsm_engine.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;

namespace {

void PrintGet(LSMEngine& engine, const string& key) {
    optional<string> value = engine.Get(key);
    cout << "[GET] " << key << " -> "
         << (value.has_value() ? *value : "Not Found") << '\n';
}

}  // namespace

int main() {
    const string data_directory = "demo_data";
    filesystem::remove_all(data_directory);

    cout << "=== LSM-Tree Demo Start ===\n";

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

        cout << "\n[CRASH] Simulating process crash by destroying the engine object\n";
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

        vector<string> probe_keys = {
            "seq_key_2",
            "seq_key_3",
            "seq_key_52",
            "profile_region",
            "rand_key_7",
            "missing_key",
        };

        cout << "\n=== Final point lookups ===\n";
        for (const string& key : probe_keys) {
            PrintGet(recovered_engine, key);
        }
    }

    cout << "\n=== LSM-Tree Demo Complete ===\n";
    return 0;
}
