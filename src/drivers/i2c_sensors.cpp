/**
 * CinePi Camera - I2C Sensor Drivers
 * BH1750 Light Sensor (0x23) + L3G4200D Gyroscope (0x69)
 */

#include "drivers/i2c_sensors.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

namespace cinepi {

// ─── BH1750 registers ──────────────────────────────────────────────
static constexpr uint8_t BH1750_POWER_ON       = 0x01;
static constexpr uint8_t BH1750_CONT_HIRES     = 0x10;  // 1 lx resolution, 120ms

// ─── L3G4200D registers ────────────────────────────────────────────
static constexpr uint8_t L3G_WHO_AM_I          = 0x0F;
static constexpr uint8_t L3G_CTRL_REG1         = 0x20;
static constexpr uint8_t L3G_CTRL_REG4         = 0x23;
static constexpr uint8_t L3G_OUT_X_L           = 0x28;
static constexpr float   L3G_SENSITIVITY_250   = 8.75f / 1000.0f;  // deg/s per digit at 250dps

static bool i2c_write_byte(int fd, uint8_t addr, uint8_t val) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) return false;
    return write(fd, &val, 1) == 1;
}

static bool i2c_write_reg(int fd, uint8_t addr, uint8_t reg, uint8_t val) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) return false;
    uint8_t buf[2] = {reg, val};
    return write(fd, buf, 2) == 2;
}

static int i2c_read_bytes(int fd, uint8_t addr, uint8_t reg, uint8_t* buf, int len) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) return -1;
    // Set register address with auto-increment (bit 7 for L3G4200D)
    uint8_t r = reg | 0x80;
    if (write(fd, &r, 1) != 1) return -1;
    return read(fd, buf, len);
}

I2CSensors::I2CSensors() = default;

I2CSensors::~I2CSensors() {
    deinit();
}

bool I2CSensors::init() {
    i2c_fd_ = open(I2C_DEV, O_RDWR);
    if (i2c_fd_ < 0) {
        fprintf(stderr, "[I2C] Failed to open %s: %s\n", I2C_DEV, strerror(errno));
        return false;
    }

    bh1750_ok_ = init_bh1750();
    l3g4200d_ok_ = init_l3g4200d();

    fprintf(stderr, "[I2C] BH1750=%s, L3G4200D=%s\n",
            bh1750_ok_ ? "ok" : "fail",
            l3g4200d_ok_ ? "ok" : "fail");

    return bh1750_ok_ || l3g4200d_ok_;
}

bool I2CSensors::init_bh1750() {
    if (!i2c_write_byte(i2c_fd_, I2C_ADDR_LIGHT, BH1750_POWER_ON)) return false;
    usleep(10000);
    if (!i2c_write_byte(i2c_fd_, I2C_ADDR_LIGHT, BH1750_CONT_HIRES)) return false;
    usleep(180000);  // Wait for first measurement
    return true;
}

bool I2CSensors::init_l3g4200d() {
    // Check WHO_AM_I
    if (ioctl(i2c_fd_, I2C_SLAVE, I2C_ADDR_GYRO) < 0) return false;
    uint8_t who = 0;
    uint8_t reg = L3G_WHO_AM_I;
    if (write(i2c_fd_, &reg, 1) != 1) return false;
    if (read(i2c_fd_, &who, 1) != 1) return false;
    if (who != 0xD3) {
        fprintf(stderr, "[I2C] L3G4200D WHO_AM_I=0x%02X (expected 0xD3)\n", who);
        return false;
    }

    // CTRL_REG1: normal mode, all axes enabled, 100Hz ODR
    i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG1, 0x0F);
    // CTRL_REG4: 250 dps full scale
    i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG4, 0x00);

    return true;
}

void I2CSensors::deinit() {
    stop_polling();
    if (i2c_fd_ >= 0) {
        close(i2c_fd_);
        i2c_fd_ = -1;
    }
}

float I2CSensors::read_lux() {
    if (!bh1750_ok_ || i2c_fd_ < 0) return 0.0f;

    if (ioctl(i2c_fd_, I2C_SLAVE, I2C_ADDR_LIGHT) < 0) return 0.0f;

    uint8_t buf[2] = {};
    if (read(i2c_fd_, buf, 2) != 2) return 0.0f;

    uint16_t raw = (buf[0] << 8) | buf[1];
    return raw / 1.2f;  // BH1750 conversion factor
}

GyroData I2CSensors::read_gyro() {
    GyroData data = {};
    if (!l3g4200d_ok_ || i2c_fd_ < 0) return data;

    uint8_t buf[6] = {};
    if (i2c_read_bytes(i2c_fd_, I2C_ADDR_GYRO, L3G_OUT_X_L, buf, 6) != 6) {
        return data;
    }

    int16_t raw_x = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t raw_y = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t raw_z = (int16_t)(buf[5] << 8 | buf[4]);

    // Convert to degrees/sec, then integrate (simple approximation)
    data.roll  = raw_x * L3G_SENSITIVITY_250;
    data.pitch = raw_y * L3G_SENSITIVITY_250;
    data.yaw   = raw_z * L3G_SENSITIVITY_250;

    return data;
}

GyroData I2CSensors::cached_gyro() const {
    GyroData d;
    d.pitch = gyro_pitch_.load();
    d.roll  = gyro_roll_.load();
    d.yaw   = gyro_yaw_.load();
    return d;
}

bool I2CSensors::has_movement(float threshold_deg) const {
    return gyro_delta_.load() > threshold_deg;
}

void I2CSensors::start_polling() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&I2CSensors::poll_thread, this);
}

void I2CSensors::stop_polling() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void I2CSensors::poll_thread() {
    int light_counter = 0;
    float pitch_acc = 0, roll_acc = 0, yaw_acc = 0;

    while (running_) {
        // Read gyro every ~100ms
        if (l3g4200d_ok_) {
            GyroData g = read_gyro();
            // Simple complementary filter / integration
            float dt = 0.1f;  // 100ms
            pitch_acc += g.pitch * dt;
            roll_acc  += g.roll  * dt;
            yaw_acc   += g.yaw   * dt;

            gyro_pitch_.store(pitch_acc);
            gyro_roll_.store(roll_acc);
            gyro_yaw_.store(yaw_acc);

            // Movement detection: magnitude of angular velocity
            float delta = std::sqrt(g.pitch * g.pitch + g.roll * g.roll + g.yaw * g.yaw);
            gyro_delta_.store(delta);
        }

        // Read light every ~500ms
        light_counter++;
        if (light_counter >= 5 && bh1750_ok_) {
            light_counter = 0;
            float lux = read_lux();
            lux_.store(lux);
        }

        usleep(100000);  // 100ms
    }
}

} // namespace cinepi
