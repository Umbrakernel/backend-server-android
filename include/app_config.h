#pragma once

#define SERVER_ENDPOINT "tcp://*:5555"
#define TELEMETRY_FILE "build/location_data.json"

#define DB_HOST "localhost"
#define DB_PORT "5433"
#define DB_NAME "telemetry_db"
#define DB_USER "postgres"
#define DB_PASSWORD "1234"
#define DB_TABLE "telemetry_metrics"

#define GRAPH_HISTORY_POINTS 40000
#define HEATMAP_HISTORY_POINTS 40000

#define MAP_ZOOM 15
#define MAP_RADIUS_TILES 1
#define TILE_CACHE_DIR "tile_cache"
#define OSM_USER_AGENT "backend-server-android/1.0"
