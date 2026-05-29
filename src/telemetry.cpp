#include "telemetry.h"

using namespace std;

void TelemetryState::update(const Telemetry& telemetry) {
    lock_guard<mutex> lock(mutex_);
    current_ = telemetry;
}

TelemetrySnapshot TelemetryState::snapshot() const {
    lock_guard<mutex> lock(mutex_);
    return {current_};
}
