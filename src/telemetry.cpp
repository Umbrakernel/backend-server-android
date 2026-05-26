#include "telemetry.h"

#include "app_config.h"

using namespace std;

namespace {

void trim_vector(vector<float>& values) {
    if (values.size() > MAX_HISTORY_POINTS) {
        values.erase(values.begin());
    }
}

void trim_map(map<int, vector<float>>& values_by_pci) {
    for (auto& [pci, values] : values_by_pci) {
        trim_vector(values);
    }
}

} // namespace

void TelemetryState::update(const Telemetry& telemetry) {
    lock_guard<mutex> lock(mutex_);

    current_ = telemetry;

    const float seconds = static_cast<float>(telemetry.time) / 1000.0f;
    time_history_.push_back(seconds);
    rsrp_by_pci_[telemetry.pci].push_back(static_cast<float>(telemetry.rsrp));
    rssi_by_pci_[telemetry.pci].push_back(static_cast<float>(telemetry.rssi));
    sinr_by_pci_[telemetry.pci].push_back(telemetry.rssnr);

    trim_vector(time_history_);
    trim_map(rsrp_by_pci_);
    trim_map(rssi_by_pci_);
    trim_map(sinr_by_pci_);
}

TelemetrySnapshot TelemetryState::snapshot() const {
    lock_guard<mutex> lock(mutex_);
    return {
        current_,
        time_history_,
        rsrp_by_pci_,
        rssi_by_pci_,
        sinr_by_pci_,
    };
}
