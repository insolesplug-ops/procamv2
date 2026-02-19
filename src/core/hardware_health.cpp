#include "core/hardware_health.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/i2c-dev.h>
#endif
#include <gpiod.h>
#include <libcamera/libcamera.h>
#include <dirent.h>
#ifdef __linux__
#include <linux/input.h>
#endif

namespace cinepi {

HardwareHealth::HardwareHealth() {
    status_[HardwareComponent::Camera] = HardwareStatus::Failed;
    status_[HardwareComponent::Display] = HardwareStatus::Failed;
    status_[HardwareComponent::TouchInput] = HardwareStatus::Failed;
    status_[HardwareComponent::GPIOButtons] = HardwareStatus::Failed;
    status_[HardwareComponent::I2CSensors] = HardwareStatus::Failed;
    status_[HardwareComponent::Vibration] = HardwareStatus::Failed;
    status_[HardwareComponent::Flash] = HardwareStatus::Failed;
}

bool HardwareHealth::init() {
    fprintf(stderr, "\n[Hardware] Running diagnostics...\n");
    
    bool camera_ok = check_camera();
    bool display_ok = check_display();
    
    check_touch();
    check_gpio();
    check_i2c();
    
    log_status();
    
    if (!camera_ok) {
        fprintf(stderr, "[Hardware] CRITICAL: Camera not available\n");
        return false;
    }
    
    if (!display_ok) {
        fprintf(stderr, "[Hardware] CRITICAL: Display not available\n");
        return false;
    }
    
    fprintf(stderr, "[Hardware] Diagnostics complete\n\n");
    return true;
}

bool HardwareHealth::check_camera() {
    try {
        using namespace libcamera;
        auto cm = std::make_unique<CameraManager>();
        if (cm->start() != 0) {
            status_[HardwareComponent::Camera] = HardwareStatus::Failed;
            return false;
        }
        
        auto cameras = cm->cameras();
        if (cameras.empty()) {
            status_[HardwareComponent::Camera] = HardwareStatus::Failed;
            cm->stop();
            return false;
        }
        
        status_[HardwareComponent::Camera] = HardwareStatus::OK;
        cm->stop();
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[Hardware] Camera exception: %s\n", e.what());
        status_[HardwareComponent::Camera] = HardwareStatus::Failed;
        return false;
    }
}

bool HardwareHealth::check_display() {
    const char* drm_paths[] = {"/dev/dri/card1", "/dev/dri/card0"};
    
    for (auto path : drm_paths) {
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            close(fd);
            status_[HardwareComponent::Display] = HardwareStatus::OK;
            return true;
        }
    }
    
    status_[HardwareComponent::Display] = HardwareStatus::Failed;
    return false;
}

bool HardwareHealth::check_touch() {
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        status_[HardwareComponent::TouchInput] = HardwareStatus::Failed;
        return false;
    }
    
    bool found = false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && !found) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        
        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        unsigned long abs_bits[(ABS_MAX + 1) / (sizeof(long) * 8) + 1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        
        bool has_mt_x = (abs_bits[ABS_MT_POSITION_X / (sizeof(long) * 8)] >>
                         (ABS_MT_POSITION_X % (sizeof(long) * 8))) & 1;
        
        close(fd);
        
        if (has_mt_x) {
            status_[HardwareComponent::TouchInput] = HardwareStatus::OK;
            found = true;
        }
    }
    closedir(dir);
    
    if (!found) {
        status_[HardwareComponent::TouchInput] = HardwareStatus::Failed;
        return false;
    }
    
    return true;
}

bool HardwareHealth::check_gpio() {
    gpiod_chip* chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) {
        status_[HardwareComponent::GPIOButtons] = HardwareStatus::Failed;
        return false;
    }
    
    status_[HardwareComponent::GPIOButtons] = HardwareStatus::OK;
    gpiod_chip_close(chip);
    return true;
}

bool HardwareHealth::check_i2c() {
    int i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0) {
        status_[HardwareComponent::I2CSensors] = HardwareStatus::Failed;
        return false;
    }
    
    close(i2c_fd);
    status_[HardwareComponent::I2CSensors] = HardwareStatus::OK;
    return true;
}

bool HardwareHealth::is_available(HardwareComponent component) const {
    auto it = status_.find(component);
    if (it == status_.end()) return false;
    return it->second != HardwareStatus::Failed;
}

HardwareStatus HardwareHealth::get_status(HardwareComponent component) const {
    auto it = status_.find(component);
    if (it == status_.end()) return HardwareStatus::Failed;
    return it->second;
}

bool HardwareHealth::is_critical_ok() const {
    return (get_status(HardwareComponent::Camera) != HardwareStatus::Failed) &&
           (get_status(HardwareComponent::Display) != HardwareStatus::Failed);
}

std::string HardwareHealth::component_name(HardwareComponent component) const {
    switch (component) {
        case HardwareComponent::Camera:       return "Camera";
        case HardwareComponent::Display:      return "Display";
        case HardwareComponent::TouchInput:   return "Touch Input";
        case HardwareComponent::GPIOButtons:  return "GPIO Buttons";
        case HardwareComponent::I2CSensors:   return "I2C Sensors";
        case HardwareComponent::Vibration:    return "Vibration Motor";
        case HardwareComponent::Flash:        return "LED Flash";
        default:                              return "Unknown";
    }
}

std::string HardwareHealth::get_status_string(HardwareComponent component) const {
    HardwareStatus status = get_status(component);
    std::string name = component_name(component);
    
    std::string critical = "";
    if (component == HardwareComponent::Camera || 
        component == HardwareComponent::Display) {
        critical = " (CRITICAL)";
    } else {
        critical = " (optional)";
    }
    
    switch (status) {
        case HardwareStatus::OK:
            return "✓ " + name + critical;
        case HardwareStatus::Degraded:
            return "⚠ " + name + critical + " [Degraded]";
        case HardwareStatus::Failed:
            return "✗ " + name + critical + " [Failed]";
        default:
            return "? " + name + critical;
    }
}

std::string HardwareHealth::get_full_status() const {
    std::string result = "\n[Hardware Status]\n";
    result += get_status_string(HardwareComponent::Camera) + "\n";
    result += get_status_string(HardwareComponent::Display) + "\n";
    result += get_status_string(HardwareComponent::TouchInput) + "\n";
    result += get_status_string(HardwareComponent::GPIOButtons) + "\n";
    result += get_status_string(HardwareComponent::I2CSensors) + "\n";
    result += get_status_string(HardwareComponent::Vibration) + "\n";
    result += get_status_string(HardwareComponent::Flash) + "\n";
    return result;
}

void HardwareHealth::set_status(HardwareComponent component, HardwareStatus status) {
    status_[component] = status;
}

void HardwareHealth::log_status() const {
    fprintf(stderr, "%s\n", get_full_status().c_str());
}

} // namespace cinepi