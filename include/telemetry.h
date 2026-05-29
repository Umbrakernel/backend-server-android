#pragma once

#include <mutex>
#include <string>

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
    int earfcn = 0;

    string network_type = "UNKNOWN";
    string operator_name = "UNKNOWN";
};

struct TelemetrySnapshot {
    Telemetry current;
};

class TelemetryState {
public:
    void update(const Telemetry& telemetry);
    TelemetrySnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    Telemetry current_;
};
