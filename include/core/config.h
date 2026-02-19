#pragma once
/**
 * CinePi Camera - Application Configuration
 * Persistent settings stored in JSON on disk.
 */

#include <string>
#include <mutex>
#include <cstdint>

namespace cinepi {

struct CameraSettings {
    int iso           = 100;      // 100,200,400,800,1600,3200
    int shutter_us    = 8333;     // microseconds (1/120 default)
    int wb_mode       = 0;        // 0=Auto,1=Daylight,2=Cloudy,3=Tungsten
    bool grid_overlay = false;
    bool digital_level = false;
    int flash_mode    = 0;        // 0=OFF, 1=ON, 2=AUTO
    float colour_temp = 0.5f;     // 0.0-1.0 normalized
};

struct DisplaySettings {
    int brightness     = 128;     // 0-255
    int standby_sec    = 10;      // 10,30,60,0(never)
    bool show_clock    = true;    // show clock/status overlay
};

struct AppConfig {
    CameraSettings camera;
    DisplaySettings display;
    std::string photo_dir   = "/home/pi/photos";
    std::string config_path = "/home/pi/.cinepi_config.json";
    std::string version     = "1.0.0";
};

class ConfigManager {
public:
    static ConfigManager& instance();

    bool load(const std::string& path = "");
    bool save();

    AppConfig& get();
    const AppConfig& get() const;

    // Thread-safe accessors
    void update_camera(const CameraSettings& s);
    void update_display(const DisplaySettings& s);

private:
    ConfigManager() = default;
    AppConfig config_;
    mutable std::mutex mtx_;
};

// Shutter speed lookup: index -> microseconds
struct ShutterEntry {
    const char* label;
    int us;
};

inline constexpr ShutterEntry kShutterSpeeds[] = {
    {"1/30",   33333},
    {"1/60",   16666},
    {"1/125",   8000},
    {"1/250",   4000},
    {"1/500",   2000},
    {"1/1000",  1000},
};
inline constexpr int kNumShutterSpeeds = sizeof(kShutterSpeeds)/sizeof(kShutterSpeeds[0]);

inline constexpr int kISOValues[] = {100, 200, 400, 800, 1600, 3200};
inline constexpr int kNumISO = sizeof(kISOValues)/sizeof(kISOValues[0]);

} // namespace cinepi
