#pragma once
/**
 * CinePi Camera - Power Manager
 * Handles standby/wake based on idle detection.
 */

#include <cstdint>
#include <atomic>

namespace cinepi {

class DrmDisplay;
class CameraPipeline;
class TouchInput;
class GpioDriver;
class I2CSensors;
class LvglDriver;

class PowerManager {
public:
    PowerManager();
    ~PowerManager();

    void init(DrmDisplay& display, CameraPipeline& cam,
              TouchInput* touch, GpioDriver& gpio,
              I2CSensors* sensors, LvglDriver& lvgl);

    // Call in main loop - checks idle timeout
    void update();

    bool is_standby() const { return standby_.load(); }
    void wake();
    void sleep();

    void set_timeout(int seconds);

private:
    uint64_t last_activity_ms() const;

    DrmDisplay* display_ = nullptr;
    CameraPipeline* cam_ = nullptr;
    TouchInput* touch_ = nullptr;
    GpioDriver* gpio_ = nullptr;
    I2CSensors* sensors_ = nullptr;
    LvglDriver* lvgl_ = nullptr;

    std::atomic<bool> standby_{false};
    int timeout_sec_ = 10;
    int saved_brightness_ = 128;
};

} // namespace cinepi
