#include "imgui_ui.h"

#include "app_config.h"
#include "database.h"
#include "map_renderer.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#include <string>
#include <vector>
#include <memory>

using namespace std;

namespace {

enum class Screen {
    Metrics,
    Graphs,
    Map
};

void plot_metric_by_pci(
    const char* title,
    const char* y_axis,
    const vector<float>& time_history,
    const map<int, vector<float>>& values_by_pci
) {
    if (ImPlot::BeginPlot(title)) {
        ImPlot::SetupAxes("Time (s)", y_axis);
        ImPlot::SetupLegend(ImPlotLocation_East);

        for (const auto& [pci, values] : values_by_pci) {
            const string label = "PCI " + to_string(pci);
            const int count = static_cast<int>(min(time_history.size(), values.size()));
            if (count > 0) {
                ImPlot::PlotLine(label.c_str(), time_history.data(), values.data(), count);
            }
        }

        ImPlot::EndPlot();
    }
}

} // namespace

void run_gui(TelemetryState* telemetry_state) {
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
    glewInit();

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    MapRenderer map_renderer;
    Screen current_screen = Screen::Metrics;
    unique_ptr<PGconn, decltype(&PQfinish)> db(connect_db(), &PQfinish);

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

        const TelemetrySnapshot snapshot = telemetry_state->snapshot();
        TelemetryHistory history;
        if (db) {
            history = load_telemetry_history_from_db(db.get(), MAX_HISTORY_POINTS);
        }

        ImGui::Begin("Telemetry");

        if (ImGui::Button("Metrics")) {
            current_screen = Screen::Metrics;
        }
        ImGui::SameLine();
        if (ImGui::Button("Graphs")) {
            current_screen = Screen::Graphs;
        }
        ImGui::SameLine();
        if (ImGui::Button("Map")) {
            current_screen = Screen::Map;
        }

        ImGui::Separator();

        if (current_screen == Screen::Metrics) {
            ImGui::Text("Last update (ms): %lld", snapshot.current.time);
            ImGui::Separator();

            ImGui::Text("Latitude:  %.6f", snapshot.current.latitude);
            ImGui::Text("Longitude: %.6f", snapshot.current.longitude);
            ImGui::Text("Altitude:  %.2f m", snapshot.current.altitude);
            ImGui::Text("Accuracy:  %.2f m", snapshot.current.accuracy);

            ImGui::Separator();

            if (snapshot.current.rsrp > -200) ImGui::Text("RSRP: %d dBm", snapshot.current.rsrp);
            else ImGui::Text("RSRP: N/A");

            if (snapshot.current.rsrq > -200) ImGui::Text("RSRQ: %d dB", snapshot.current.rsrq);
            else ImGui::Text("RSRQ: N/A");

            if (snapshot.current.rssi > -200) ImGui::Text("RSSI: %d dBm", snapshot.current.rssi);
            else ImGui::Text("RSSI: N/A");

            if (snapshot.current.rssnr > -9990.0f) ImGui::Text("RSSNR: %.1f dB", snapshot.current.rssnr);
            else ImGui::Text("RSSNR: N/A");

            ImGui::Text("Network type: %s", snapshot.current.network_type.c_str());
            ImGui::Text("Operator: %s", snapshot.current.operator_name.c_str());
        }

        if (current_screen == Screen::Graphs) {
            plot_metric_by_pci("RSRP vs Time", "RSRP (dBm)", history.time_history, history.rsrp_by_pci);
            plot_metric_by_pci("RSSI vs Time", "RSSI (dBm)", history.time_history, history.rssi_by_pci);
            plot_metric_by_pci("SINR vs Time", "SINR (dB)", history.time_history, history.sinr_by_pci);

            if (ImPlot::BeginPlot("Longitude vs Latitude")) {
                if (!history.lat_history.empty() && history.lat_history.size() == history.lon_history.size()) {
                    ImPlot::SetupAxes("Latitude", "Longitude");
                    ImPlot::PlotScatter(
                        "Trajectory",
                        history.lat_history.data(),
                        history.lon_history.data(),
                        static_cast<int>(history.lat_history.size())
                    );
                }
                ImPlot::EndPlot();
            }
        }

        if (current_screen == Screen::Map) {
            ImGui::Text("OpenStreetMap");
            map_renderer.draw(snapshot.current);
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
