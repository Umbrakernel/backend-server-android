#pragma once

#include "app_config.h"
#include "telemetry.h"

#include <GL/glew.h>

#include <map>
#include <string>

using namespace std;

class MapRenderer {
public:
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

    void draw(const Telemetry& telemetry);

private:
    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    Texture* get_tile_texture(const TileId& tile);
    bool load_png_texture(const string& path, Texture* texture);
    bool is_tile_downloading(const TileId& tile);
    void start_tile_download(const TileId& tile);
    void clear_textures();

    int zoom_ = MAP_ZOOM;
    bool center_initialized_ = false;
    double center_latitude_ = 0.0;
    double center_longitude_ = 0.0;
    map<TileId, Texture> textures_;
};
