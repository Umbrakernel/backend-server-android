#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <fstream>
#include <string>
#include <vector>

#include <zmq.hpp>
#include <nlohmann/json.hpp>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;
using json = nlohmann::json;

struct telemetry {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude = 0.0f;
    float accuracy = 0.0f;
    long long time = 0;

    int rsrp = -200;
    int rsrq = -200;
    int rssi = -200;
    float rssnr = -9999.0f;

    string network_type = "UNKNOWN";
    string operator_name = "UNKNOWN";

    mutex mtx;
};

void load_location_history_from_json_file(
    const string& file_path,
    vector<float>* time_history,
    vector<float>* lat_history,
    vector<float>* lon_history,
    size_t max_points = 500
) {
    ifstream file(file_path);
    if (!file.is_open()) return;

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;

        try {
            auto j = json::parse(line);
            if (!j.contains("time") || !j.contains("latitude") || !j.contains("longitude")) continue;
            if (j["latitude"].is_null() || j["longitude"].is_null()) continue;

            long long t = j.value("time", 0LL);
            float lat = j.value("latitude", 0.0f);
            float lon = j.value("longitude", 0.0f);

            time_history->push_back(static_cast<float>(t) / 1000.0f);
            lat_history->push_back(lat);
            lon_history->push_back(lon);
        } catch (...) {
        }
    }

    if (time_history->size() > max_points) {
        size_t extra = time_history->size() - max_points;
        time_history->erase(time_history->begin(), time_history->begin() + static_cast<long long>(extra));
        lat_history->erase(lat_history->begin(), lat_history->begin() + static_cast<long long>(extra));
        lon_history->erase(lon_history->begin(), lon_history->begin() + static_cast<long long>(extra));
    }
}

void load_rsrp_history_from_json_file(
    const string& file_path,
    vector<float>* time_history,
    vector<float>* rsrp_history,
    size_t max_points = 500
) {
    ifstream file(file_path);
    if (!file.is_open()) {
        return;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            auto j = json::parse(line);
            if (!j.contains("time") || !j.contains("rsrp") || j["rsrp"].is_null()) {
                continue;
            }

            long long time = j.value("time", 0LL);
            int rsrp = j.value("rsrp", -200);
            if (rsrp <= -200) {
                continue;
            }

            time_history->push_back(static_cast<float>(time) / 1000.0f);
            rsrp_history->push_back(static_cast<float>(rsrp));
        } catch (...) {
        }
    }

    if (rsrp_history->size() > max_points) {
        const size_t extra = rsrp_history->size() - max_points;
        rsrp_history->erase(rsrp_history->begin(), rsrp_history->begin() + static_cast<long long>(extra));
        time_history->erase(time_history->begin(), time_history->begin() + static_cast<long long>(extra));
    }
}

void run_server(telemetry *tm) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.bind("tcp://*:5555");

    while (true) {
        zmq::message_t request;
        socket.recv(request, zmq::recv_flags::none);

        string raw(static_cast<char*>(request.data()), request.size());

        try {
            auto j = json::parse(raw);

            float latitude = j.value("latitude", 0.0f);
            float longitude = j.value("longitude", 0.0f);
            float altitude = j.value("altitude", 0.0f);
            float accuracy = j.value("accuracy", 0.0f);
            long long time = j.value("time", 0LL);

            int rsrp = -200;
            if (j.contains("rsrp") && !j["rsrp"].is_null()) {
                rsrp = j["rsrp"].get<int>();
            }

            int rsrq = -200;
            if (j.contains("rsrq") && !j["rsrq"].is_null()) {
                rsrq = j["rsrq"].get<int>();
            }

            int rssi = -200;
            if (j.contains("rssi") && !j["rssi"].is_null()) {
                rssi = j["rssi"].get<int>();
            }

            float rssnr = -9999.0f;
            if (j.contains("rssnr") && !j["rssnr"].is_null()) {
                rssnr = j["rssnr"].get<float>();
            }


            string network_type = j.value("networkType", string("UNKNOWN"));
            string operator_name = j.value("operatorName", string("UNKNOWN"));
            ofstream file("location_data.json", ios::app);
            file << j.dump() << endl;

            {
                lock_guard<mutex> lock(tm->mtx);

                tm->latitude = latitude;
                tm->longitude = longitude;
                tm->altitude = altitude;
                tm->accuracy = accuracy;
                tm->time = time;
                tm->rsrp = rsrp;
                tm->rsrq = rsrq;
                tm->rssi = rssi;
                tm->rssnr = rssnr;
                tm->network_type = network_type;
                tm->operator_name = operator_name;
            }

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}

void run_gui(telemetry *tm) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    SDL_Window* window = SDL_CreateWindow(
        "Backend Server",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1120,
        800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        telemetry local;
        {
            lock_guard<mutex> lock(tm->mtx);
            local.latitude = tm->latitude;
            local.longitude = tm->longitude;
            local.altitude = tm->altitude;
            local.accuracy = tm->accuracy;
            local.time = tm->time;
            local.rsrp = tm->rsrp;
            local.network_type = tm->network_type;
            local.operator_name = tm->operator_name;
        }

        vector<float> time_history;
        vector<float> rsrp_history;
        load_rsrp_history_from_json_file("build/location_data.json", &time_history, &rsrp_history);
        vector<float> loc_time_history;
        vector<float> lat_history;
        vector<float> lon_history;
        load_location_history_from_json_file("build/location_data.json", &loc_time_history, &lat_history, &lon_history);


        ImGui::Begin("Telemetry");

        ImGui::Text("Last update (ms): %lld", local.time);
        ImGui::Separator();

        ImGui::Text("Latitude:  %.6f", local.latitude);
        ImGui::Text("Longitude: %.6f", local.longitude);
        ImGui::Text("Altitude:  %.2f m", local.altitude);
        ImGui::Text("Accuracy:  %.2f m", local.accuracy);

        ImGui::Separator();

        if (local.rsrp > -200) ImGui::Text("RSRP: %d dBm", local.rsrp);
        else ImGui::Text("RSRP: N/A");
    
        if (local.rsrq > -200) ImGui::Text("RSRQ: %d dB", local.rsrq);
        else ImGui::Text("RSRQ: N/A");

        if (local.rssi > -200) ImGui::Text("RSSI: %d dBm", local.rssi);
        else ImGui::Text("RSSI: N/A");

        if (local.rssnr > -9990.0f) ImGui::Text("RSSNR: %.1f dB", local.rssnr);
        else ImGui::Text("RSSNR: N/A");

        ImGui::Text("Network type: %s", local.network_type.c_str());
        ImGui::Text("Operator: %s", local.operator_name.c_str());

        if (ImPlot::BeginPlot("RSRP vs Time")) {
            if (!rsrp_history.empty()) {
                ImPlot::SetupAxes("Time (s)", "RSRP (dBm)");
                ImPlot::PlotLine(
                    "RSRP",
                    time_history.data(),
                    rsrp_history.data(),
                    static_cast<int>(rsrp_history.size())
                );
            }
            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("Longitude vs Latitude")) {
            if (!lat_history.empty() && lat_history.size() == lon_history.size()) {
                ImPlot::SetupAxes("Latitude", "Longitude");
                ImPlot::PlotScatter(
                    "Trajectory",
                    lat_history.data(),
                    lon_history.data(),
                    (int)lat_history.size()
                );
            }
            ImPlot::EndPlot();
        }




        ImGui::End();

        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
}

int main() {
    static telemetry tm;

    thread server(run_server, &tm);
    thread gui(run_gui, &tm);

    gui.join();
    return 0;
}
