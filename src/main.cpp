#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <fstream>
#include <string>

#include <zmq.hpp>
#include <nlohmann/json.hpp>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"

using namespace std;
using json = nlohmann::json;

struct location {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude = 0.0f;
    long long time = 0;
    mutex mtx;
};

void run_server(location *loc) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.bind("tcp://*:5555");


    while (true) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        
        if (res) {
            string raw_data(static_cast<char*>(request.data()), request.size());
            try {
                auto j = json::parse(raw_data);

                ofstream file("location_data.json", std::ios::app);
                file << j.dump() << std::endl;

                {
                    lock_guard<std::mutex> lock(loc->mtx);
                    loc->latitude = j["latitude"];
                    loc->longitude = j["longitude"];
                    loc->altitude = j["altitude"];
                    loc->time = j["time"];
                }

                socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
            } catch (...) {
                socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
            }
        }
    }
}

void run_gui(location *loc) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow("Backend", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        {
            ImGui::Begin("Location Status");
            
            float lat, lon, alt;
            long long timestamp;
            {
                lock_guard<std::mutex> lock(loc->mtx);
                lat = loc->latitude;
                lon = loc->longitude;
                alt = loc->altitude;
                timestamp = loc->time;
            }

            ImGui::Text("Last Update Time: %lld", timestamp);
            ImGui::Separator();
            ImGui::Text("Latitude:  %.6f", lat);
            ImGui::Text("Longitude: %.6f", lon);
            ImGui::Text("Altitude:  %.2f m", alt);
            ImGui::End();
        }

        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    static location locationInfo;

    thread gui_thread(run_gui, &locationInfo);
    thread server_thread(run_server, &locationInfo);

    gui_thread.join();
    exit(0); 
    return 0;
}