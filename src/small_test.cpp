#include "lsm_engine.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

std::string ShowValue(LSMEngine& engine, const std::string& key) {
    std::string value;
    if (engine.Get(key, value)) {
        return value;
    }
    return "Not Found";
}

int main() {
    const std::string data_directory = "small_test_data";
    std::filesystem::remove_all(data_directory);

    std::cout << "LSM TREE SMALL TEST\n";
    std::cout << "-------------------\n";

    {
        LSMEngine engine(data_directory, 3, 128, false);

        engine.Set("apple", "red");
        engine.Set("banana", "yellow");
        engine.Set("carrot", "orange");
        engine.Delete("banana");
        engine.Set("date", "brown");

        std::cout << "Run 1\n";
        std::cout << "  apple  -> " << ShowValue(engine, "apple") << '\n';
        std::cout << "  banana -> " << ShowValue(engine, "banana") << '\n';
        std::cout << "  date   -> " << ShowValue(engine, "date") << '\n';
    }

    {
        LSMEngine engine(data_directory, 3, 128, false);

        std::cout << "Run 2 (after restart)\n";
        std::cout << "  apple  -> " << ShowValue(engine, "apple") << '\n';
        std::cout << "  banana -> " << ShowValue(engine, "banana") << '\n';
        std::cout << "  carrot -> " << ShowValue(engine, "carrot") << '\n';
        std::cout << "  date   -> " << ShowValue(engine, "date") << '\n';
    }

    std::vector<std::string> files;
    std::filesystem::directory_iterator end_it;
    for (std::filesystem::directory_iterator it(data_directory); it != end_it; ++it) {
        if (it->is_regular_file()) {
            files.push_back(it->path().filename().string());
        }
    }

    std::cout << "Files\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  - " << files[i] << '\n';
    }

    return 0;
}
