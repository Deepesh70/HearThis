#include "hearthis/settings.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <cstdio>

int main() {
    std::cout << "[HearThis C++] Testing Settings module..." << std::endl;

    std::string test_file = "test_settings_temp.txt";

    // Test 1: Default configuration state
    hearthis::AppSettings defaults;
    assert(defaults.provider_index == 0);
    assert(defaults.duration_seconds == 10);
    assert(defaults.audio_source == hearthis::AudioSource::Speakers);
    assert(defaults.dark_theme == true);
    std::cout << "  [PASS] Default settings verified." << std::endl;

    // Test 2: Save and Load roundtrip
    hearthis::AppSettings custom;
    custom.provider_index = 1;
    custom.duration_seconds = 5;
    custom.audio_source = hearthis::AudioSource::Microphone;
    custom.dark_theme = false;
    custom.acr_host = "custom-host.acrcloud.com";
    custom.acr_key = "key123";
    custom.acr_secret = "secret456";
    custom.audd_token = "token789";

    hearthis::save_settings(custom, test_file);

    hearthis::AppSettings loaded = hearthis::load_settings(test_file);
    assert(loaded.provider_index == 1);
    assert(loaded.duration_seconds == 5);
    assert(loaded.audio_source == hearthis::AudioSource::Microphone);
    assert(loaded.dark_theme == false);
    assert(loaded.acr_host == "custom-host.acrcloud.com");
    assert(loaded.acr_key == "key123");
    assert(loaded.acr_secret == "secret456");
    assert(loaded.audd_token == "token789");
    std::cout << "  [PASS] Settings serialization roundtrip verified." << std::endl;

    // Test 3: Legacy configuration keys (e.g. source=1 instead of audio_source=1)
    {
        std::ofstream out(test_file);
        out << "source=1\n";
        out << "theme=light\n";
        out << "duration=invalid_number\n"; // tests corruption resilience
        out.close();
    }

    hearthis::AppSettings legacy = hearthis::load_settings(test_file);
    assert(legacy.audio_source == hearthis::AudioSource::Microphone);
    assert(legacy.dark_theme == false);
    assert(legacy.duration_seconds == 10); // remains default due to invalid number protection
    std::cout << "  [PASS] Legacy key support and corruption resilience verified." << std::endl;

    std::remove(test_file.c_str());

    std::cout << "\n>>> ALL SETTINGS TESTS PASSED! <<<\n" << std::endl;
    return 0;
}
