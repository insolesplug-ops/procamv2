/**
 * CinePi Camera - Scene Manager
 * Updates SquareLine UI with live sensor/battery/storage data.
 */

#include "ui/scene_manager.h"
#include "ui/lvgl_driver.h"
#include "camera/camera_pipeline.h"
#include "drivers/gpio_driver.h"
#include "drivers/i2c_sensors.h"
#include "drivers/drm_display.h"
#include "core/config.h"
#include "core/constants.h"

#include "ui.h"  // SquareLine generated

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/statvfs.h>

namespace cinepi {

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

void SceneManager::init(CameraPipeline& cam, GpioDriver& gpio, I2CSensors& sensors,
                        DrmDisplay& display, LvglDriver& lvgl) {
    cam_ = &cam;
    gpio_ = &gpio;
    sensors_ = &sensors;
    display_ = &display;
    lvgl_ = &lvgl;

    // Initialize SquareLine UI
    ui_init();

    setup_ui_callbacks();

    fprintf(stderr, "[SceneManager] Initialized\n");
}

void SceneManager::update() {
    frame_count_++;

    // Update status bar every ~30 frames (1 second at 30fps)
    if (frame_count_ % 30 == 0) {
        update_status_bar();
    }

    // Update level indicator every ~3 frames
    if (frame_count_ % 3 == 0) {
        update_level_indicator();
    }

    // Detect current screen
    lv_obj_t* active = lv_scr_act();
    if (active == ui_main) {
        current_ = Scene::Camera;
    } else if (active == ui_Gallery1) {
        current_ = Scene::Gallery;
    } else if (active == ui_settings1) {
        current_ = Scene::Settings;
    }
}

void SceneManager::update_status_bar() {
    if (!ui_INFOSONSCREEN) return;

    // Build status string: "12:34  64GB  85%"
    char buf[64];

    // Time
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);

    // Free space
    struct statvfs stat;
    float free_gb = 0;
    auto& cfg = ConfigManager::instance().get();
    if (statvfs(cfg.photo_dir.c_str(), &stat) == 0) {
        free_gb = (float)(stat.f_bavail * stat.f_frsize) / (1024.0f * 1024.0f * 1024.0f);
    }

    // Battery (read from UPS HAT sysfs or I2C)
    // Simplified: read from /sys/class/power_supply if available
    int battery_pct = 100;
    FILE* fp = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (fp) {
        if (fscanf(fp, "%d", &battery_pct) != 1) battery_pct = 100;
        fclose(fp);
    }

    snprintf(buf, sizeof(buf), "%02d:%02d   %.1fGB   %d%%",
             t->tm_hour, t->tm_min, free_gb, battery_pct);

    // Find or create a label inside INFOSONSCREEN
    // The SquareLine UI has INFOSONSCREEN as a container; we add a label dynamically
    lv_obj_t* label = lv_obj_get_child(ui_INFOSONSCREEN, 0);
    if (!label || !lv_obj_check_type(label, &lv_label_class)) {
        label = lv_label_create(ui_INFOSONSCREEN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label, &ui_font_Font1, 0);
    }
    lv_label_set_text(label, buf);
}

void SceneManager::update_level_indicator() {
    // Only show if digital level is enabled
    auto& cfg = ConfigManager::instance().get();
    if (!cfg.camera.digital_level) return;

    // Read gyro roll for level indication
    auto gyro = sensors_->cached_gyro();
    // Roll angle clamped to visual range
    // Positive = tilted right, Negative = tilted left
    // We could draw this on the LVGL overlay if needed
    (void)gyro;
}

void SceneManager::setup_ui_callbacks() {
    // Connect ISO slider to camera
    if (ui_ISO) {
        lv_obj_add_event_cb(ui_ISO, [](lv_event_t* e) {
            auto* self = static_cast<SceneManager*>(lv_event_get_user_data(e));
            if (!self || !self->cam_) return;
            int val = lv_slider_get_value(ui_ISO);
            // Map slider 0-100 to ISO index
            int idx = val * (kNumISO - 1) / 100;
            if (idx < 0) idx = 0;
            if (idx >= kNumISO) idx = kNumISO - 1;
            self->cam_->set_iso(kISOValues[idx]);
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.iso = kISOValues[idx];
        }, LV_EVENT_VALUE_CHANGED, this);
    }

    // Connect SHUTTER slider
    if (ui_SHUTTER) {
        lv_obj_add_event_cb(ui_SHUTTER, [](lv_event_t* e) {
            auto* self = static_cast<SceneManager*>(lv_event_get_user_data(e));
            if (!self || !self->cam_) return;
            int val = lv_slider_get_value(ui_SHUTTER);
            int idx = val * (kNumShutterSpeeds - 1) / 100;
            if (idx < 0) idx = 0;
            if (idx >= kNumShutterSpeeds) idx = kNumShutterSpeeds - 1;
            self->cam_->set_shutter(kShutterSpeeds[idx].us);
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.shutter_us = kShutterSpeeds[idx].us;
        }, LV_EVENT_VALUE_CHANGED, this);
    }

    // Grid overlay switch
    if (ui_GRIDSWITCH) {
        lv_obj_add_event_cb(ui_GRIDSWITCH, [](lv_event_t* e) {
            bool checked = lv_obj_has_state(ui_GRIDSWITCH, LV_STATE_CHECKED);
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.grid_overlay = checked;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Digital level switch
    if (ui_DIGITALLEVELSWITCH) {
        lv_obj_add_event_cb(ui_DIGITALLEVELSWITCH, [](lv_event_t* e) {
            bool checked = lv_obj_has_state(ui_DIGITALLEVELSWITCH, LV_STATE_CHECKED);
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.digital_level = checked;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Flash buttons (ON=Button1, AUTO=Button2, OFF=Button3)
    if (ui_Button1) {
        lv_obj_add_event_cb(ui_Button1, [](lv_event_t* e) {
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.flash_mode = 1;
            // Uncheck others
            lv_obj_clear_state(ui_Button2, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_Button3, LV_STATE_CHECKED);
            lv_obj_add_state(ui_Button1, LV_STATE_CHECKED);
        }, LV_EVENT_CLICKED, nullptr);
    }
    if (ui_Button2) {
        lv_obj_add_event_cb(ui_Button2, [](lv_event_t* e) {
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.flash_mode = 2;
            lv_obj_clear_state(ui_Button1, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_Button3, LV_STATE_CHECKED);
            lv_obj_add_state(ui_Button2, LV_STATE_CHECKED);
        }, LV_EVENT_CLICKED, nullptr);
    }
    if (ui_Button3) {
        lv_obj_add_event_cb(ui_Button3, [](lv_event_t* e) {
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.flash_mode = 0;
            lv_obj_clear_state(ui_Button1, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_Button2, LV_STATE_CHECKED);
            lv_obj_add_state(ui_Button3, LV_STATE_CHECKED);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Colour slider
    if (ui_COLOUR) {
        lv_obj_add_event_cb(ui_COLOUR, [](lv_event_t* e) {
            int val = lv_slider_get_value(ui_COLOUR);
            auto& cfg = ConfigManager::instance().get();
            cfg.camera.colour_temp = val / 100.0f;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

} // namespace cinepi
