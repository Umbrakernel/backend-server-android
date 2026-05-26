#pragma once

#include <string>

using namespace std;

bool curl_download_file(const string& url, const string& path, const string& user_agent);
