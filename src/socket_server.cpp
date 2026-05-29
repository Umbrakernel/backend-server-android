#include "socket_server.h"

#include "app_config.h"
#include "database.h"
#include "json_history.h"
#include "telemetry_json.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

using namespace std;

using json = nlohmann::json;

namespace {

string project_file_path(const string& relative_path) {
    filesystem::path exe_path = filesystem::read_symlink("/proc/self/exe");
    filesystem::path project_root = exe_path.parent_path().parent_path();
    return (project_root / relative_path).string();
}

} // namespace

void run_server(TelemetryState* telemetry_state) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    try {
        socket.bind(SERVER_ENDPOINT);
    } catch (const zmq::error_t& e) {
        cerr << "Socket bind error: " << e.what() << endl;
        cerr << "Port 5555 is already busy. Stop the old server process and run again." << endl;
        return;
    }

    unique_ptr<PGconn, decltype(&PQfinish)> db(connect_db(), &PQfinish);
    const string telemetry_file = project_file_path(TELEMETRY_FILE);
    if (db) {
        int imported = import_telemetry_json_file(db.get(), telemetry_file);
        cout << "Imported rows from " << telemetry_file << ": " << imported << endl;
    }

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

            append_json_line(telemetry_file, j.dump());
            if (db) {
                if (insert_telemetry_row(db.get(), telemetry)) {
                    mark_json_line_inserted(db.get(), telemetry_file);
                }
            }
            telemetry_state->update(telemetry);

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (const exception& e) {
            cout << "SERVER ERROR: " << e.what() << endl;
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}
