#pragma once

#include "app_config.h"
#include "database.h"
#include "telemetry.h"

#include <GL/glew.h>

#include <map>
#include <string>

using namespace std;

class MapRenderer {
public:
    enum class HeatmapCriterion {
        RSRP,
        RSRQ,
        RSSI,
        Altitude
    };

    struct TileId {
        int z = 0;
        int x = 0;
        int y = 0;

        bool operator<(const TileId& other) const {
            if (z != other.z) return z < other.z;
            if (x != other.x) return x < other.x;
            return y < other.y;
        }
    };

    ~MapRenderer();

    void draw(const Telemetry& telemetry, const TelemetryHistory& history, HeatmapCriterion criterion, int pci, float radius_meters);

private:
    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    Texture* get_tile_texture(const TileId& tile);
    Texture* get_heatmap_texture(const TileId& tile, const TelemetryHistory& history, HeatmapCriterion criterion, int pci, float radius_meters);
    bool load_png_texture(const string& path, Texture* texture);
    bool is_tile_downloading(const TileId& tile);
    void start_tile_download(const TileId& tile);
    void start_heatmap_build(const TileId& tile, const TelemetryHistory& history, HeatmapCriterion criterion, int pci, float radius_meters);
    void clear_textures();

    int zoom_ = MAP_ZOOM;
    bool center_initialized_ = false;
    double center_latitude_ = 0.0;
    double center_longitude_ = 0.0;
    map<TileId, Texture> textures_;
    map<string, Texture> heatmap_textures_;
};
