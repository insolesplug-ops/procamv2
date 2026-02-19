/**
 * CinePi Camera - Hardware Health Monitor
 */

#pragma once

#include <cstdint>
#include <string>
#include <map>

namespace cinepi {

enum class HardwareComponent {
    Camera,
    Display,
    TouchInput,
    GPIOButtons,
    I2CSensors,
    Vibration,
    Flash,
};

enum class HardwareStatus {
    OK,
    Degraded,
    Failed,
};

class HardwareHealth {
public:
    HardwareHealth();
    ~HardwareHealth() = default;

    bool init();
    bool is_available(HardwareComponent component) const;
    HardwareStatus get_status(HardwareComponent component) const;
    bool is_critical_ok() const;
    std::string get_status_string(HardwareComponent component) const;
    std::string get_full_status() const;
    void set_status(HardwareComponent component, HardwareStatus status);
    void log_status() const;

private:
    std::map<HardwareComponent, HardwareStatus> status_;
    
    bool check_camera();
    bool check_display();
    bool check_touch();
    bool check_gpio();
    bool check_i2c();
    
    std::string component_name(HardwareComponent component) const;
};

} // namespace cinepi