/**
 * CinePi Camera - Camera Scene Extensions
 * Draws rule-of-thirds grid and level indicator over camera preview.
 */

#include "ui/camera_scene.h"
#include "camera/camera_pipeline.h"
#include "drivers/i2c_sensors.h"
#include "core/constants.h"
#include "core/config.h"

#include "ui.h"
#include "lvgl/lvgl.h"

namespace cinepi {

// Static LVGL objects for overlays
static lv_obj_t* grid_lines[4] = {};   // 2 horizontal + 2 vertical
static lv_obj_t* level_bar = nullptr;

void CameraScene::init() {
    // Grid overlay lines (created hidden)
    if (ui_main) {
        int third_x = DISPLAY_W / 3;
        int third_y = DISPLAY_H / 3;

        // Vertical lines
        for (int i = 0; i < 2; i++) {
            grid_lines[i] = lv_obj_create(ui_main);
            lv_obj_remove_style_all(grid_lines[i]);
            lv_obj_set_size(grid_lines[i], 1, DISPLAY_H);
            lv_obj_set_pos(grid_lines[i], third_x * (i + 1), 0);
            lv_obj_set_style_bg_color(grid_lines[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(grid_lines[i], 80, 0);
            lv_obj_add_flag(grid_lines[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(grid_lines[i], LV_OBJ_FLAG_CLICKABLE);
        }

        // Horizontal lines
        for (int i = 0; i < 2; i++) {
            grid_lines[i + 2] = lv_obj_create(ui_main);
            lv_obj_remove_style_all(grid_lines[i + 2]);
            lv_obj_set_size(grid_lines[i + 2], DISPLAY_W, 1);
            lv_obj_set_pos(grid_lines[i + 2], 0, third_y * (i + 1));
            lv_obj_set_style_bg_color(grid_lines[i + 2], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(grid_lines[i + 2], 80, 0);
            lv_obj_add_flag(grid_lines[i + 2], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(grid_lines[i + 2], LV_OBJ_FLAG_CLICKABLE);
        }

        // Level indicator bar (horizontal, at center)
        level_bar = lv_obj_create(ui_main);
        lv_obj_remove_style_all(level_bar);
        lv_obj_set_size(level_bar, 100, 3);
        lv_obj_set_align(level_bar, LV_ALIGN_CENTER);
        lv_obj_set_y(level_bar, 0);
        lv_obj_set_style_bg_color(level_bar, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_bg_opa(level_bar, 200, 0);
        lv_obj_add_flag(level_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(level_bar, LV_OBJ_FLAG_CLICKABLE);
    }
}

void CameraScene::update(const CameraPipeline& cam, const I2CSensors& sensors) {
    auto& cfg = ConfigManager::instance().get();

    if (grid_visible_ != cfg.camera.grid_overlay) {
        set_grid_visible(cfg.camera.grid_overlay);
    }

    if (level_visible_ != cfg.camera.digital_level) {
        set_level_visible(cfg.camera.digital_level);
    }

    if (level_visible_ && level_bar) {
        auto gyro = sensors.cached_gyro();
        draw_level(gyro.roll);
    }
}

void CameraScene::set_grid_visible(bool visible) {
    grid_visible_ = visible;
    for (auto* line : grid_lines) {
        if (!line) continue;
        if (visible)
            lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    }
}

void CameraScene::set_level_visible(bool visible) {
    level_visible_ = visible;
    if (level_bar) {
        if (visible)
            lv_obj_clear_flag(level_bar, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(level_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

void CameraScene::draw_grid() {
    // Grid is static, drawn once during init
}

void CameraScene::draw_level(float roll_deg) {
    if (!level_bar) return;

    // Clamp roll to visible range (-30 to +30 degrees)
    if (roll_deg > 30.0f) roll_deg = 30.0f;
    if (roll_deg < -30.0f) roll_deg = -30.0f;

    // Map roll to y offset (pixels)
    int y_offset = static_cast<int>(roll_deg * 2.0f);
    lv_obj_set_y(level_bar, y_offset);

    // Color: green when level (<2 degrees), yellow when slightly off, red when way off
    if (roll_deg > -2.0f && roll_deg < 2.0f) {
        lv_obj_set_style_bg_color(level_bar, lv_color_hex(0x00FF00), 0);
    } else if (roll_deg > -10.0f && roll_deg < 10.0f) {
        lv_obj_set_style_bg_color(level_bar, lv_color_hex(0xFFFF00), 0);
    } else {
        lv_obj_set_style_bg_color(level_bar, lv_color_hex(0xFF0000), 0);
    }
}

} // namespace cinepi
