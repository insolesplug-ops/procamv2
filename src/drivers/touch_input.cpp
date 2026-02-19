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

        struct input_absinfo absinfo = {};
        bool has_mt = (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo) == 0) ||
                      (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo) == 0);
        bool has_single = (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) == 0) ||
                          (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) == 0);

        close(fd);

        if (has_mt || has_single) {
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

    // Try exclusive access (non-fatal if unsupported)
    ioctl(fd_, EVIOCGRAB, 1);

    query_abs_ranges();

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

    // Normalize raw touch coordinates using device-reported ABS ranges.
    int norm_x = 0;
    int norm_y = 0;

    int x_range = abs_max_x_ - abs_min_x_;
    int y_range = abs_max_y_ - abs_min_y_;
    if (x_range <= 0) x_range = DISPLAY_PHYS_W - 1;
    if (y_range <= 0) y_range = DISPLAY_PHYS_H - 1;

    norm_x = (phys_x - abs_min_x_) * (DISPLAY_PHYS_W - 1) / x_range;
    norm_y = (phys_y - abs_min_y_) * (DISPLAY_PHYS_H - 1) / y_range;

    // Rotate 90° clockwise: landscape -> portrait
    tp.x = norm_y;
    tp.y = (DISPLAY_H - 1) - norm_x;
    tp.pressed = pressed_.load();

    // Clamp
    if (tp.x < 0) tp.x = 0;
    if (tp.x >= DISPLAY_W) tp.x = DISPLAY_W - 1;
    if (tp.y < 0) tp.y = 0;
    if (tp.y >= DISPLAY_H) tp.y = DISPLAY_H - 1;

    return tp;
}

bool TouchInput::query_abs_ranges() {
    if (fd_ < 0) return false;

    struct input_absinfo abs_x = {};
    struct input_absinfo abs_y = {};

    bool ok_x = false;
    bool ok_y = false;

    if (ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == 0) ok_x = true;
    else if (ioctl(fd_, EVIOCGABS(ABS_X), &abs_x) == 0) ok_x = true;

    if (ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == 0) ok_y = true;
    else if (ioctl(fd_, EVIOCGABS(ABS_Y), &abs_y) == 0) ok_y = true;

    if (ok_x && ok_y) {
        abs_min_x_ = abs_x.minimum;
        abs_max_x_ = abs_x.maximum;
        abs_min_y_ = abs_y.minimum;
        abs_max_y_ = abs_y.maximum;
        fprintf(stderr, "[Touch] ABS ranges X:[%d..%d] Y:[%d..%d]\n",
                abs_min_x_, abs_max_x_, abs_min_y_, abs_max_y_);
        return true;
    }

    fprintf(stderr, "[Touch] Could not query ABS ranges, using defaults\n");
    return false;
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
