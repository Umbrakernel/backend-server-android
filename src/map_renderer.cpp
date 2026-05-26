#include "map_renderer.h"

#include "app_config.h"
#include "curl_func.h"

#include "imgui.h"

#ifdef HAVE_PNG
#include <png.h>
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace std;

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr float tile_size = 256.0f;
mutex downloading_mutex;
set<MapRenderer::TileId> downloading_tiles;

double longitude_to_tile_x(double longitude, int zoom) {
    return (longitude + 180.0) / 360.0 * pow(2.0, zoom);
}

double latitude_to_tile_y(double latitude, int zoom) {
    const double lat_rad = latitude * pi / 180.0;
    return (1.0 - asinh(tan(lat_rad)) / pi) / 2.0 * pow(2.0, zoom);
}

double tile_x_to_longitude(double tile_x, int zoom) {
    return tile_x / pow(2.0, zoom) * 360.0 - 180.0;
}

double tile_y_to_latitude(double tile_y, int zoom) {
    const double n = pi - 2.0 * pi * tile_y / pow(2.0, zoom);
    return atan(sinh(n)) * 180.0 / pi;
}

string tile_path(const MapRenderer::TileId& tile) {
    return string(TILE_CACHE_DIR) + "/" + to_string(tile.z) + "/" +
        to_string(tile.x) + "/" + to_string(tile.y) + ".png";
}

string tile_url(const MapRenderer::TileId& tile) {
    return "https://tile.openstreetmap.org/" + to_string(tile.z) + "/" +
        to_string(tile.x) + "/" + to_string(tile.y) + ".png";
}

} // namespace

MapRenderer::~MapRenderer() {
    clear_textures();
}

void MapRenderer::draw(const Telemetry& telemetry) {
    if (abs(telemetry.latitude) < 0.000001f && abs(telemetry.longitude) < 0.000001f) {
        ImGui::Text("Map: waiting for valid coordinates");
        return;
    }

    if (!center_initialized_) {
        center_latitude_ = telemetry.latitude;
        center_longitude_ = telemetry.longitude;
        center_initialized_ = true;
    }

    if (ImGui::Button("Center")) {
        center_latitude_ = telemetry.latitude;
        center_longitude_ = telemetry.longitude;
    }
    ImGui::SameLine();
    if (ImGui::Button("-")) {
        if (zoom_ > 1) {
            zoom_--;
        }
    }
    ImGui::SameLine();
    ImGui::Text("Zoom: %d", zoom_);
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        if (zoom_ < 19) {
            zoom_++;
        }
    }

    const int zoom = zoom_;
    double center_tile_x = longitude_to_tile_x(center_longitude_, zoom);
    double center_tile_y = latitude_to_tile_y(center_latitude_, zoom);
    const int center_x = static_cast<int>(floor(center_tile_x));
    const int center_y = static_cast<int>(floor(center_tile_y));
    const int tiles_per_axis = MAP_RADIUS_TILES * 2 + 1;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 map_size(tile_size * tiles_per_axis, tile_size * tiles_per_axis);

    ImGui::InvisibleButton("map_canvas", map_size, ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 drag = ImGui::GetIO().MouseDelta;
        center_tile_x -= drag.x / tile_size;
        center_tile_y -= drag.y / tile_size;
        center_latitude_ = tile_y_to_latitude(center_tile_y, zoom);
        center_longitude_ = tile_x_to_longitude(center_tile_x, zoom);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, ImVec2(origin.x + map_size.x, origin.y + map_size.y), IM_COL32(35, 38, 42, 255));

    for (int dy = -MAP_RADIUS_TILES; dy <= MAP_RADIUS_TILES; ++dy) {
        for (int dx = -MAP_RADIUS_TILES; dx <= MAP_RADIUS_TILES; ++dx) {
            const TileId tile{zoom, center_x + dx, center_y + dy};
            Texture* texture = get_tile_texture(tile);
            const float px = origin.x + static_cast<float>(dx + MAP_RADIUS_TILES) * tile_size;
            const float py = origin.y + static_cast<float>(dy + MAP_RADIUS_TILES) * tile_size;
            const ImVec2 min(px, py);
            const ImVec2 max(px + tile_size, py + tile_size);

            if (texture && texture->id != 0) {
                draw_list->AddImage(static_cast<ImTextureID>(texture->id), min, max);
            } else {
                draw_list->AddRect(min, max, IM_COL32(90, 95, 100, 255));
                const char* text = is_tile_downloading(tile) ? "loading..." : "tile unavailable";
                draw_list->AddText(ImVec2(px + 8.0f, py + 8.0f), IM_COL32(220, 220, 220, 255), text);
            }
        }
    }

    const double marker_tile_x = longitude_to_tile_x(telemetry.longitude, zoom);
    const double marker_tile_y = latitude_to_tile_y(telemetry.latitude, zoom);
    const float marker_x = origin.x + static_cast<float>((marker_tile_x - (center_x - MAP_RADIUS_TILES)) * tile_size);
    const float marker_y = origin.y + static_cast<float>((marker_tile_y - (center_y - MAP_RADIUS_TILES)) * tile_size);
    draw_list->AddCircleFilled(ImVec2(marker_x, marker_y), 6.0f, IM_COL32(230, 54, 69, 255));
    draw_list->AddCircle(ImVec2(marker_x, marker_y), 8.0f, IM_COL32(255, 255, 255, 255), 24, 2.0f);
}

MapRenderer::Texture* MapRenderer::get_tile_texture(const TileId& tile) {
    auto existing = textures_.find(tile);
    if (existing != textures_.end()) {
        return &existing->second;
    }

    const string path = tile_path(tile);
    if (!filesystem::exists(path)) {
        start_tile_download(tile);
        return nullptr;
    }

    Texture texture;
    if (!filesystem::exists(path) || !load_png_texture(path, &texture)) {
        textures_[tile] = texture;
        return &textures_[tile];
    }

    textures_[tile] = texture;
    return &textures_[tile];
}

bool MapRenderer::load_png_texture(const string& path, Texture* texture) {
#ifndef HAVE_PNG
    const string raw_path = path + ".rgba";
    const string meta_path = path + ".meta";
    const string command =
        "python3 -c \"from PIL import Image; import sys; "
        "im=Image.open(sys.argv[1]).convert('RGBA'); "
        "open(sys.argv[2], 'wb').write(im.tobytes()); "
        "open(sys.argv[3], 'w').write(str(im.width) + ' ' + str(im.height))\" "
        "\"" + path + "\" \"" + raw_path + "\" \"" + meta_path + "\"";

    if (system(command.c_str()) != 0) {
        return false;
    }

    ifstream meta_file(meta_path);
    int width = 0;
    int height = 0;
    meta_file >> width >> height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    ifstream raw_file(raw_path, ios::binary);
    vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    raw_file.read(reinterpret_cast<char*>(pixels.data()), static_cast<streamsize>(pixels.size()));
    if (raw_file.gcount() != static_cast<streamsize>(pixels.size())) {
        return false;
    }

    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    texture->width = width;
    texture->height = height;
    return true;
#else
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_read_info(png, info);

    const int width = static_cast<int>(png_get_image_width(png, info));
    const int height = static_cast<int>(png_get_image_height(png, info));
    const png_byte color_type = png_get_color_type(png, info);
    const png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    vector<png_bytep> rows(height);
    for (int y = 0; y < height; ++y) {
        rows[y] = pixels.data() + static_cast<size_t>(y) * width * 4;
    }
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(file);

    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    texture->width = width;
    texture->height = height;
    return true;
#endif
}

bool MapRenderer::is_tile_downloading(const TileId& tile) {
    lock_guard<mutex> lock(downloading_mutex);
    return downloading_tiles.find(tile) != downloading_tiles.end();
}

void MapRenderer::start_tile_download(const TileId& tile) {
    {
        lock_guard<mutex> lock(downloading_mutex);
        if (downloading_tiles.find(tile) != downloading_tiles.end()) {
            return;
        }
        downloading_tiles.insert(tile);
    }

    thread([tile]() {
        curl_download_file(tile_url(tile), tile_path(tile), OSM_USER_AGENT);
        lock_guard<mutex> lock(downloading_mutex);
        downloading_tiles.erase(tile);
    }).detach();
}

void MapRenderer::clear_textures() {
    for (auto& [tile, texture] : textures_) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }
    }
    textures_.clear();
}
