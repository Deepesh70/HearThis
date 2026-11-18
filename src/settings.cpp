#include "hearthis/settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <sstream>

namespace hearthis {

std::string get_load_config_file_path() {
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
        std::string base(appdata);
        free(appdata);

        std::string new_path = base + "\\HearThis\\config.txt";
        DWORD attr = GetFileAttributesA(new_path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            return new_path;
        }

        std::string legacy_path = base + "\\SongDetector\\native_config.txt";
        DWORD legacy_attr = GetFileAttributesA(legacy_path.c_str());
        if (legacy_attr != INVALID_FILE_ATTRIBUTES) {
            return legacy_path;
        }

        std::string new_dir = base + "\\HearThis";
        CreateDirectoryA(new_dir.c_str(), nullptr);
        return new_path;
    }
    return "config.txt";
}

std::string get_save_config_file_path() {
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
        std::string dir = std::string(appdata) + "\\HearThis";
        CreateDirectoryA(dir.c_str(), nullptr);
        std::string path = dir + "\\config.txt";
        free(appdata);
        return path;
    }
    return "config.txt";
}

AppSettings load_settings(const std::string& path) {
    AppSettings s;
    std::string config_path = path.empty() ? get_load_config_file_path() : path;
    std::ifstream file(config_path);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "provider") {
                    try { s.provider_index = std::stoi(v); } catch (...) {}
                } else if (k == "duration") {
                    try { s.duration_seconds = std::stoi(v); } catch (...) {}
                } else if (k == "audio_source" || k == "source") {
                    try {
                        s.audio_source = (std::stoi(v) == 1) ? AudioSource::Microphone : AudioSource::Speakers;
                    } catch (...) {}
                } else if (k == "theme") {
                    s.dark_theme = (v != "light");
                } else if (k == "acr_host") {
                    s.acr_host = v;
                } else if (k == "acr_key") {
                    s.acr_key = v;
                } else if (k == "acr_secret") {
                    s.acr_secret = v;
                } else if (k == "audd_token") {
                    s.audd_token = v;
                }
            }
        }
        file.close();
    }
    return s;
}

void save_settings(const AppSettings& s, const std::string& path) {
    std::string config_path = path.empty() ? get_save_config_file_path() : path;
    std::ofstream file(config_path);
    if (file.is_open()) {
        file << "provider=" << s.provider_index << "\n";
        file << "duration=" << s.duration_seconds << "\n";
        file << "audio_source=" << static_cast<int>(s.audio_source) << "\n";
        file << "theme=" << (s.dark_theme ? "dark" : "light") << "\n";
        file << "acr_host=" << s.acr_host << "\n";
        file << "acr_key=" << s.acr_key << "\n";
        file << "acr_secret=" << s.acr_secret << "\n";
        file << "audd_token=" << s.audd_token << "\n";
        file.close();
    }
}

} // namespace hearthis
