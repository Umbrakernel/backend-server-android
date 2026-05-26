#include "imgui_ui.h"
#include "socket_server.h"
#include "telemetry.h"

#include <thread>

using namespace std;

int main() {
    TelemetryState telemetry_state;

    thread server(run_server, &telemetry_state);
    server.detach();

    run_gui(&telemetry_state);
    return 0;
}
