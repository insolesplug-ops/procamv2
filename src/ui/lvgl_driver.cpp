/**
 * CinePi Camera - LVGL Display/Input Driver
 * Renders LVGL to ARGB8888 DRM overlay plane.
 */

#include "ui/lvgl_driver.h"
#include "drivers/drm_display.h"
#include "drivers/touch_input.h"
#include "core/constants.h"

#include "lvgl/lvgl.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace cinepi {

// Global references for LVGL C callbacks
static DrmDisplay* g_display = nullptr;
static TouchInput* g_touch = nullptr;

static uint64_t g_start_ms = 0;

static uint64_t get_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

LvglDriver::LvglDriver() = default;

LvglDriver::~LvglDriver() {
    deinit();
}

bool LvglDriver::init(DrmDisplay& display, TouchInput* touch) {
    display_ = &display;
    touch_ = touch;
    g_display = &display;
    g_touch = touch;
    g_start_ms = get_ms();

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers as lv_color_t arrays, stored as void* (matching header)
    uint32_t buf_size = DISPLAY_W * LVGL_BUF_LINES;
    buf1_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    buf2_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    if (!buf1_ || !buf2_) {
        fprintf(stderr, "[LVGL] Buffer allocation failed (requested %u bytes)\n", buf_size * (uint32_t)sizeof(lv_color_t));
        if (buf1_) { free(buf1_); buf1_ = nullptr; }
        if (buf2_) { free(buf2_); buf2_ = nullptr; }
        return false;
    }

    // Display driver
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1_, buf2_, buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_W;
    disp_drv.ver_res = DISPLAY_H;
    disp_drv.draw_buf = &draw_buf;
    // Use reinterpret_cast to handle void* -> typedef* conversion
    disp_drv.flush_cb = reinterpret_cast<decltype(disp_drv.flush_cb)>(LvglDriver::flush_cb);
    disp_drv.user_data = this;
    lv_disp_drv_register(&disp_drv);

    // Input driver (touch)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = reinterpret_cast<decltype(indev_drv.read_cb)>(LvglDriver::input_read_cb);
    indev_drv.user_data = this;
    lv_indev_drv_register(&indev_drv);

    initialized_ = true;
    fprintf(stderr, "[LVGL] Initialized (%dx%d, buf=%d lines)\n",
            DISPLAY_W, DISPLAY_H, LVGL_BUF_LINES);
    return true;
}

void LvglDriver::deinit() {
    if (buf1_) { free(buf1_); buf1_ = nullptr; }
    if (buf2_) { free(buf2_); buf2_ = nullptr; }
    g_display = nullptr;
    g_touch = nullptr;
    initialized_ = false;
}

void LvglDriver::tick() {
    if (!initialized_ || paused_) return;
    lv_timer_handler();
}

void LvglDriver::pause() {
    paused_ = true;
}

void LvglDriver::resume() {
    paused_ = false;
}

void LvglDriver::flush_cb(void* drv_void, const void* area_void, void* color_p_void) {
    auto* drv = reinterpret_cast<lv_disp_drv_t*>(drv_void);
    auto* area = reinterpret_cast<const lv_area_t*>(area_void);
    auto* color_p = reinterpret_cast<lv_color_t*>(color_p_void);
    if (!g_display) {
        lv_disp_flush_ready(drv);
        return;
    }

    uint8_t* fb = g_display->get_ui_buffer();
    int fb_pitch = g_display->get_ui_pitch();

    if (!fb) {
        lv_disp_flush_ready(drv);
        return;
    }

    // Query the active screen's background opacity and color from the LVGL
    // object tree.  When bg_opa is transparent, pixels matching the screen
    // background color are written with the screen's opacity so the camera
    // feed shows through.  All other pixels (actual UI content) get full
    // opacity, which means genuinely-black widgets render correctly.
    lv_obj_t* scr = lv_scr_act();
    lv_opa_t  scr_bg_opa   = lv_obj_get_style_bg_opa(scr, LV_PART_MAIN);
    lv_color_t scr_bg_color = lv_obj_get_style_bg_color(scr, LV_PART_MAIN);
    uint16_t  bg_raw        = scr_bg_color.full;

    // Copy rendered area to ARGB8888 framebuffer
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    for (int y = 0; y < h; y++) {
        int fb_y = area->y1 + y;
        uint32_t* dst = reinterpret_cast<uint32_t*>(fb + fb_y * fb_pitch) + area->x1;
        lv_color_t* src = color_p + y * w;

        for (int x = 0; x < w; x++) {
            // Convert RGB565 to ARGB8888
            uint16_t c = src[x].full;
            uint8_t r = ((c >> 11) & 0x1F) << 3;
            uint8_t g = ((c >> 5)  & 0x3F) << 2;
            uint8_t b = ((c >> 0)  & 0x1F) << 3;

            // Determine alpha from the screen background opacity:
            // If this pixel matches the screen bg color, use the screen's
            // bg_opa as its alpha (transparent bg -> camera shows through).
            // Otherwise the pixel belongs to an actual UI widget and gets
            // full opacity, so black text/icons remain visible.
            uint8_t a = (c == bg_raw) ? scr_bg_opa : 255;
            dst[x] = (static_cast<uint32_t>(a) << 24) | (r << 16) | (g << 8) | b;
        }
    }

    lv_disp_flush_ready(drv);
}

void LvglDriver::input_read_cb(void* drv_void, void* data_void) {
    auto* drv = reinterpret_cast<lv_indev_drv_t*>(drv_void);
    auto* data = reinterpret_cast<lv_indev_data_t*>(data_void);
    if (!g_touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    TouchPoint tp = g_touch->read();
    data->point.x = tp.x;
    data->point.y = tp.y;
    data->state = tp.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

} // namespace cinepi

// Tick provider for LVGL
extern "C" uint32_t cinepi_lv_tick_get(void) {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}
