#pragma once
/**
 * CinePi Camera - Scene Manager
 * Bridges hardware events to SquareLine Studio UI.
 */

#include <cstdint>

namespace cinepi {

class CameraPipeline;
class GpioDriver;
class I2CSensors;
class DrmDisplay;
class LvglDriver;
struct AppConfig;

enum class Scene {
    Camera,
    Gallery,
    Settings
};

class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    void init(CameraPipeline& cam, GpioDriver& gpio, I2CSensors& sensors,
              DrmDisplay& display, LvglDriver& lvgl);

    // Update UI elements with live data (call each frame)
    void update();

    // Get currently active scene
    Scene current_scene() const { return current_; }

private:
    void update_status_bar();
    void update_level_indicator();
    void setup_ui_callbacks();

    CameraPipeline* cam_ = nullptr;
    GpioDriver* gpio_ = nullptr;
    I2CSensors* sensors_ = nullptr;
    DrmDisplay* display_ = nullptr;
    LvglDriver* lvgl_ = nullptr;

    Scene current_ = Scene::Camera;
    uint32_t frame_count_ = 0;
};

} // namespace cinepi
