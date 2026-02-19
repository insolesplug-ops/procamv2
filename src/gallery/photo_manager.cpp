/**
 * CinePi Camera - Photo Manager
 * Orchestrates the capture pipeline:
 * 1. Check flash requirement
 * 2. Fire flash if needed
 * 3. Trigger capture
 * 4. Save JPEG
 * 5. Haptic feedback
 */

#include "gallery/photo_manager.h"
#include "camera/camera_pipeline.h"
#include "camera/photo_capture.h"
#include "drivers/gpio_driver.h"
#include "drivers/i2c_sensors.h"
#include "core/config.h"

#include <cstdio>

namespace cinepi {

PhotoManager::PhotoManager() = default;
PhotoManager::~PhotoManager() = default;

void PhotoManager::init(CameraPipeline& cam, GpioDriver& gpio, I2CSensors& sensors) {
    cam_ = &cam;
    gpio_ = &gpio;
    sensors_ = &sensors;

    // Register shutter button callback
    gpio.on_shutter([this]() {
        trigger_capture();
    });

    fprintf(stderr, "[PhotoManager] Initialized\n");
}

void PhotoManager::trigger_capture() {
    if (capturing_) return;
    capturing_ = true;

    auto& cfg = ConfigManager::instance().get();

    // Generate filename
    std::string path = PhotoCapture::generate_filename(cfg.photo_dir);

    // Check flash
    CaptureParams params;
    params.iso = cfg.camera.iso;
    params.shutter_us = cfg.camera.shutter_us;
    params.wb_mode = cfg.camera.wb_mode;
    params.flash_mode = cfg.camera.flash_mode;
    params.ambient_lux = sensors_->cached_lux();

    bool use_flash = PhotoCapture::should_flash(params);
    if (use_flash && gpio_) {
        gpio_->set_flash(true);
    }

    // Trigger capture
    cam_->capture_photo(path, [this, path, use_flash](const std::string& saved_path, bool success) {
        // Turn off flash
        if (use_flash && gpio_) {
            gpio_->set_flash(false);
        }

        if (success) {
            last_path_ = saved_path;
            // Haptic feedback
            if (gpio_) {
                gpio_->vibrate(50);
            }
            fprintf(stderr, "[PhotoManager] Captured: %s\n", saved_path.c_str());
        } else {
            fprintf(stderr, "[PhotoManager] Capture failed\n");
        }

        capturing_ = false;

        if (done_cb_) {
            done_cb_(success, saved_path);
        }
    });

    // For synchronous mode (when capture_photo doesn't call callback immediately),
    // we do an immediate vibrate as feedback that the capture was initiated
    if (gpio_) {
        gpio_->vibrate(30);
    }
}

void PhotoManager::on_capture_done(DoneCallback cb) {
    done_cb_ = std::move(cb);
}

} // namespace cinepi
