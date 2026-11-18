#pragma once

#include "hearthis/wasapi_capture.h"
#include <string>

namespace hearthis {

struct AppSettings {
    int provider_index = 0; // 0 = ACRCloud, 1 = AudD
    int duration_seconds = 10; // 5 or 10
    AudioSource audio_source = AudioSource::Speakers;
    bool dark_theme = true; // true = Dark, false = Light
    std::string acr_host = "identify-eu-west-1.acrcloud.com";
    std::string acr_key = "";
    std::string acr_secret = "";
    std::string audd_token = "";
};

// Returns the full path to config.txt (including %APPDATA% migration checks)
std::string get_load_config_file_path();
std::string get_save_config_file_path();

// Load application settings from disk (or custom file path)
AppSettings load_settings(const std::string& path = "");

// Save application settings to disk (or custom file path)
void save_settings(const AppSettings& s, const std::string& path = "");

} // namespace hearthis
