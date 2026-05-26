#include "socket_server.h"

#include "app_config.h"
#include "database.h"
#include "json_history.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

using namespace std;

using json = nlohmann::json;

namespace {

template <typename T>
T json_value_or(const json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }

    try {
        return it->get<T>();
    } catch (...) {
        return fallback;
    }
}

int pci_from_nested_cell_info(const json& j) {
    try {
        const auto& lte = j.at("CellInfoLte").at("CellIdentityLte").at("PCI");
        if (!lte.is_null()) {
            return lte.get<int>();
        }
    } catch (...) {
    }

    try {
        const auto& nr = j.at("CellInfoNr").at("CellIdentityNr").at("PCI");
        if (!nr.is_null()) {
            return nr.get<int>();
        }
    } catch (...) {
    }

    return 0;
}

Telemetry telemetry_from_json(const json& j) {
    Telemetry telemetry;
    telemetry.latitude = json_value_or(j, "latitude", 0.0f);
    telemetry.longitude = json_value_or(j, "longitude", 0.0f);
    telemetry.altitude = json_value_or(j, "altitude", 0.0f);
    telemetry.accuracy = json_value_or(j, "accuracy", 0.0f);
    telemetry.time = json_value_or(j, "time", 0LL);

    telemetry.rsrp = json_value_or(j, "rsrp", -200);
    telemetry.rsrq = json_value_or(j, "rsrq", -200);
    telemetry.rssi = json_value_or(j, "rssi", -200);
    telemetry.rssnr = json_value_or(j, "rssnr", -9999.0f);
    telemetry.pci = json_value_or(j, "pci", pci_from_nested_cell_info(j));

    if (telemetry.rssnr > 1000.0f || telemetry.rssnr < -1000.0f) {
        telemetry.rssnr = -9999.0f;
    }

    telemetry.network_type = json_value_or(j, "networkType", string("UNKNOWN"));
    telemetry.operator_name = json_value_or(j, "operatorName", string("UNKNOWN"));
    return telemetry;
}

} // namespace

void run_server(TelemetryState* telemetry_state) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.bind(SERVER_ENDPOINT);

    unique_ptr<PGconn, decltype(&PQfinish)> db(connect_db(), &PQfinish);

    while (true) {
        zmq::message_t request;
        const auto received = socket.recv(request, zmq::recv_flags::none);
        if (!received) {
            continue;
        }

        const string raw(static_cast<char*>(request.data()), request.size());

        try {
            const auto j = json::parse(raw);
            const Telemetry telemetry = telemetry_from_json(j);

            append_json_line(TELEMETRY_FILE, j.dump());
            if (db) {
                insert_telemetry_row(db.get(), telemetry);
            }
            telemetry_state->update(telemetry);

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (const exception& e) {
            cout << "SERVER ERROR: " << e.what() << endl;
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}
