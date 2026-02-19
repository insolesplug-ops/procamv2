#pragma once
/**
 * CinePi Camera - Photo Manager
 * Coordinates capture flow: flash -> capture -> save -> vibrate.
 */

#include <string>
#include <functional>

namespace cinepi {

class CameraPipeline;
class GpioDriver;
class I2CSensors;

class PhotoManager {
public:
    PhotoManager();
    ~PhotoManager();

    void init(CameraPipeline& cam, GpioDriver& gpio, I2CSensors& sensors);

    // Trigger a capture (called from shutter button or UI)
    void trigger_capture();

    // Get last captured file path
    const std::string& last_photo() const { return last_path_; }

    // Set callback for capture complete
    using DoneCallback = std::function<void(bool success, const std::string& path)>;
    void on_capture_done(DoneCallback cb);

private:
    CameraPipeline* cam_ = nullptr;
    GpioDriver* gpio_ = nullptr;
    I2CSensors* sensors_ = nullptr;

    std::string last_path_;
    DoneCallback done_cb_;
    bool capturing_ = false;
};

} // namespace cinepi
