#pragma once
/**
 * CinePi Camera - GPIO Driver (libgpiod v2.x)
 * Handles shutter button, encoder, vibration motor, LED flash.
 */

#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>

struct gpiod_chip;
struct gpiod_line_request;

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
    gpiod_line_request* input_req_ = nullptr;   // Shutter, Enc CLK/DT, Enc Button
    gpiod_line_request* output_req_ = nullptr;  // Flash LED, Vibration Motor

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
