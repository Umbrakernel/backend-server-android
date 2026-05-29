#pragma once

#include "telemetry.h"

#include <nlohmann/json.hpp>

Telemetry telemetry_from_json(const nlohmann::json& j);
