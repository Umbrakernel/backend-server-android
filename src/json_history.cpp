#include "json_history.h"

#include <filesystem>
#include <fstream>

using namespace std;

void append_json_line(const string& file_path, const string& json_line) {
    filesystem::create_directories(filesystem::path(file_path).parent_path());
    ofstream file(file_path, ios::app);
    if (file.is_open()) {
        file << json_line << '\n';
    }
}
