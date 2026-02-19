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
    // Open GPIO chip
    chip_ = gpiod_chip_open(GPIO_CHIP);
    if (!chip_) {
        fprintf(stderr, "[GPIO] Failed to open %s: %s\n", GPIO_CHIP, strerror(errno));
        return false;
    }

    // Define input lines: shutter, encoder CLK, encoder DT, encoder button
    unsigned int input_offsets[] = {
        GPIO_SHUTTER_BTN,    // 0
        GPIO_ENCODER_CLK,    // 1
        GPIO_ENCODER_DT,     // 2
        GPIO_ENCODER_BTN     // 3
    };
    const unsigned int NUM_INPUTS = 4;

    // Create and configure input request
    struct gpiod_line_config *input_cfg = gpiod_line_config_new();
    if (!input_cfg) {
        fprintf(stderr, "[GPIO] Failed to allocate input config\n");
        gpiod_chip_close(chip_);
        chip_ = nullptr;
        return false;
    }

    // Set direction and flags for all input lines
    gpiod_line_config_set_direction_input(input_cfg);
    gpiod_line_config_set_bias(input_cfg, GPIOD_LINE_BIAS_PULL_UP);

    // Request all input lines at once
    input_req_ = gpiod_chip_request_lines(chip_, nullptr, input_cfg,
                                          input_offsets, NUM_INPUTS);
    gpiod_line_config_free(input_cfg);

    if (!input_req_) {
        fprintf(stderr, "[GPIO] Failed to request input lines: %s\n", strerror(errno));
        gpiod_chip_close(chip_);
        chip_ = nullptr;
        return false;
    }

    // Define output lines: flash LED, vibration motor
    unsigned int output_offsets[] = {
        GPIO_LED_FLASH,      // 0
        GPIO_VIBRATION       // 1
    };
    const unsigned int NUM_OUTPUTS = 2;

    // Create and configure output request
    struct gpiod_line_config *output_cfg = gpiod_line_config_new();
    if (!output_cfg) {
        fprintf(stderr, "[GPIO] Failed to allocate output config\n");
        gpiod_line_request_release(input_req_);
        gpiod_chip_close(chip_);
        chip_ = nullptr;
        return false;
    }

    // Set direction for output lines
    gpiod_line_config_set_direction_output(output_cfg);
    gpiod_line_config_set_output_value(output_cfg, GPIOD_LINE_VALUE_INACTIVE);

    // Request all output lines at once
    output_req_ = gpiod_chip_request_lines(chip_, nullptr, output_cfg,
                                           output_offsets, NUM_OUTPUTS);
    gpiod_line_config_free(output_cfg);

    if (!output_req_) {
        fprintf(stderr, "[GPIO] Failed to request output lines: %s\n", strerror(errno));
        gpiod_line_request_release(input_req_);
        gpiod_chip_close(chip_);
        chip_ = nullptr;
        return false;
    }

    // Read initial encoder CLK state (input line 1)
    enum gpiod_line_value clk_val = gpiod_line_request_get_value(input_req_, 1);
    enc_last_clk_ = (clk_val == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;

    last_activity_.store(now_ms());
    running_ = true;
    thread_ = std::thread(&GpioDriver::poll_thread, this);

    fprintf(stderr, "[GPIO] Initialized (libgpiod v2.x)\n");
    return true;
}

void GpioDriver::deinit() {
    running_ = false;
    if (thread_.joinable()) thread_.join();

    // Release output request (turns off outputs first)
    if (output_req_) {
        gpiod_line_request_set_value(output_req_, 0, GPIOD_LINE_VALUE_INACTIVE);  // flash
        gpiod_line_request_set_value(output_req_, 1, GPIOD_LINE_VALUE_INACTIVE);  // vibrate
        gpiod_line_request_release(output_req_);
        output_req_ = nullptr;
    }

    // Release input request
    if (input_req_) {
        gpiod_line_request_release(input_req_);
        input_req_ = nullptr;
    }

    // Close chip
    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
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
    if (output_req_) {
        enum gpiod_line_value val = on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
        gpiod_line_request_set_value(output_req_, 0, val);  // output line 0 = flash
    }
}

void GpioDriver::vibrate(int duration_ms) {
    if (!output_req_ || !running_) return;
    
    // Turn on vibration motor (output line 1)
    gpiod_line_request_set_value(output_req_, 1, GPIOD_LINE_VALUE_ACTIVE);
    usleep(duration_ms * 1000);
    
    // Turn off vibration motor
    if (output_req_ && running_) {
        gpiod_line_request_set_value(output_req_, 1, GPIOD_LINE_VALUE_INACTIVE);
    }
}

uint64_t GpioDriver::last_activity_ms() const {
    return last_activity_.load();
}

void GpioDriver::poll_thread() {
    bool shutter_prev = true;   // pull-up: HIGH = not pressed
    bool enc_btn_prev = true;
    int debounce_ms = 50;
    uint64_t shutter_last = 0;
    uint64_t enc_btn_last = 0;

    while (running_) {
        if (!input_req_) break;

        uint64_t now = now_ms();

        // Read all input lines at once (more efficient than individual reads)
        enum gpiod_line_value values[4];
        int ret = gpiod_line_request_get_values(input_req_, values);
        if (ret != 0) {
            usleep(2000);
            continue;
        }

        // Process input line 0: shutter button (active-low)
        bool shutter_active = (values[0] == GPIOD_LINE_VALUE_ACTIVE);
        bool shutter_pressed = !shutter_active;  // inverted: active=pressed in our logic
        if (shutter_pressed && !shutter_prev && (now - shutter_last > debounce_ms)) {
            shutter_last = now;
            last_activity_.store(now);
            if (shutter_cb_) shutter_cb_();
        }
        shutter_prev = shutter_pressed;

        // Process input line 3: encoder button (active-low)
        bool enc_btn_active = (values[3] == GPIOD_LINE_VALUE_ACTIVE);
        bool enc_btn_pressed = !enc_btn_active;
        if (enc_btn_pressed && !enc_btn_prev && (now - enc_btn_last > debounce_ms)) {
            enc_btn_last = now;
            last_activity_.store(now);
            if (enc_btn_cb_) enc_btn_cb_();
        }
        enc_btn_prev = enc_btn_pressed;

        // Process rotary encoder: line 1 = CLK, line 2 = DT
        int clk = (values[1] == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
        int dt = (values[2] == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;

        if (clk != enc_last_clk_ && clk == 0) {
            int dir = (dt != clk) ? +1 : -1;
            last_activity_.store(now);
            if (enc_rot_cb_) enc_rot_cb_(dir);
        }
        enc_last_clk_ = clk;

        usleep(2000);  // 2ms poll interval
    }
}

} // namespace cinepi
