#include "telemetry_json.h"

using namespace std;
using json = nlohmann::json;

Telemetry telemetry_from_json(const json& j) {
    Telemetry telemetry;

    telemetry.latitude = j.value("latitude", 0.0f);
    telemetry.longitude = j.value("longitude", 0.0f);
    telemetry.altitude = j.value("altitude", 0.0f);
    telemetry.accuracy = j.value("accuracy", 0.0f);
    telemetry.time = j.value("time", 0LL);

    telemetry.rsrp = j.value("rsrp", -200);
    telemetry.rsrq = j.value("rsrq", -200);
    telemetry.rssi = j.value("rssi", -200);
    telemetry.rssnr = j.value("rssnr", -9999.0f);
    telemetry.pci = j.value("pci", 0);
    telemetry.earfcn = j.value("earfcn", 0);

    telemetry.network_type = j.value("networkType", string("UNKNOWN"));
    telemetry.operator_name = j.value("operatorName", string("UNKNOWN"));

    return telemetry;
}
