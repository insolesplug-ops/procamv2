/**
 * CinePi Camera - Power Manager
 * Standby when idle: screen off, camera paused, LVGL paused, CPU powersave.
 * Wake on touch, shutter, or encoder input.
 */

#include "power/power_manager.h"
#include "drivers/drm_display.h"
#include "camera/camera_pipeline.h"
#include "drivers/touch_input.h"
#include "drivers/gpio_driver.h"
#include "drivers/i2c_sensors.h"
#include "ui/lvgl_driver.h"
#include "core/config.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <algorithm>

namespace cinepi {

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void set_cpu_governor(const char* governor) {
    // Set for all CPU cores
    for (int i = 0; i < 4; i++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        FILE* fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "%s", governor);
            fclose(fp);
        }
    }
}

PowerManager::PowerManager() = default;
PowerManager::~PowerManager() = default;

void PowerManager::init(DrmDisplay& display, CameraPipeline& cam,
                        TouchInput& touch, GpioDriver& gpio,
                        I2CSensors& sensors, LvglDriver& lvgl) {
    display_ = &display;
    cam_ = &cam;
    touch_ = &touch;
    gpio_ = &gpio;
    sensors_ = &sensors;
    lvgl_ = &lvgl;

    timeout_sec_ = ConfigManager::instance().get().display.standby_sec;
    saved_brightness_ = ConfigManager::instance().get().display.brightness;

    fprintf(stderr, "[Power] Initialized (timeout=%ds)\n", timeout_sec_);
}

void PowerManager::update() {
    if (timeout_sec_ <= 0) return;  // Never standby

    uint64_t now = now_ms();
    uint64_t last = last_activity_ms();
    uint64_t idle_ms = (last > 0) ? (now - last) : 0;

    if (standby_.load()) {
        // Check for wake triggers
        if (idle_ms < 500) {  // Activity detected recently
            wake();
        }
    } else {
        // Check for idle timeout
        uint64_t timeout_ms = static_cast<uint64_t>(timeout_sec_) * 1000;
        bool gyro_still = !sensors_->has_movement(5.0f);

        if (idle_ms > timeout_ms && gyro_still) {
            sleep();
        }
    }
}

void PowerManager::sleep() {
    if (standby_.load()) return;
    standby_ = true;

    fprintf(stderr, "[Power] Entering standby\n");

    // Save current brightness
    saved_brightness_ = ConfigManager::instance().get().display.brightness;

    // 1. Screen off
    display_->set_blank(true);

    // 2. Pause camera
    cam_->stop_preview();

    // 3. Pause LVGL rendering
    lvgl_->pause();

    // 4. CPU powersave
    set_cpu_governor("powersave");
}

void PowerManager::wake() {
    if (!standby_.load()) return;
    standby_ = false;

    fprintf(stderr, "[Power] Waking up\n");

    // 1. CPU performance
    set_cpu_governor("performance");

    // 2. Resume LVGL
    lvgl_->resume();

    // 3. Resume camera
    cam_->start_preview();

    // 4. Screen on
    auto& cfg = ConfigManager::instance().get();
    cfg.display.brightness = saved_brightness_;
    display_->set_blank(false);
}

void PowerManager::set_timeout(int seconds) {
    timeout_sec_ = seconds;
}

uint64_t PowerManager::last_activity_ms() const {
    uint64_t touch_ms = touch_ ? touch_->last_activity_ms() : 0;
    uint64_t gpio_ms  = gpio_ ? gpio_->last_activity_ms() : 0;
    return std::max(touch_ms, gpio_ms);
}

} // namespace cinepi
