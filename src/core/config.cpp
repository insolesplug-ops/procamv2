/**
 * CinePi Camera - Configuration Manager
 * Loads/saves settings from JSON on disk.
 */

#include "core/config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <sys/stat.h>

using json = nlohmann::json;

namespace cinepi {

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

bool ConfigManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!path.empty()) config_.config_path = path;

    std::ifstream f(config_.config_path);
    if (!f.is_open()) {
        fprintf(stderr, "[Config] No config file at %s, using defaults\n",
                config_.config_path.c_str());
        return false;
    }

    try {
        json j = json::parse(f);

        if (j.contains("camera")) {
            auto& c = j["camera"];
            if (c.contains("iso"))           config_.camera.iso = c["iso"];
            if (c.contains("shutter_us"))    config_.camera.shutter_us = c["shutter_us"];
            if (c.contains("wb_mode"))       config_.camera.wb_mode = c["wb_mode"];
            if (c.contains("grid_overlay"))  config_.camera.grid_overlay = c["grid_overlay"];
            if (c.contains("digital_level")) config_.camera.digital_level = c["digital_level"];
            if (c.contains("flash_mode"))    config_.camera.flash_mode = c["flash_mode"];
            if (c.contains("colour_temp"))   config_.camera.colour_temp = c["colour_temp"];
        }
        if (j.contains("display")) {
            auto& d = j["display"];
            if (d.contains("brightness"))    config_.display.brightness = d["brightness"];
            if (d.contains("standby_sec"))   config_.display.standby_sec = d["standby_sec"];
        }
        if (j.contains("photo_dir")) {
            config_.photo_dir = j["photo_dir"].get<std::string>();
        }
    } catch (const json::exception& e) {
        fprintf(stderr, "[Config] JSON parse error: %s\n", e.what());
        return false;
    }

    fprintf(stderr, "[Config] Loaded from %s\n", config_.config_path.c_str());
    return true;
}

bool ConfigManager::save() {
    std::lock_guard<std::mutex> lk(mtx_);

    json j;
    j["camera"]["iso"]           = config_.camera.iso;
    j["camera"]["shutter_us"]    = config_.camera.shutter_us;
    j["camera"]["wb_mode"]       = config_.camera.wb_mode;
    j["camera"]["grid_overlay"]  = config_.camera.grid_overlay;
    j["camera"]["digital_level"] = config_.camera.digital_level;
    j["camera"]["flash_mode"]    = config_.camera.flash_mode;
    j["camera"]["colour_temp"]   = config_.camera.colour_temp;
    j["display"]["brightness"]   = config_.display.brightness;
    j["display"]["standby_sec"]  = config_.display.standby_sec;
    j["photo_dir"]               = config_.photo_dir;
    j["version"]                 = config_.version;

    std::ofstream f(config_.config_path);
    if (!f.is_open()) {
        fprintf(stderr, "[Config] Cannot write to %s\n", config_.config_path.c_str());
        return false;
    }
    f << j.dump(2);
    f.close();
    fprintf(stderr, "[Config] Saved to %s\n", config_.config_path.c_str());
    return true;
}

AppConfig& ConfigManager::get() {
    return config_;
}

const AppConfig& ConfigManager::get() const {
    return config_;
}

void ConfigManager::update_camera(const CameraSettings& s) {
    std::lock_guard<std::mutex> lk(mtx_);
    config_.camera = s;
}

void ConfigManager::update_display(const DisplaySettings& s) {
    std::lock_guard<std::mutex> lk(mtx_);
    config_.display = s;
}

} // namespace cinepi
