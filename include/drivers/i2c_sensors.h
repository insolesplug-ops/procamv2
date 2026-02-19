#pragma once
/**
 * CinePi Camera - I2C Sensor Drivers
 * BH1750 Light Sensor + L3G4200D Gyroscope
 */

#include <cstdint>
#include <atomic>
#include <thread>

namespace cinepi {

struct GyroData {
    float pitch = 0.0f;  // degrees
    float roll  = 0.0f;  // degrees
    float yaw   = 0.0f;  // degrees
};

class I2CSensors {
public:
    I2CSensors();
    ~I2CSensors();

    bool init();
    void deinit();

    // Light sensor (BH1750)
    float read_lux();

    // Gyroscope (L3G4200D)
    GyroData read_gyro();

    // Continuous background reading
    void start_polling();
    void stop_polling();

    // Cached values (thread-safe)
    float cached_lux() const { return lux_.load(); }
    GyroData cached_gyro() const;

    // Activity detection (gyro movement)
    bool has_movement(float threshold_deg = 5.0f) const;

private:
    bool init_bh1750();
    bool init_l3g4200d();
    void poll_thread();

    int i2c_fd_ = -1;
    bool bh1750_ok_ = false;
    bool l3g4200d_ok_ = false;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<float> lux_{0.0f};
    std::atomic<float> gyro_pitch_{0.0f};
    std::atomic<float> gyro_roll_{0.0f};
    std::atomic<float> gyro_yaw_{0.0f};
    std::atomic<float> gyro_delta_{0.0f};
};

} // namespace cinepi
