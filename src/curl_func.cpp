#include "curl_func.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace std;

bool curl_download_file(const string& url, const string& path, const string& user_agent) {
    filesystem::create_directories(filesystem::path(path).parent_path());
    const string temp_path = path + ".tmp";
    filesystem::remove(temp_path);

    const string command =
        "curl -L --fail --silent --show-error --connect-timeout 5 --max-time 15 "
        "-A \"" + user_agent + "\" "
        "-o \"" + temp_path + "\" "
        "\"" + url + "\"";

    const int result = system(command.c_str());
    if (result != 0) {
        cerr << "Tile download failed: " << url << " code=" << result << endl;
        filesystem::remove(temp_path);
        return false;
    }

    filesystem::rename(temp_path, path);
    return true;
}
