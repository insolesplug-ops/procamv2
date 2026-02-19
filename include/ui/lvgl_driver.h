#pragma once
/**
 * CinePi Camera - LVGL Display/Input Driver
 * Connects LVGL to DRM framebuffer and touch input.
 */

#include <cstdint>

// Forward-declare LVGL opaque types to avoid including lvgl.h in header
struct _lv_disp_drv_t;
struct _lv_area_t;
struct _lv_color_t;
struct _lv_indev_drv_t;
struct _lv_indev_data_t;

namespace cinepi {

class DrmDisplay;
class TouchInput;

class LvglDriver {
public:
    LvglDriver();
    ~LvglDriver();

    bool init(DrmDisplay& display, TouchInput* touch);
    void deinit();

    // Call in main loop
    void tick();

    // Pause/resume rendering (for standby)
    void pause();
    void resume();

    bool is_paused() const { return paused_; }

private:
    // Callbacks use void* to avoid type conflicts with LVGL internal types
    static void flush_cb(void* drv, const void* area, void* color_p);
    static void input_read_cb(void* drv, void* data);

    DrmDisplay* display_ = nullptr;
    TouchInput* touch_ = nullptr;
    bool paused_ = false;
    bool initialized_ = false;

    // LVGL draw buffers (allocated as lv_color_t in .cpp)
    void* buf1_ = nullptr;
    void* buf2_ = nullptr;
};

} // namespace cinepi

// Tick function for lv_conf.h
#ifdef __cplusplus
extern "C" {
#endif
uint32_t cinepi_lv_tick_get(void);
#ifdef __cplusplus
}
#endif
