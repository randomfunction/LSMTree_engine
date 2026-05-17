#include "lsm_engine.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

string Show(optional<string> value) {
    return value.has_value() ? *value : "Not Found";
}

}  // namespace

int main() {
    const string data_directory = "readme_demo_data";
    filesystem::remove_all(data_directory);

    cout << "LSM TREE QUICK TEST\n";
    cout << "-------------------\n";

    {
        LSMEngine engine(data_directory, 3, 128, false);

        engine.Set("apple", "red");
        engine.Set("banana", "yellow");
        engine.Set("carrot", "orange");
        engine.Delete("banana");
        engine.Set("date", "brown");

        cout << "Run 1\n";
        cout << "  apple  -> " << Show(engine.Get("apple")) << '\n';
        cout << "  banana -> " << Show(engine.Get("banana")) << '\n';
        cout << "  date   -> " << Show(engine.Get("date")) << '\n';
    }

    {
        LSMEngine engine(data_directory, 3, 128, false);

        cout << "Run 2 (after restart)\n";
        cout << "  apple  -> " << Show(engine.Get("apple")) << '\n';
        cout << "  banana -> " << Show(engine.Get("banana")) << '\n';
        cout << "  carrot -> " << Show(engine.Get("carrot")) << '\n';
        cout << "  date   -> " << Show(engine.Get("date")) << '\n';
    }

    vector<string> files;
    for (const auto& entry : filesystem::directory_iterator(data_directory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().filename().string());
        }
    }

    cout << "Files\n";
    for (const string& file : files) {
        cout << "  - " << file << '\n';
    }

    return 0;
}
