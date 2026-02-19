/**
 * CinePi Camera - Touch Input Driver
 * Reads capacitive touch from /dev/input/eventX
 * Rotates coordinates from landscape (800x480) to portrait (480x800)
 */

#include "drivers/touch_input.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <chrono>

namespace cinepi {

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

TouchInput::TouchInput() = default;

TouchInput::~TouchInput() {
    deinit();
}

std::string TouchInput::find_touch_device() {
    // Scan /dev/input/ for a touchscreen device
    DIR* dir = opendir("/dev/input");
    if (!dir) return "";

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        char name[256] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        // Check for ABS_MT_POSITION_X capability (multitouch)
        unsigned long abs_bits[(ABS_MAX + 1) / (sizeof(long) * 8) + 1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

        bool has_mt_x = (abs_bits[ABS_MT_POSITION_X / (sizeof(long) * 8)] >>
                         (ABS_MT_POSITION_X % (sizeof(long) * 8))) & 1;
        bool has_abs_x = (abs_bits[ABS_X / (sizeof(long) * 8)] >>
                          (ABS_X % (sizeof(long) * 8))) & 1;

        close(fd);

        if (has_mt_x || has_abs_x) {
            fprintf(stderr, "[Touch] Found device: %s (%s)\n", path.c_str(), name);
            closedir(dir);
            return path;
        }
    }
    closedir(dir);
    return "";
}

bool TouchInput::init() {
    std::string dev = find_touch_device();
    if (dev.empty()) {
        fprintf(stderr, "[Touch] No touchscreen found\n");
        return false;
    }

    fd_ = open(dev.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        fprintf(stderr, "[Touch] Failed to open %s: %s\n", dev.c_str(), strerror(errno));
        return false;
    }

    // Grab exclusive access
    ioctl(fd_, EVIOCGRAB, 1);

    running_ = true;
    thread_ = std::thread(&TouchInput::reader_thread, this);
    fprintf(stderr, "[Touch] Initialized on %s\n", dev.c_str());
    return true;
}

void TouchInput::deinit() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) {
        ioctl(fd_, EVIOCGRAB, 0);
        close(fd_);
        fd_ = -1;
    }
}

TouchPoint TouchInput::read() {
    TouchPoint tp;
    int phys_x = raw_x_.load(std::memory_order_acquire);
    int phys_y = raw_y_.load(std::memory_order_acquire);

    // Rotate 90° clockwise: landscape (800x480) -> portrait (480x800)
    // Portrait x = phys_y (mapped from 0..479 to 0..479)
    // Portrait y = (799 - phys_x) (mapped from 0..799 to 0..799)
    tp.x = phys_y;
    tp.y = (DISPLAY_H - 1) - (phys_x * (DISPLAY_H - 1) / (DISPLAY_PHYS_W - 1));
    tp.pressed = pressed_.load();

    // Clamp
    if (tp.x < 0) tp.x = 0;
    if (tp.x >= DISPLAY_W) tp.x = DISPLAY_W - 1;
    if (tp.y < 0) tp.y = 0;
    if (tp.y >= DISPLAY_H) tp.y = DISPLAY_H - 1;

    return tp;
}

uint64_t TouchInput::last_activity_ms() const {
    return last_activity_.load();
}

void TouchInput::reader_thread() {
    struct input_event ev;
    int cur_x = 0, cur_y = 0;
    bool touching = false;

    while (running_) {
        ssize_t n = ::read(fd_, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            usleep(5000);
            continue;
        }

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                cur_x = ev.value;
                raw_x_.store(cur_x, std::memory_order_release);
            } else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                cur_y = ev.value;
                raw_y_.store(cur_y, std::memory_order_release);
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                touching = (ev.value >= 0);
                pressed_.store(touching);
            }
            last_activity_.store(now_ms());
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            touching = (ev.value > 0);
            pressed_.store(touching);
            last_activity_.store(now_ms());
        }
    }
}

} // namespace cinepi
