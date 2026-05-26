#include "database.h"

#include "app_config.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace std;

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
    return exec_command(connection, add_pci_query, "DB schema update error");
}

bool insert_telemetry_row(PGconn* connection, const Telemetry& telemetry) {
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return false;
    }

    const string query =
        "INSERT INTO " + string(DB_TABLE) + " ("
        "time_ms, latitude, longitude, altitude, accuracy, "
        "rsrp, rsrq, rssi, rssnr, pci, network_type, operator_name"
        ") VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12)";

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

    const char* params[12] = {
        p1.c_str(), p2.c_str(), p3.c_str(), p4.c_str(), p5.c_str(),
        p6.c_str(), p7.c_str(), p8.c_str(), p9.c_str(), p10.c_str(),
        telemetry.network_type.c_str(), telemetry.operator_name.c_str()
    };

    PGresult* result = PQexecParams(
        connection,
        query.c_str(),
        12,
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

TelemetryHistory load_telemetry_history_from_db(PGconn* connection, int max_points) {
    TelemetryHistory history;
    if (!connection || PQstatus(connection) != CONNECTION_OK) {
        return history;
    }

    const string query =
        "SELECT time_ms, latitude, longitude, rsrp, rssi, rssnr, COALESCE(pci, 0) "
        "FROM ("
        "  SELECT time_ms, latitude, longitude, rsrp, rssi, rssnr, pci "
        "  FROM " + string(DB_TABLE) + " "
        "  ORDER BY id DESC "
        "  LIMIT " + to_string(max_points) + " "
        ") last_points "
        "ORDER BY time_ms ASC";

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
        const float rsrp = PQgetisnull(result, row, 3) ? -200.0f : static_cast<float>(atof(PQgetvalue(result, row, 3)));
        const float rssi = PQgetisnull(result, row, 4) ? -200.0f : static_cast<float>(atof(PQgetvalue(result, row, 4)));
        const float rssnr = PQgetisnull(result, row, 5) ? -9999.0f : static_cast<float>(atof(PQgetvalue(result, row, 5)));
        const int pci = PQgetisnull(result, row, 6) ? 0 : atoi(PQgetvalue(result, row, 6));

        history.time_history.push_back(static_cast<float>(time_ms) / 1000.0f);
        history.lat_history.push_back(latitude);
        history.lon_history.push_back(longitude);
        history.rsrp_by_pci[pci].push_back(rsrp);
        history.rssi_by_pci[pci].push_back(rssi);
        history.sinr_by_pci[pci].push_back(rssnr);
    }

    PQclear(result);
    return history;
}
