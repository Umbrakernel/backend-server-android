#pragma once

#include "telemetry.h"

#include <libpq-fe.h>
#include <map>
#include <vector>

using namespace std;

struct TelemetryHistory {
    vector<float> time_history;
    vector<float> lat_history;
    vector<float> lon_history;
    map<int, vector<float>> rsrp_by_pci;
    map<int, vector<float>> rssi_by_pci;
    map<int, vector<float>> sinr_by_pci;
};

PGconn* connect_db();
bool ensure_telemetry_table(PGconn* connection);
bool insert_telemetry_row(PGconn* connection, const Telemetry& telemetry);
TelemetryHistory load_telemetry_history_from_db(PGconn* connection, int max_points);
