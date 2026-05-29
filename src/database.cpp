#include "database.h"

#include "app_config.h"
#include "telemetry_json.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using namespace std;
using json = nlohmann::json;

namespace {

bool exec_command(PGconn* connection, const string& query, const char* error_prefix) {
    PGresult* result = PQexec(connection, query.c_str());
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!ok) {
        cerr << error_prefix << ": " << PQresultErrorMessage(result) << endl;
    }
    PQclear(result);
    return ok;
}

string sql_text(PGconn* connection, const string& value) {
    char* escaped = PQescapeLiteral(connection, value.c_str(), value.size());
    string result = escaped;
    PQfreemem(escaped);
    return result;
}

} // namespace

PGconn* connect_db() {
    const string conninfo =
        "host=" + string(DB_HOST) +
        " port=" + DB_PORT +
        " dbname=" + DB_NAME +
        " user=" + DB_USER +
        " password=" + DB_PASSWORD;

    PGconn* connection = PQconnectdb(conninfo.c_str());
    if (PQstatus(connection) != CONNECTION_OK) {
        cerr << "DB connection error: " << PQerrorMessage(connection) << endl;
        PQfinish(connection);
        return nullptr;
    }

    cout << "DB connected" << endl;
    ensure_telemetry_table(connection);
    return connection;
}

bool ensure_telemetry_table(PGconn* connection) {
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return false;
    }

    const string query =
        "CREATE TABLE IF NOT EXISTS " + string(DB_TABLE) + " ("
        "id BIGSERIAL PRIMARY KEY,"
        "time_ms BIGINT NOT NULL,"
        "latitude DOUBLE PRECISION,"
        "longitude DOUBLE PRECISION,"
        "altitude DOUBLE PRECISION,"
        "accuracy DOUBLE PRECISION,"
        "rsrp INTEGER,"
        "rsrq INTEGER,"
        "rssi INTEGER,"
        "rssnr DOUBLE PRECISION,"
        "pci INTEGER DEFAULT 0,"
        "earfcn INTEGER DEFAULT 0,"
        "network_type TEXT,"
        "operator_name TEXT,"
        "created_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")";

    if (!exec_command(connection, query, "DB schema error")) {
        return false;
    }

    const string add_pci_query =
        "ALTER TABLE " + string(DB_TABLE) +
        " ADD COLUMN IF NOT EXISTS pci INTEGER DEFAULT 0";
    if (!exec_command(connection, add_pci_query, "DB schema update error")) {
        return false;
    }

    const string add_earfcn_query =
        "ALTER TABLE " + string(DB_TABLE) +
        " ADD COLUMN IF NOT EXISTS earfcn INTEGER DEFAULT 0";
    if (!exec_command(connection, add_earfcn_query, "DB schema update error")) {
        return false;
    }

    const string drop_unique_query = "DROP INDEX IF EXISTS telemetry_metrics_unique_sample";
    if (!exec_command(connection, drop_unique_query, "DB index drop error")) {
        return false;
    }

    const string import_state_query =
        "CREATE TABLE IF NOT EXISTS telemetry_import_state ("
        "file_path TEXT PRIMARY KEY,"
        "file_size BIGINT NOT NULL,"
        "line_count INTEGER NOT NULL"
        ")";
    return exec_command(connection, import_state_query, "DB import state error");
}

bool insert_telemetry_row(PGconn* connection, const Telemetry& telemetry) {
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return false;
    }

    const string query =
        "INSERT INTO " + string(DB_TABLE) + " ("
        "time_ms, latitude, longitude, altitude, accuracy, "
        "rsrp, rsrq, rssi, rssnr, pci, earfcn, network_type, operator_name"
        ") VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13)";

    const string p1 = to_string(telemetry.time);
    const string p2 = to_string(telemetry.latitude);
    const string p3 = to_string(telemetry.longitude);
    const string p4 = to_string(telemetry.altitude);
    const string p5 = to_string(telemetry.accuracy);
    const string p6 = to_string(telemetry.rsrp);
    const string p7 = to_string(telemetry.rsrq);
    const string p8 = to_string(telemetry.rssi);
    const string p9 = to_string(telemetry.rssnr);
    const string p10 = to_string(telemetry.pci);
    const string p11 = to_string(telemetry.earfcn);

    const char* params[13] = {
        p1.c_str(), p2.c_str(), p3.c_str(), p4.c_str(), p5.c_str(),
        p6.c_str(), p7.c_str(), p8.c_str(), p9.c_str(), p10.c_str(), p11.c_str(),
        telemetry.network_type.c_str(), telemetry.operator_name.c_str()
    };

    PGresult* result = PQexecParams(
        connection,
        query.c_str(),
        13,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!ok) {
        cerr << "DB insert error: " << PQresultErrorMessage(result) << endl;
    }
    PQclear(result);
    return ok;
}

int import_telemetry_json_file(PGconn* connection, const string& file_path) {
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return 0;
    }

    ifstream file(file_path);
    if (!file.is_open()) {
        return 0;
    }

    file.seekg(0, ios::end);
    const long long file_size = static_cast<long long>(file.tellg());
    file.seekg(0, ios::beg);

    int start_line = 0;
    const string state_query =
        "SELECT file_size, line_count FROM telemetry_import_state WHERE file_path = " +
        sql_text(connection, file_path);
    PGresult* state_result = PQexec(connection, state_query.c_str());
    if (PQresultStatus(state_result) == PGRES_TUPLES_OK && PQntuples(state_result) == 1) {
        const long long old_size = atoll(PQgetvalue(state_result, 0, 0));
        const int old_lines = atoi(PQgetvalue(state_result, 0, 1));
        if (file_size >= old_size) {
            start_line = old_lines;
        }
    }
    PQclear(state_result);

    int imported = 0;
    int line_number = 0;
    string line;
    while (getline(file, line)) {
        line_number++;
        if (line_number <= start_line) {
            continue;
        }

        try {
            Telemetry telemetry = telemetry_from_json(json::parse(line));
            insert_telemetry_row(connection, telemetry);
            imported++;
        } catch (...) {
        }
    }

    const string update_state_query =
        "INSERT INTO telemetry_import_state (file_path, file_size, line_count) VALUES (" +
        sql_text(connection, file_path) + "," + to_string(file_size) + "," + to_string(line_number) + ") "
        "ON CONFLICT (file_path) DO UPDATE SET "
        "file_size = EXCLUDED.file_size, "
        "line_count = EXCLUDED.line_count";
    exec_command(connection, update_state_query, "DB import state update error");

    return imported;
}

void mark_json_line_inserted(PGconn* connection, const string& file_path) {
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return;
    }

    const long long file_size = static_cast<long long>(filesystem::file_size(file_path));
    const string query =
        "INSERT INTO telemetry_import_state (file_path, file_size, line_count) VALUES (" +
        sql_text(connection, file_path) + "," + to_string(file_size) + ",1) "
        "ON CONFLICT (file_path) DO UPDATE SET "
        "file_size = EXCLUDED.file_size, "
        "line_count = telemetry_import_state.line_count + 1";
    exec_command(connection, query, "DB import state update error");
}

TelemetryHistory load_telemetry_history_from_db(PGconn* connection, int max_points) {
    TelemetryHistory history;
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return history;
    }

    const string query =
        "SELECT time_ms, latitude, longitude, altitude, rsrp, rsrq, rssi, rssnr, COALESCE(pci, 0) "
        "FROM ("
        "  SELECT time_ms, latitude, longitude, altitude, rsrp, rsrq, rssi, rssnr, pci "
        "  FROM " + string(DB_TABLE) + " "
        "  ORDER BY time_ms DESC, id DESC " +
        (max_points > 0 ? "  LIMIT " + to_string(max_points) + " " : "") +
        ") last_points "
        "ORDER BY time_ms ASC, latitude ASC, longitude ASC";

    PGresult* result = PQexec(connection, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        cerr << "DB history read error: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return history;
    }

    const int rows = PQntuples(result);
    for (int row = 0; row < rows; ++row) {
        const long long time_ms = atoll(PQgetvalue(result, row, 0));
        const float latitude = static_cast<float>(atof(PQgetvalue(result, row, 1)));
        const float longitude = static_cast<float>(atof(PQgetvalue(result, row, 2)));
        const float altitude = PQgetisnull(result, row, 3) ? 0.0f : static_cast<float>(atof(PQgetvalue(result, row, 3)));
        const float rsrp = PQgetisnull(result, row, 4) ? -200.0f : static_cast<float>(atof(PQgetvalue(result, row, 4)));
        const float rsrq = PQgetisnull(result, row, 5) ? -200.0f : static_cast<float>(atof(PQgetvalue(result, row, 5)));
        const float rssi = PQgetisnull(result, row, 6) ? -200.0f : static_cast<float>(atof(PQgetvalue(result, row, 6)));
        const float rssnr = PQgetisnull(result, row, 7) ? -9999.0f : static_cast<float>(atof(PQgetvalue(result, row, 7)));
        const int pci = PQgetisnull(result, row, 8) ? 0 : atoi(PQgetvalue(result, row, 8));

        history.time_history.push_back(static_cast<float>(time_ms) / 1000.0f);
        history.lat_history.push_back(latitude);
        history.lon_history.push_back(longitude);
        history.altitude_history.push_back(altitude);
        history.rsrp_history.push_back(rsrp);
        history.rsrq_history.push_back(rsrq);
        history.rssi_history.push_back(rssi);
        history.pci_history.push_back(pci);
        history.rsrp_by_pci[pci].push_back(rsrp);
        history.rssi_by_pci[pci].push_back(rssi);
        history.sinr_by_pci[pci].push_back(rssnr);
    }

    PQclear(result);
    return history;
}

vector<int> load_pcis_from_db(PGconn* connection) {
    vector<int> pcis;
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return pcis;
    }

    const string query =
        "SELECT DISTINCT COALESCE(pci, 0) FROM " + string(DB_TABLE) +
        " ORDER BY COALESCE(pci, 0)";

    PGresult* result = PQexec(connection, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        PQclear(result);
        return pcis;
    }

    const int rows = PQntuples(result);
    for (int row = 0; row < rows; ++row) {
        pcis.push_back(atoi(PQgetvalue(result, row, 0)));
    }

    PQclear(result);
    return pcis;
}
