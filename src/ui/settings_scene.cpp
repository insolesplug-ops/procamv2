/**
 * CinePi Camera - Settings Scene
 * Builds extended settings UI programmatically on the SquareLine settings screen.
 */

#include "ui/settings_scene.h"
#include "drivers/drm_display.h"
#include "core/config.h"
#include "core/constants.h"

#include "ui.h"
#include "lvgl/lvgl.h"

#include <cstdio>
#include <cstring>
#include <sys/statvfs.h>

namespace cinepi {

// Static UI elements created programmatically
static lv_obj_t* settings_list = nullptr;
static lv_obj_t* brightness_slider = nullptr;
static lv_obj_t* standby_dropdown = nullptr;
static lv_obj_t* used_label = nullptr;
static lv_obj_t* free_label = nullptr;

static void brightness_changed_cb(lv_event_t* e) {
    int val = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    auto& cfg = ConfigManager::instance().get();
    cfg.display.brightness = val;

    // Write to backlight immediately
    FILE* fp = fopen(BACKLIGHT_BRIGHTNESS, "w");
    if (fp) {
        fprintf(fp, "%d", val);
        fclose(fp);
    }
}

static void standby_changed_cb(lv_event_t* e) {
    uint16_t sel = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    int values[] = {10, 30, 60, 0};
    auto& cfg = ConfigManager::instance().get();
    cfg.display.standby_sec = values[sel];
}

static void reboot_cb(lv_event_t* e) {
    ConfigManager::instance().save();
    system("sudo reboot");
}

static void shutdown_cb(lv_event_t* e) {
    ConfigManager::instance().save();
    system("sudo shutdown -h now");
}

static void format_sd_cb(lv_event_t* e) {
    // Show confirmation messagebox
    static const char* btns[] = {"Format", "Cancel", ""};
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Format SD Card",
        "This will ERASE ALL photos! Are you sure?", btns, false);
    lv_obj_center(mbox);

    lv_obj_add_event_cb(mbox, [](lv_event_t* ev) {
        lv_obj_t* obj = lv_event_get_current_target(ev);
        const char* txt = lv_msgbox_get_active_btn_text(obj);
        if (txt && strcmp(txt, "Format") == 0) {
            // This is a dangerous operation - format the data partition
            system("sudo mkfs.ext4 -F /dev/mmcblk0p2");
            system("sudo reboot");
        }
        lv_msgbox_close(obj);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}

void SettingsScene::init() {
    // Will be built on first enter
}

void SettingsScene::enter() {
    if (!initialized_ && ui_settings1) {
        create_settings_ui();
        initialized_ = true;
    }

    // Update storage info
    if (used_label && free_label) {
        auto& cfg = ConfigManager::instance().get();
        struct statvfs stat;
        if (statvfs(cfg.photo_dir.c_str(), &stat) == 0) {
            float total_gb = (float)(stat.f_blocks * stat.f_frsize) / (1024.0f * 1024.0f * 1024.0f);
            float free_gb  = (float)(stat.f_bavail * stat.f_frsize) / (1024.0f * 1024.0f * 1024.0f);
            float used_gb  = total_gb - free_gb;

            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f GB", used_gb);
            lv_label_set_text(used_label, buf);
            snprintf(buf, sizeof(buf), "%.1f GB", free_gb);
            lv_label_set_text(free_label, buf);
        }
    }
}

void SettingsScene::leave() {
    // Save config on leaving settings
    ConfigManager::instance().save();
}

void SettingsScene::create_settings_ui() {
    auto& cfg = ConfigManager::instance().get();

    // Create scrollable list on settings screen
    settings_list = lv_list_create(ui_settings1);
    lv_obj_set_size(settings_list, 440, 700);
    lv_obj_align(settings_list, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(settings_list, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(settings_list, 255, 0);
    lv_obj_set_style_border_side(settings_list, LV_BORDER_SIDE_NONE, 0);
    lv_obj_set_style_radius(settings_list, 15, 0);
    lv_obj_set_style_pad_all(settings_list, 15, 0);

    // ═══ DISPLAY SECTION ═══
    lv_obj_t* hdr_display = lv_list_add_text(settings_list, "DISPLAY");
    lv_obj_set_style_text_color(hdr_display, lv_color_hex(0x00CA00), 0);
    lv_obj_set_style_text_font(hdr_display, &ui_font_Font2, 0);

    // Brightness slider
    lv_obj_t* brightness_row = lv_obj_create(settings_list);
    lv_obj_remove_style_all(brightness_row);
    lv_obj_set_size(brightness_row, LV_PCT(100), 50);
    lv_obj_set_style_pad_all(brightness_row, 5, 0);

    lv_obj_t* br_label = lv_label_create(brightness_row);
    lv_label_set_text(br_label, "Brightness");
    lv_obj_set_style_text_color(br_label, lv_color_hex(0xB4B4B4), 0);
    lv_obj_set_style_text_font(br_label, &ui_font_Font1, 0);
    lv_obj_align(br_label, LV_ALIGN_LEFT_MID, 0, 0);

    brightness_slider = lv_slider_create(brightness_row);
    lv_obj_set_width(brightness_slider, 200);
    lv_obj_align(brightness_slider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_slider_set_range(brightness_slider, 10, 255);
    lv_slider_set_value(brightness_slider, cfg.display.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x6A6A6A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x00CA00), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Standby Timeout
    lv_obj_t* standby_row = lv_obj_create(settings_list);
    lv_obj_remove_style_all(standby_row);
    lv_obj_set_size(standby_row, LV_PCT(100), 50);
    lv_obj_set_style_pad_all(standby_row, 5, 0);

    lv_obj_t* st_label = lv_label_create(standby_row);
    lv_label_set_text(st_label, "Standby");
    lv_obj_set_style_text_color(st_label, lv_color_hex(0xB4B4B4), 0);
    lv_obj_set_style_text_font(st_label, &ui_font_Font1, 0);
    lv_obj_align(st_label, LV_ALIGN_LEFT_MID, 0, 0);

    standby_dropdown = lv_dropdown_create(standby_row);
    lv_dropdown_set_options(standby_dropdown, "10 sec\n30 sec\n60 sec\nNever");
    lv_obj_set_width(standby_dropdown, 130);
    lv_obj_align(standby_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(standby_dropdown, lv_color_hex(0x383838), 0);
    lv_obj_set_style_text_color(standby_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_side(standby_dropdown, LV_BORDER_SIDE_NONE, 0);

    // Pre-select current value
    int standby_idx = 0;
    if (cfg.display.standby_sec == 30) standby_idx = 1;
    else if (cfg.display.standby_sec == 60) standby_idx = 2;
    else if (cfg.display.standby_sec == 0) standby_idx = 3;
    lv_dropdown_set_selected(standby_dropdown, standby_idx);
    lv_obj_add_event_cb(standby_dropdown, standby_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // ═══ STORAGE SECTION ═══
    lv_obj_t* hdr_storage = lv_list_add_text(settings_list, "STORAGE");
    lv_obj_set_style_text_color(hdr_storage, lv_color_hex(0x00CA00), 0);
    lv_obj_set_style_text_font(hdr_storage, &ui_font_Font2, 0);

    // Used space
    lv_obj_t* used_row = lv_obj_create(settings_list);
    lv_obj_remove_style_all(used_row);
    lv_obj_set_size(used_row, LV_PCT(100), 30);

    lv_obj_t* ul = lv_label_create(used_row);
    lv_label_set_text(ul, "Used Space");
    lv_obj_set_style_text_color(ul, lv_color_hex(0xB4B4B4), 0);
    lv_obj_set_style_text_font(ul, &ui_font_Font1, 0);
    lv_obj_align(ul, LV_ALIGN_LEFT_MID, 0, 0);

    used_label = lv_label_create(used_row);
    lv_label_set_text(used_label, "-- GB");
    lv_obj_set_style_text_color(used_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(used_label, &ui_font_Font1, 0);
    lv_obj_align(used_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Free space
    lv_obj_t* free_row = lv_obj_create(settings_list);
    lv_obj_remove_style_all(free_row);
    lv_obj_set_size(free_row, LV_PCT(100), 30);

    lv_obj_t* fl = lv_label_create(free_row);
    lv_label_set_text(fl, "Free Space");
    lv_obj_set_style_text_color(fl, lv_color_hex(0xB4B4B4), 0);
    lv_obj_set_style_text_font(fl, &ui_font_Font1, 0);
    lv_obj_align(fl, LV_ALIGN_LEFT_MID, 0, 0);

    free_label = lv_label_create(free_row);
    lv_label_set_text(free_label, "-- GB");
    lv_obj_set_style_text_color(free_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(free_label, &ui_font_Font1, 0);
    lv_obj_align(free_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Format SD Card button
    lv_obj_t* format_btn = lv_btn_create(settings_list);
    lv_obj_set_width(format_btn, LV_PCT(100));
    lv_obj_set_height(format_btn, 40);
    lv_obj_set_style_bg_color(format_btn, lv_color_hex(0x8B0000), 0);
    lv_obj_set_style_radius(format_btn, 8, 0);
    lv_obj_t* fmt_lbl = lv_label_create(format_btn);
    lv_label_set_text(fmt_lbl, "Format SD Card");
    lv_obj_set_style_text_color(fmt_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(fmt_lbl, &ui_font_Font1, 0);
    lv_obj_center(fmt_lbl);
    lv_obj_add_event_cb(format_btn, format_sd_cb, LV_EVENT_CLICKED, nullptr);

    // ═══ SYSTEM SECTION ═══
    lv_obj_t* hdr_sys = lv_list_add_text(settings_list, "SYSTEM");
    lv_obj_set_style_text_color(hdr_sys, lv_color_hex(0x00CA00), 0);
    lv_obj_set_style_text_font(hdr_sys, &ui_font_Font2, 0);

    // Reboot button
    lv_obj_t* reboot_btn = lv_btn_create(settings_list);
    lv_obj_set_width(reboot_btn, LV_PCT(100));
    lv_obj_set_height(reboot_btn, 40);
    lv_obj_set_style_bg_color(reboot_btn, lv_color_hex(0x383838), 0);
    lv_obj_set_style_radius(reboot_btn, 8, 0);
    lv_obj_t* reb_lbl = lv_label_create(reboot_btn);
    lv_label_set_text(reb_lbl, "Reboot");
    lv_obj_set_style_text_color(reb_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(reb_lbl, &ui_font_Font1, 0);
    lv_obj_center(reb_lbl);
    lv_obj_add_event_cb(reboot_btn, reboot_cb, LV_EVENT_CLICKED, nullptr);

    // Shutdown button
    lv_obj_t* shutdown_btn = lv_btn_create(settings_list);
    lv_obj_set_width(shutdown_btn, LV_PCT(100));
    lv_obj_set_height(shutdown_btn, 40);
    lv_obj_set_style_bg_color(shutdown_btn, lv_color_hex(0x383838), 0);
    lv_obj_set_style_radius(shutdown_btn, 8, 0);
    lv_obj_t* sht_lbl = lv_label_create(shutdown_btn);
    lv_label_set_text(sht_lbl, "Shutdown");
    lv_obj_set_style_text_color(sht_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sht_lbl, &ui_font_Font1, 0);
    lv_obj_center(sht_lbl);
    lv_obj_add_event_cb(shutdown_btn, shutdown_cb, LV_EVENT_CLICKED, nullptr);

    // Version
    lv_obj_t* ver_row = lv_obj_create(settings_list);
    lv_obj_remove_style_all(ver_row);
    lv_obj_set_size(ver_row, LV_PCT(100), 30);

    lv_obj_t* vl = lv_label_create(ver_row);
    lv_label_set_text(vl, "Version");
    lv_obj_set_style_text_color(vl, lv_color_hex(0xB4B4B4), 0);
    lv_obj_set_style_text_font(vl, &ui_font_Font1, 0);
    lv_obj_align(vl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* vv = lv_label_create(ver_row);
    char ver_buf[16];
    snprintf(ver_buf, sizeof(ver_buf), "v%s", cfg.version.c_str());
    lv_label_set_text(vv, ver_buf);
    lv_obj_set_style_text_color(vv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(vv, &ui_font_Font1, 0);
    lv_obj_align(vv, LV_ALIGN_RIGHT_MID, 0, 0);
}

} // namespace cinepi
