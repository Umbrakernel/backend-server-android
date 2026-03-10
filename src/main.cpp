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

struct location {
    float latitude = 0;
    float longitude = 0;
    float altitude = 0;
    long long time = 0;

    vector<float> altitude_history;
    vector<float> time_history;

    mutex mtx;
};

void run_server(location *loc) {

    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);

    socket.bind("tcp://*:5555");

    while (true) {

        zmq::message_t request;
        socket.recv(request, zmq::recv_flags::none);

        string raw(static_cast<char*>(request.data()), request.size());

        try {

            auto j = json::parse(raw);

            float lat = j["latitude"];
            float lon = j["longitude"];
            float alt = j["altitude"];
            long long time = j["time"];

            ofstream file("location_data.json", ios::app);
            file << j.dump() << endl;

            {
                lock_guard<mutex> lock(loc->mtx);

                loc->latitude = lat;
                loc->longitude = lon;
                loc->altitude = alt;
                loc->time = time;

                loc->altitude_history.push_back(alt);
                loc->time_history.push_back((float)time);

                if (loc->altitude_history.size() > 200) {
                    loc->altitude_history.erase(loc->altitude_history.begin());
                    loc->time_history.erase(loc->time_history.begin());
                }
            }

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);

        } catch (...) {
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
        }
    }
}

void run_gui(location *loc) {

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    SDL_Window* window = SDL_CreateWindow(
        "Backend Server",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1024,
        768,
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

            if (event.type == SDL_QUIT)
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        float lat, lon, alt;
        long long time;

        vector<float> alt_hist;
        vector<float> time_hist;

        {
            lock_guard<mutex> lock(loc->mtx);

            lat = loc->latitude;
            lon = loc->longitude;
            alt = loc->altitude;
            time = loc->time;

            alt_hist = loc->altitude_history;
            time_hist = loc->time_history;
        }

        ImGui::Begin("Location Status");

        ImGui::Text("Last Update Time: %lld", time);
        ImGui::Separator();

        ImGui::Text("Latitude:  %.6f", lat);
        ImGui::Text("Longitude: %.6f", lon);
        ImGui::Text("Altitude:  %.2f m", alt);

        ImGui::Separator();

        if (ImPlot::BeginPlot("Altitude vs Time")) {

            if (!alt_hist.empty()) {
                ImPlot::PlotLine(
                    "Altitude",
                    time_hist.data(),
                    alt_hist.data(),
                    alt_hist.size()
                );
            }

            ImPlot::EndPlot();
        }

        ImGui::End();

        ImGui::Render();

        glClearColor(0.1f,0.1f,0.1f,1);
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

    static location loc;

    thread server(run_server, &loc);
    thread gui(run_gui, &loc);

    gui.join();

    return 0;
}