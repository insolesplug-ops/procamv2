#pragma once
/**
 * CinePi Camera - LVGL Display/Input Driver
 * Connects LVGL to DRM framebuffer and touch input.
 */

#include <cstdint>

namespace cinepi {

class DrmDisplay;
class TouchInput;

class LvglDriver {
public:
    LvglDriver();
    ~LvglDriver();

    bool init(DrmDisplay& display, TouchInput& touch);
    void deinit();

    // Call in main loop
    void tick();

    // Pause/resume rendering (for standby)
    void pause();
    void resume();

    bool is_paused() const { return paused_; }

private:
    static void flush_cb(struct _lv_disp_drv_t* drv, const struct _lv_area_t* area,
                          uint16_t* color_p);  // actually lv_color_t*
    static void input_read_cb(struct _lv_indev_drv_t* drv, struct _lv_indev_data_t* data);

    DrmDisplay* display_ = nullptr;
    TouchInput* touch_ = nullptr;
    bool paused_ = false;
    bool initialized_ = false;

    // LVGL draw buffers
    uint16_t* buf1_ = nullptr;
    uint16_t* buf2_ = nullptr;
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
