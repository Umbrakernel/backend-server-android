#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace std;

struct Telemetry {
    float latitude = 55.0084f;
    float longitude = 82.9357f;
    float altitude = 0.0f;
    float accuracy = 0.0f;
    long long time = 0;

    int rsrp = -200;
    int rsrq = -200;
    int rssi = -200;
    float rssnr = -9999.0f;
    int pci = 0;

    string network_type = "UNKNOWN";
    string operator_name = "UNKNOWN";
};

struct TelemetrySnapshot {
    Telemetry current;
    vector<float> time_history;
    map<int, vector<float>> rsrp_by_pci;
    map<int, vector<float>> rssi_by_pci;
    map<int, vector<float>> sinr_by_pci;
};

class TelemetryState {
public:
    void update(const Telemetry& telemetry);
    TelemetrySnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    Telemetry current_;
    vector<float> time_history_;
    map<int, vector<float>> rsrp_by_pci_;
    map<int, vector<float>> rssi_by_pci_;
    map<int, vector<float>> sinr_by_pci_;
};
