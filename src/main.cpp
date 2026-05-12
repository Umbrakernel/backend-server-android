#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <fstream>
#include <string>
#include <map>
#include <vector>

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;
using json = nlohmann::json;

#define DB_HOST "localhost"
#define DB_PORT "5433"
#define DB_NAME "telemetry_db"
#define DB_USER "postgres"
#define DB_PASSWORD "1234"
#define DB_TABLE "telemetry_metrics"

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

map<int, vector<float>> rsrp_by_pci;
map<int, vector<float>> rssi_by_pci;
map<int, vector<float>> sinr_by_pci;
vector<float> multi_time_history;

PGconn* connect_db() {
    const char* conninfo =
        "host=" DB_HOST
        " port=" DB_PORT
        " dbname=" DB_NAME
        " user=" DB_USER
        " password=" DB_PASSWORD;

    PGconn* con = PQconnectdb(conninfo);
    if (PQstatus(con) != CONNECTION_OK) {
        cerr << "DB connection error: " << PQerrorMessage(con) << endl;
        PQfinish(con);
        return nullptr;
    }

    cout << "DB connected" << endl;
    return con;
}

void insert_telemetry_row(
    PGconn* con,
    float latitude,
    float longitude,
    float altitude,
    float accuracy,
    long long time,
    int rsrp,
    int rsrq,
    int rssi,
    float rssnr,
    const string& network_type,
    const string& operator_name
) {
    if (!con) return;

    const string query =
        "INSERT INTO " + string(DB_TABLE) + " ("
        "time_ms, latitude, longitude, altitude, accuracy, "
        "rsrp, rsrq, rssi, rssnr, network_type, operator_name"
        ") VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)";

    const string p1 = to_string(time);
    const string p2 = to_string(latitude);
    const string p3 = to_string(longitude);
    const string p4 = to_string(altitude);
    const string p5 = to_string(accuracy);
    const string p6 = to_string(rsrp);
    const string p7 = to_string(rsrq);
    const string p8 = to_string(rssi);
    const string p9 = to_string(rssnr);
    const string p10 = network_type;
    const string p11 = operator_name;

    const char* params[11] = {
        p1.c_str(), p2.c_str(), p3.c_str(), p4.c_str(), p5.c_str(),
        p6.c_str(), p7.c_str(), p8.c_str(), p9.c_str(), p10.c_str(), p11.c_str()
    };

    PGresult* res = PQexecParams(
        con,
        query.c_str(),
        11,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        cerr << "DB insert error: " << PQresultErrorMessage(res) << endl;
    }
    PQclear(res);
}

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
    PGconn* db = connect_db();

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

            int rsrp  = j.value("rsrp", -200);
            int rsrq  = j.value("rsrq", -200);
            int rssi  = j.value("rssi", -200);
            float rssnr = j.value("rssnr", -9999.0f);

            if (rssnr > 1000.0f || rssnr < -1000.0f)
                rssnr = -9999.0f;

            string network_type = j.value("networkType", string("UNKNOWN"));
            string operator_name = j.value("operatorName", string("UNKNOWN"));
            ofstream file("location_data.json", ios::app);
            file << j.dump() << endl;

            insert_telemetry_row(
                db,
                latitude,
                longitude,
                altitude,
                accuracy,
                time,
                rsrp,
                rsrq,
                rssi,
                rssnr,
                network_type,
                operator_name
            );

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

            float t = static_cast<float>(time) / 1000.0f;

            multi_time_history.push_back(t);

            int pci = j.value("pci", 0);

            rsrp_by_pci[pci].push_back((float)rsrp);
            rssi_by_pci[pci].push_back((float)rssi);
            sinr_by_pci[pci].push_back((float)rssnr);

            if (multi_time_history.size() > 500)
                multi_time_history.erase(multi_time_history.begin());

            for (auto& [id, arr] : rsrp_by_pci)
                if (arr.size() > 500) arr.erase(arr.begin());

            for (auto& [id, arr] : rssi_by_pci)
                if (arr.size() > 500) arr.erase(arr.begin());

            for (auto& [id, arr] : sinr_by_pci)
                if (arr.size() > 500) arr.erase(arr.begin());

            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        } catch (const exception& e) { 
            cout << "SERVER ERROR: " << e.what() << endl; 
            socket.send(zmq::str_buffer("Error"), zmq::send_flags::none); 
        }
    }

    if (db) {
        PQfinish(db);
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
            ImPlot::SetupAxes("Time (s)", "RSRP (dBm)");
            ImPlot::SetupLegend(ImPlotLocation_East);

            for (auto& [pci, values] : rsrp_by_pci) {
                string label = "PCI " + to_string(pci);

                ImPlot::PlotLine(
                    label.c_str(),
                    multi_time_history.data(),
                    values.data(),
                    (int)values.size()
                );
            }

            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("RSSI vs Time")) {
            ImPlot::SetupAxes("Time (s)", "RSSI (dBm)");
            ImPlot::SetupLegend(ImPlotLocation_East);

            for (auto& [pci, values] : rssi_by_pci) {
                string label = "PCI " + to_string(pci);

                ImPlot::PlotLine(
                    label.c_str(),
                    multi_time_history.data(),
                    values.data(),
                    (int)values.size()
                );
            }

            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("SINR vs Time")) {
            ImPlot::SetupAxes("Time (s)", "SINR (dB)");
            ImPlot::SetupLegend(ImPlotLocation_East);

            for (auto& [pci, values] : sinr_by_pci) {
                string label = "PCI " + to_string(pci);

                ImPlot::PlotLine(
                    label.c_str(),
                    multi_time_history.data(),
                    values.data(),
                    (int)values.size()
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
