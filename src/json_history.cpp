#include "json_history.h"

#include <fstream>
#include <nlohmann/json.hpp>

using namespace std;

using json = nlohmann::json;

void append_json_line(const string& file_path, const string& json_line) {
    ofstream file(file_path, ios::app);
    if (file.is_open()) {
        file << json_line << '\n';
    }
}

void load_location_history_from_json_file(
    const string& file_path,
    vector<float>* time_history,
    vector<float>* lat_history,
    vector<float>* lon_history,
    size_t max_points
) {
    ifstream file(file_path);
    if (!file.is_open()) {
        return;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            const auto j = json::parse(line);
            if (!j.contains("time") || !j.contains("latitude") || !j.contains("longitude")) {
                continue;
            }
            if (j["latitude"].is_null() || j["longitude"].is_null()) {
                continue;
            }

            const long long time = j.value("time", 0LL);
            const float latitude = j.value("latitude", 0.0f);
            const float longitude = j.value("longitude", 0.0f);

            time_history->push_back(static_cast<float>(time) / 1000.0f);
            lat_history->push_back(latitude);
            lon_history->push_back(longitude);
        } catch (...) {
        }
    }

    if (time_history->size() > max_points) {
        const size_t extra = time_history->size() - max_points;
        time_history->erase(time_history->begin(), time_history->begin() + static_cast<long long>(extra));
        lat_history->erase(lat_history->begin(), lat_history->begin() + static_cast<long long>(extra));
        lon_history->erase(lon_history->begin(), lon_history->begin() + static_cast<long long>(extra));
    }
}
