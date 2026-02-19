/**
 * CinePi Camera - GPIO Driver (libgpiod)
 * Shutter button (GPIO26), Rotary Encoder (GPIO5/6/13),
 * LED Flash (GPIO27), Vibration Motor (GPIO18)
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
    chip_ = gpiod_chip_open(GPIO_CHIP);
    if (!chip_) {
        fprintf(stderr, "[GPIO] Failed to open %s: %s\n", GPIO_CHIP, strerror(errno));
        return false;
    }

    // Shutter button (GPIO26, input, pull-up, active-low)
    shutter_line_ = gpiod_chip_get_line(chip_, GPIO_SHUTTER_BTN);
    if (shutter_line_) {
        struct gpiod_line_request_config cfg = {};
        cfg.consumer = "cinepi-shutter";
        cfg.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
        cfg.flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        if (gpiod_line_request(shutter_line_, &cfg, 0) != 0) {
            fprintf(stderr, "[GPIO] Shutter line request failed\n");
            shutter_line_ = nullptr;
        }
    }

    // Encoder CLK (GPIO5)
    enc_clk_line_ = gpiod_chip_get_line(chip_, GPIO_ENCODER_CLK);
    if (enc_clk_line_) {
        struct gpiod_line_request_config cfg = {};
        cfg.consumer = "cinepi-enc-clk";
        cfg.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
        cfg.flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        if (gpiod_line_request(enc_clk_line_, &cfg, 0) != 0) {
            enc_clk_line_ = nullptr;
        }
    }

    // Encoder DT (GPIO6)
    enc_dt_line_ = gpiod_chip_get_line(chip_, GPIO_ENCODER_DT);
    if (enc_dt_line_) {
        struct gpiod_line_request_config cfg = {};
        cfg.consumer = "cinepi-enc-dt";
        cfg.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
        cfg.flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        if (gpiod_line_request(enc_dt_line_, &cfg, 0) != 0) {
            enc_dt_line_ = nullptr;
        }
    }

    // Encoder Button (GPIO13)
    enc_btn_line_ = gpiod_chip_get_line(chip_, GPIO_ENCODER_BTN);
    if (enc_btn_line_) {
        struct gpiod_line_request_config cfg = {};
        cfg.consumer = "cinepi-enc-btn";
        cfg.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
        cfg.flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        if (gpiod_line_request(enc_btn_line_, &cfg, 0) != 0) {
            enc_btn_line_ = nullptr;
        }
    }

    // Flash LED (GPIO27, output)
    flash_line_ = gpiod_chip_get_line(chip_, GPIO_LED_FLASH);
    if (flash_line_) {
        if (gpiod_line_request_output(flash_line_, "cinepi-flash", 0) != 0) {
            flash_line_ = nullptr;
        }
    }

    // Vibration Motor (GPIO18, output)
    vibrate_line_ = gpiod_chip_get_line(chip_, GPIO_VIBRATION);
    if (vibrate_line_) {
        if (gpiod_line_request_output(vibrate_line_, "cinepi-vib", 0) != 0) {
            vibrate_line_ = nullptr;
        }
    }

    // Read initial encoder state
    if (enc_clk_line_) {
        enc_last_clk_ = gpiod_line_get_value(enc_clk_line_);
    }

    last_activity_.store(now_ms());

    running_ = true;
    thread_ = std::thread(&GpioDriver::poll_thread, this);

    fprintf(stderr, "[GPIO] Initialized (shutter=%s, encoder=%s, flash=%s, vib=%s)\n",
            shutter_line_ ? "ok" : "fail",
            enc_clk_line_ ? "ok" : "fail",
            flash_line_ ? "ok" : "fail",
            vibrate_line_ ? "ok" : "fail");
    return true;
}

void GpioDriver::deinit() {
    running_ = false;
    if (thread_.joinable()) thread_.join();

    if (flash_line_) {
        gpiod_line_set_value(flash_line_, 0);
        gpiod_line_release(flash_line_);
    }
    if (vibrate_line_) {
        gpiod_line_set_value(vibrate_line_, 0);
        gpiod_line_release(vibrate_line_);
    }
    if (shutter_line_) gpiod_line_release(shutter_line_);
    if (enc_clk_line_) gpiod_line_release(enc_clk_line_);
    if (enc_dt_line_)  gpiod_line_release(enc_dt_line_);
    if (enc_btn_line_) gpiod_line_release(enc_btn_line_);

    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
}

void GpioDriver::on_shutter(ButtonCallback cb) {
    // IMPORTANT: Must be called BEFORE init() or while poll thread is not running
    // to avoid data race on std::function
    shutter_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_button(ButtonCallback cb) {
    enc_btn_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_rotate(EncoderCallback cb) {
    enc_rot_cb_ = std::move(cb);
}

void GpioDriver::set_flash(bool on) {
    if (flash_line_) {
        gpiod_line_set_value(flash_line_, on ? 1 : 0);
    }
}

void GpioDriver::vibrate(int duration_ms) {
    if (!vibrate_line_ || !running_) return;
    gpiod_line_set_value(vibrate_line_, 1);
    // Use a blocking sleep in-place instead of a detached thread
    // to avoid use-after-free if deinit() is called while sleeping.
    // Vibrate durations are short (30-50ms), so blocking is acceptable.
    usleep(duration_ms * 1000);
    if (vibrate_line_ && running_) {
        gpiod_line_set_value(vibrate_line_, 0);
    }
}

uint64_t GpioDriver::last_activity_ms() const {
    return last_activity_.load();
}

void GpioDriver::poll_thread() {
    bool shutter_prev = true;  // pull-up, active low
    bool enc_btn_prev = true;
    int debounce_ms = 50;
    uint64_t shutter_last = 0;
    uint64_t enc_btn_last = 0;

    while (running_) {
        uint64_t now = now_ms();

        // Shutter button
        if (shutter_line_) {
            int val = gpiod_line_get_value(shutter_line_);
            bool pressed = (val == 0);  // active low
            if (pressed && !shutter_prev && (now - shutter_last > debounce_ms)) {
                shutter_last = now;
                last_activity_.store(now);
                if (shutter_cb_) shutter_cb_();
            }
            shutter_prev = pressed;
        }

        // Encoder button
        if (enc_btn_line_) {
            int val = gpiod_line_get_value(enc_btn_line_);
            bool pressed = (val == 0);
            if (pressed && !enc_btn_prev && (now - enc_btn_last > debounce_ms)) {
                enc_btn_last = now;
                last_activity_.store(now);
                if (enc_btn_cb_) enc_btn_cb_();
            }
            enc_btn_prev = pressed;
        }

        // Rotary encoder
        if (enc_clk_line_ && enc_dt_line_) {
            int clk = gpiod_line_get_value(enc_clk_line_);
            if (clk != enc_last_clk_ && clk == 0) {
                int dt = gpiod_line_get_value(enc_dt_line_);
                int dir = (dt != clk) ? +1 : -1;
                last_activity_.store(now);
                if (enc_rot_cb_) enc_rot_cb_(dir);
            }
            enc_last_clk_ = clk;
        }

        usleep(2000);  // 2ms poll interval
    }
}

} // namespace cinepi
