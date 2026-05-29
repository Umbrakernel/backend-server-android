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
    vector<float> altitude_history;
    vector<float> rsrp_history;
    vector<float> rsrq_history;
    vector<float> rssi_history;
    vector<int> pci_history;
    map<int, vector<float>> rsrp_by_pci;
    map<int, vector<float>> rssi_by_pci;
    map<int, vector<float>> sinr_by_pci;
};

PGconn* connect_db();
bool ensure_telemetry_table(PGconn* connection);
bool insert_telemetry_row(PGconn* connection, const Telemetry& telemetry);
int import_telemetry_json_file(PGconn* connection, const string& file_path);
void mark_json_line_inserted(PGconn* connection, const string& file_path);
TelemetryHistory load_telemetry_history_from_db(PGconn* connection, int max_points);
vector<int> load_pcis_from_db(PGconn* connection);
