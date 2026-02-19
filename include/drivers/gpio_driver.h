#pragma once
/**
 * CinePi Camera - GPIO Driver (libgpiod)
 * Handles shutter button, encoder, vibration motor, LED flash.
 */

#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>

struct gpiod_chip;
struct gpiod_line;

namespace cinepi {

using ButtonCallback = std::function<void()>;
using EncoderCallback = std::function<void(int direction)>;  // +1 or -1

class GpioDriver {
public:
    GpioDriver();
    ~GpioDriver();

    bool init();
    void deinit();

    // Set callbacks
    void on_shutter(ButtonCallback cb);
    void on_encoder_button(ButtonCallback cb);
    void on_encoder_rotate(EncoderCallback cb);

    // Outputs
    void set_flash(bool on);
    void vibrate(int duration_ms);

    // Last activity for standby
    uint64_t last_activity_ms() const;

private:
    void poll_thread();

    gpiod_chip* chip_ = nullptr;
    gpiod_line* shutter_line_ = nullptr;
    gpiod_line* enc_clk_line_ = nullptr;
    gpiod_line* enc_dt_line_  = nullptr;
    gpiod_line* enc_btn_line_ = nullptr;
    gpiod_line* flash_line_   = nullptr;
    gpiod_line* vibrate_line_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> last_activity_{0};

    ButtonCallback shutter_cb_;
    ButtonCallback enc_btn_cb_;
    EncoderCallback enc_rot_cb_;

    // Encoder state
    int enc_last_clk_ = 1;
};

} // namespace cinepi
