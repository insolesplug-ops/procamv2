#pragma once
/**
 * CinePi Camera - Capacitive Touch Input Driver
 * Reads from /dev/input/eventX, rotates coordinates for portrait mode.
 */

#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <string>

namespace cinepi {

struct TouchPoint {
    int x = 0;
    int y = 0;
    bool pressed = false;
};

class TouchInput {
public:
    TouchInput();
    ~TouchInput();

    bool init();
    void deinit();

    // Get current touch state (thread-safe)
    TouchPoint read();

    // Last activity timestamp (for standby detection)
    uint64_t last_activity_ms() const;

private:
    void reader_thread();
    std::string find_touch_device();

    int fd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<int> raw_x_{0};
    std::atomic<int> raw_y_{0};
    std::atomic<bool> pressed_{false};
    std::atomic<uint64_t> last_activity_{0};
};

} // namespace cinepi
