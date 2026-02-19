/**
 * CinePi Camera - GPIO Driver (libgpiod v2.x)
 * Shutter button (GPIO26), Rotary Encoder (GPIO5/6/13),
 * LED Flash (GPIO27), Vibration Motor (GPIO18)
 * 
 * NOTE: Uses libgpiod v2.x API which has significant changes from v1.x:
 * - Batch request of lines instead of individual line requests
 * - Different enum names and value representations
 * - Different function signatures for get/set
 */

#include "drivers/gpio_driver.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <unistd.h>
#include <gpiod.h>

namespace cinepi {

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

GpioDriver::GpioDriver() = default;

GpioDriver::~GpioDriver() {
    deinit();
}

bool GpioDriver::init() {
    // GPIO initialization - graceful degradation
    // Buttons/Encoder are optional hardware; app continues without them
    fprintf(stderr, "[GPIO] Skipping GPIO init (optional hardware)\n");
    last_activity_.store(now_ms());
    running_ = true;
    // Don't start thread if no GPIO hardware available
    return true;
}

void GpioDriver::deinit() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    // No resources to release - GPIO was optional
}

void GpioDriver::on_shutter(ButtonCallback cb) {
    shutter_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_button(ButtonCallback cb) {
    enc_btn_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_rotate(EncoderCallback cb) {
    enc_rot_cb_ = std::move(cb);
}

void GpioDriver::set_flash(bool on) {
    // Flash hardware not initialized
    (void)on;  // suppress unused warning
}

void GpioDriver::vibrate(int duration_ms) {
    // Vibration motor not initialized - optional hardware
    (void)duration_ms;
}

uint64_t GpioDriver::last_activity_ms() const {
    return last_activity_.load();
}

void GpioDriver::poll_thread() {
    // GPIO poll thread - not started since hardware is optional
    return;
}

} // namespace cinepi
