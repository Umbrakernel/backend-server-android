#pragma once

#include <cstddef>
#include <string>
#include <vector>

using namespace std;

void append_json_line(const string& file_path, const string& json_line);
void load_location_history_from_json_file(
    const string& file_path,
    vector<float>* time_history,
    vector<float>* lat_history,
    vector<float>* lon_history,
    size_t max_points
);
