# CinePi Camera - Detailed Fixes Guide

## Critical Fixes (Must Do First)

---

## FIX 1: DRM Display Resource Leak in find_crtc()

**File**: [src/drivers/drm_display.cpp](src/drivers/drm_display.cpp#L88-110)
**Priority**: CRITICAL
**Effort**: 5 minutes

### Current Code (BUGGY):
```cpp
bool DrmDisplay::find_crtc() {
    drmModeRes* res = drmModeGetResources(drm_fd_);
    if (!res) {
        fprintf(stderr, "[DRM] drmModeGetResources failed\n");
        return false;
    }

    drmModeConnector* conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_id_ = conn->connector_id;
            break;
        }
        if (conn) {
            drmModeFreeConnector(conn);
            conn = nullptr;
        }
    }

    if (!conn) {
        fprintf(stderr, "[DRM] No connected display found\n");
        drmModeFreeResources(res);  // ← Only freed on this path, not on early returns below!
        return false;
    }
    // ... more code follows ...
```

### Fixed Code:
```cpp
bool DrmDisplay::find_crtc() {
    drmModeRes* res = drmModeGetResources(drm_fd_);
    if (!res) {
        fprintf(stderr, "[DRM] drmModeGetResources failed\n");
        return false;
    }

    // RAII approach: ensure cleanup on all exit paths
    auto res_cleanup = [&]() { if (res) drmModeFreeResources(res); };
    
    drmModeConnector* conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_id_ = conn->connector_id;
            break;
        }
        if (conn) {
            drmModeFreeConnector(conn);
            conn = nullptr;
        }
    }

    if (!conn) {
        fprintf(stderr, "[DRM] No connected display found\n");
        res_cleanup();
        return false;
    }

    // ... rest of function ...
    // At ALL exit points, call res_cleanup() before return
    if (!crtc_id_) {
        fprintf(stderr, "[DRM] No CRTC available\n");
        drmModeFreeConnector(conn);
        res_cleanup();
        return false;
    }

    // ... more setup ...

    drmModeFreeConnector(conn);
    res_cleanup();  // Make sure to clean up!
    return true;
}
```

### Alternative (Simpler):
```cpp
// Use a simple scope guard or ensure every return path frees res
defer_free_resources {
    drmModeFreeResources(res);
};  // Calls cleanup on scope exit
```

---

## FIX 2: LVGL Buffer Leak on Allocation Failure

**File**: [src/ui/lvgl_driver.cpp](src/ui/lvgl_driver.cpp#L38-48)
**Priority**: CRITICAL
**Effort**: 5 minutes

### Current Code (BUGGY):
```cpp
bool LvglDriver::init(DrmDisplay& display, TouchInput* touch) {
    // ...
    uint32_t buf_size = DISPLAY_W * LVGL_BUF_LINES;
    buf1_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    buf2_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    if (!buf1_ || !buf2_) {
        fprintf(stderr, "[LVGL] Buffer allocation failed\n");
        return false;  // ← buf1_ is leaked if buf2_ allocation failed!
    }
    // ...
}
```

### Fixed Code:
```cpp
bool LvglDriver::init(DrmDisplay& display, TouchInput* touch) {
    // ...
    uint32_t buf_size = DISPLAY_W * LVGL_BUF_LINES;
    
    // Allocate and check separately
    buf1_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    if (!buf1_) {
        fprintf(stderr, "[LVGL] buf1_ allocation failed\n");
        return false;
    }
    
    buf2_ = static_cast<void*>(malloc(buf_size * sizeof(lv_color_t)));
    if (!buf2_) {
        fprintf(stderr, "[LVGL] buf2_ allocation failed\n");
        free(buf1_);  // ← Clean up buf1_ if buf2_ fails
        buf1_ = nullptr;
        return false;
    }
    // ...
}
```

---

## FIX 3: Camera Request Queue Leak

**File**: [src/camera/camera_pipeline.cpp](src/camera/camera_pipeline.cpp#L113-125)
**Priority**: CRITICAL
**Effort**: 10 minutes

### Current Code (BUGGY):
```cpp
bool CameraPipeline::start_preview() {
    if (running_ || !camera_) return false;

    camera_->requestCompleted.connect(this, &CameraPipeline::request_complete);

    if (camera_->start() != 0) {
        fprintf(stderr, "[Camera] Failed to start\n");
        return false;
    }

    const auto& buffers = allocator_->buffers(preview_stream_);
    for (auto& buf : buffers) {
        std::unique_ptr<Request> request = camera_->createRequest();
        if (!request) continue;
        request->addBuffer(preview_stream_, buf.get());
        configure_controls();
        camera_->queueRequest(request.release());  // ← No error check!
    }

    running_ = true;
    fprintf(stderr, "[Camera] Preview started\n");
    return true;
}
```

### Fixed Code:
```cpp
bool CameraPipeline::start_preview() {
    if (running_ || !camera_) return false;

    camera_->requestCompleted.connect(this, &CameraPipeline::request_complete);

    if (camera_->start() != 0) {
        fprintf(stderr, "[Camera] Failed to start\n");
        return false;
    }

    const auto& buffers = allocator_->buffers(preview_stream_);
    int queued_count = 0;
    
    for (auto& buf : buffers) {
        std::unique_ptr<Request> request = camera_->createRequest();
        if (!request) {
            fprintf(stderr, "[Camera] Failed to create request\n");
            continue;
        }
        
        request->addBuffer(preview_stream_, buf.get());
        configure_controls();
        
        // Check return value of queueRequest
        int ret = camera_->queueRequest(request.get());  // Don't release yet!
        if (ret != 0) {
            fprintf(stderr, "[Camera] Failed to queue request: %d\n", ret);
            // request will be destroyed here when going out of scope
            continue;
        }
        
        // Only release ownership if queueRequest succeeded
        request.release();
        queued_count++;
    }

    // Ensure we queued at least one request
    if (queued_count == 0) {
        fprintf(stderr, "[Camera] No requests queued\n");
        camera_->stop();
        return false;
    }

    running_ = true;
    fprintf(stderr, "[Camera] Preview started with %d requests\n", queued_count);
    return true;
}
```

---

## FIX 4: Scene Manager - Null Pointer Crash

**File**: [src/ui/scene_manager.cpp](src/ui/scene_manager.cpp#L70-78)
**Priority**: CRITICAL
**Effort**: 5 minutes

### Current Code (BUGGY):
```cpp
void SceneManager::update_status_bar() {
    if (!ui_INFOSONSCREEN) return;

    char buf[64];

    // Time
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    // CRASH HERE: if localtime returns nullptr:
    snprintf(buf, sizeof(buf), "%02d:%02d   %.1fGB   %d%%",
             t->tm_hour, t->tm_min, free_gb, battery_pct);  // ← SEGFAULT!
```

### Fixed Code:
```cpp
void SceneManager::update_status_bar() {
    if (!ui_INFOSONSCREEN) return;

    char buf[64];

    // Time
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    
    // Safety check for localtime failure
    if (!t) {
        fprintf(stderr, "[SceneManager] localtime failed\n");
        snprintf(buf, sizeof(buf), "??:??   ?.?GB   ?%%");
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d   %.1fGB   %d%%",
                 t->tm_hour, t->tm_min, free_gb, battery_pct);
    }

    // ... rest of function ...
}
```

---

## High Priority Fixes

---

## FIX 5: GPIO Callback Thread Safety

**File**: [src/drivers/gpio_driver.h](src/drivers/gpio_driver.h)
**Priority**: HIGH
**Effort**: 20 minutes

### Add to Header:
```cpp
#pragma once
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>  // ← Add this

struct gpiod_chip;
struct gpiod_line;

namespace cinepi {

using ButtonCallback = std::function<void()>;
using EncoderCallback = std::function<void(int direction)>;

class GpioDriver {
public:
    GpioDriver();
    ~GpioDriver();

    bool init();
    void deinit();

    // Set callbacks - NOW WITH MUTEX PROTECTION
    void on_shutter(ButtonCallback cb);
    void on_encoder_button(ButtonCallback cb);
    void on_encoder_rotate(EncoderCallback cb);

    // Outputs
    void set_flash(bool on);
    void vibrate(int duration_ms);

    uint64_t last_activity_ms() const;

private:
    void poll_thread();

    gpiod_chip* chip_ = nullptr;
    // ... line definitions ...

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> last_activity_{0};

    // Add mutex for callback safety
    mutable std::mutex callback_mtx_;  // ← NEW
    ButtonCallback shutter_cb_;
    ButtonCallback enc_btn_cb_;
    EncoderCallback enc_rot_cb_;

    int enc_last_clk_ = 1;
};

} // namespace cinepi
```

### Update Implementation in gpio_driver.cpp:
```cpp
void GpioDriver::on_shutter(ButtonCallback cb) {
    std::lock_guard<std::mutex> lk(callback_mtx_);  // ← Add lock
    shutter_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_button(ButtonCallback cb) {
    std::lock_guard<std::mutex> lk(callback_mtx_);  // ← Add lock
    enc_btn_cb_ = std::move(cb);
}

void GpioDriver::on_encoder_rotate(EncoderCallback cb) {
    std::lock_guard<std::mutex> lk(callback_mtx_);  // ← Add lock
    enc_rot_cb_ = std::move(cb);
}

void GpioDriver::poll_thread() {
    // ... polling code ...
    
    if (shutter_line_ && /* button pressed */) {
        ButtonCallback cb;
        {
            std::lock_guard<std::mutex> lk(callback_mtx_);  // ← Acquire lock
            cb = shutter_cb_;  // ← Copy callback
        }  // ← Release lock before invoking
        
        if (cb) cb();  // ← Safe to invoke after releasing lock
    }
}
```

---

## FIX 6: DRM Plane Error Handling

**File**: [src/drivers/drm_display.cpp](src/drivers/drm_display.cpp#L304-340)
**Priority**: HIGH
**Effort**: 15 minutes

### Current Code (BUGGY):
```cpp
bool DrmDisplay::set_camera_dmabuf(int dmabuf_fd, int width, int height,
                                     int stride, uint32_t format) {
    if (drm_fd_ < 0) return false;

    // ... setup code ...

    if (drmModeAddFB2(drm_fd_, width, height, format,
                       handles, strides, offsets,
                       &camera_fb_id_, 0) != 0) {
        fprintf(stderr, "[DRM] drmModeAddFB2 failed: %s\n", strerror(errno));
        // Cleanup
        struct drm_gem_close gem_close = {};
        gem_close.handle = gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
        s_camera_imported_gem_handle = 0;
        return false;
    }

    // Set on primary plane
    uint32_t primary_plane_id = find_plane_for_layer(0);
    if (primary_plane_id) {
        drmModeSetPlane(drm_fd_, primary_plane_id, crtc_id_,
                        camera_fb_id_, 0,
                        0, 0, DISPLAY_PHYS_W, DISPLAY_PHYS_H,
                        0, 0, width << 16, height << 16);  // ← NO ERROR CHECK!
    } else {
        drmModeSetCrtc(drm_fd_, crtc_id_, camera_fb_id_, 0, 0,
                        &connector_id_, 1, nullptr);  // ← NO ERROR CHECK!
    }

    return true;  // ← Always returns true!
}
```

### Fixed Code:
```cpp
bool DrmDisplay::set_camera_dmabuf(int dmabuf_fd, int width, int height,
                                     int stride, uint32_t format) {
    if (drm_fd_ < 0) return false;

    // ... setup code ...

    if (drmModeAddFB2(drm_fd_, width, height, format,
                       handles, strides, offsets,
                       &camera_fb_id_, 0) != 0) {
        fprintf(stderr, "[DRM] drmModeAddFB2 failed: %s\n", strerror(errno));
        // Cleanup
        struct drm_gem_close gem_close = {};
        gem_close.handle = gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
        s_camera_imported_gem_handle = 0;
        return false;
    }

    // Set on primary plane - NOW WITH ERROR CHECKING
    uint32_t primary_plane_id = find_plane_for_layer(0);
    if (primary_plane_id) {
        int ret = drmModeSetPlane(drm_fd_, primary_plane_id, crtc_id_,
                                  camera_fb_id_, 0,
                                  0, 0, DISPLAY_PHYS_W, DISPLAY_PHYS_H,
                                  0, 0, width << 16, height << 16);
        if (ret != 0) {
            fprintf(stderr, "[DRM] drmModeSetPlane failed: %s\n", strerror(errno));
            // Clean up framebuffer
            drmModeRmFB(drm_fd_, camera_fb_id_);
            camera_fb_id_ = 0;
            // Clean up imported handle
            struct drm_gem_close gem_close = {};
            gem_close.handle = gem_handle;
            drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
            s_camera_imported_gem_handle = 0;
            return false;
        }
    } else {
        int ret = drmModeSetCrtc(drm_fd_, crtc_id_, camera_fb_id_, 0, 0,
                                  &connector_id_, 1, nullptr);
        if (ret != 0) {
            fprintf(stderr, "[DRM] drmModeSetCrtc failed: %s\n", strerror(errno));
            drmModeRmFB(drm_fd_, camera_fb_id_);
            camera_fb_id_ = 0;
            struct drm_gem_close gem_close = {};
            gem_close.handle = gem_handle;
            drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
            s_camera_imported_gem_handle = 0;
            return false;
        }
    }

    return true;  // ← Only if all operations succeeded
}
```

---

## FIX 7: Config Manager Thread Safety

**File**: [src/core/config.h](src/core/config.h)
**Priority**: HIGH
**Effort**: 10 minutes

### Updated Header:
```cpp
#pragma once
#include <string>
#include <mutex>
#include <cstdint>

namespace cinepi {

struct CameraSettings {
    int iso           = 100;
    int shutter_us    = 8333;
    int wb_mode       = 0;
    bool grid_overlay = false;
    bool digital_level = false;
    int flash_mode    = 0;
    float colour_temp = 0.5f;
};

struct DisplaySettings {
    int brightness     = 128;
    int standby_sec    = 10;
};

struct AppConfig {
    CameraSettings camera;
    DisplaySettings display;
    std::string photo_dir   = "/home/pi/photos";
    std::string config_path = "/home/pi/.cinepi_config.json";
    std::string version     = "1.0.0";
};

class ConfigManager {
public:
    static ConfigManager& instance();

    bool load(const std::string& path = "");
    bool save();

    // Thread-safe getters - return copy with lock
    AppConfig get_safe() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return config_;
    }
    
    // For convenience, keep old API but document thread requirements
    AppConfig& get();
    const AppConfig& get() const;

    void update_camera(const CameraSettings& s);
    void update_display(const DisplaySettings& s);

private:
    ConfigManager() = default;
    AppConfig config_;
    mutable std::mutex mtx_;
};

// ... rest of header ...
} // namespace cinepi
```

### Updated Config Implementation:
```cpp
AppConfig ConfigManager::get_safe() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return config_;  // Returns copy while locked
}

// For reads without locks (if config is seldom modified):
const AppConfig& ConfigManager::get() const {
    return config_;  // Document that this is not protected
}
```

Then in places that read config (like scene_manager.cpp callbacks):
```cpp
// Instead of:
auto& cfg = ConfigManager::instance().get();
cfg.camera.iso = value;

// Use:
AppConfig cfg = ConfigManager::instance().get_safe();
cfg.camera.iso = value;
ConfigManager::instance().save();
```

---

## FIX 8: I2C Sensor Error Checking

**File**: [src/drivers/i2c_sensors.cpp](src/drivers/i2c_sensors.cpp#L84-106)
**Priority**: HIGH
**Effort**: 15 minutes

### Current Code (BUGGY):
```cpp
bool I2CSensors::init_l3g4200d() {
    if (ioctl(i2c_fd_, I2C_SLAVE, I2C_ADDR_GYRO) < 0) return false;
    uint8_t who = 0;
    uint8_t reg = L3G_WHO_AM_I;
    if (write(i2c_fd_, &reg, 1) != 1) return false;
    if (read(i2c_fd_, &who, 1) != 1) return false;
    if (who != 0xD3) {
        fprintf(stderr, "[I2C] L3G4200D WHO_AM_I=0x%02X (expected 0xD3)\n", who);
        return false;
    }

    // NO ERROR CHECKING ON THESE:
    i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG1, 0x0F);
    i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG4, 0x00);

    return true;
}
```

### Fixed Code:
```cpp
bool I2CSensors::init_l3g4200d() {
    if (ioctl(i2c_fd_, I2C_SLAVE, I2C_ADDR_GYRO) < 0) {
        fprintf(stderr, "[I2C] Failed to set I2C slave address 0x%02x\n", I2C_ADDR_GYRO);
        return false;
    }
    
    uint8_t who = 0;
    uint8_t reg = L3G_WHO_AM_I;
    if (write(i2c_fd_, &reg, 1) != 1) {
        fprintf(stderr, "[I2C] Failed to write WHO_AM_I register\n");
        return false;
    }
    
    if (read(i2c_fd_, &who, 1) != 1) {
        fprintf(stderr, "[I2C] Failed to read WHO_AM_I\n");
        return false;
    }
    
    if (who != 0xD3) {
        fprintf(stderr, "[I2C] L3G4200D WHO_AM_I=0x%02X (expected 0xD3)\n", who);
        return false;
    }

    // NOW WITH ERROR CHECKING:
    if (!i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG1, 0x0F)) {
        fprintf(stderr, "[I2C] Failed to configure L3G CTRL_REG1\n");
        return false;
    }
    
    if (!i2c_write_reg(i2c_fd_, I2C_ADDR_GYRO, L3G_CTRL_REG4, 0x00)) {
        fprintf(stderr, "[I2C] Failed to configure L3G CTRL_REG4\n");
        return false;
    }

    return true;
}
```

---

## FIX 9: Photo Capture - Disk Write Verification

**File**: [src/camera/photo_capture.cpp](src/camera/photo_capture.cpp#L40-55)
**Priority**: HIGH
**Effort**: 10 minutes

### Current Code (BUGGY):
```cpp
bool PhotoCapture::encode_jpeg(const uint8_t* rgb_data, int width, int height,
                                int stride, int quality, const std::string& output_path) {
    // ... compression code ...

    FILE* fp = fopen(output_path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[Capture] Cannot open %s for writing\n", output_path.c_str());
        tjFree(jpeg_buf);
        tjDestroy(handle);
        return false;
    }

    fwrite(jpeg_buf, 1, jpeg_size, fp);  // ← No return value check!
    fclose(fp);

    fprintf(stderr, "[Capture] Saved %s (%dx%d, %lu bytes)\n",
            output_path.c_str(), width, height, jpeg_size);

    tjFree(jpeg_buf);
    tjDestroy(handle);
    return true;  // ← Always true!
}
```

### Fixed Code:
```cpp
bool PhotoCapture::encode_jpeg(const uint8_t* rgb_data, int width, int height,
                                int stride, int quality, const std::string& output_path) {
    // ... compression code ...

    FILE* fp = fopen(output_path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[Capture] Cannot open %s for writing: %s\n", 
                output_path.c_str(), strerror(errno));
        tjFree(jpeg_buf);
        tjDestroy(handle);
        return false;
    }

    // Check write result
    size_t written = fwrite(jpeg_buf, 1, jpeg_size, fp);
    if (written != jpeg_size) {
        fprintf(stderr, "[Capture] Write failed: wrote %zu/%lu bytes to %s\n",
                written, jpeg_size, output_path.c_str());
        fclose(fp);
        tjFree(jpeg_buf);
        tjDestroy(handle);
        return false;
    }

    // Check close result for write errors
    if (fclose(fp) != 0) {
        fprintf(stderr, "[Capture] Failed to close %s: %s\n", 
                output_path.c_str(), strerror(errno));
        tjFree(jpeg_buf);
        tjDestroy(handle);
        return false;
    }

    fprintf(stderr, "[Capture] Saved %s (%dx%d, %lu bytes)\n",
            output_path.c_str(), width, height, jpeg_size);

    tjFree(jpeg_buf);
    tjDestroy(handle);
    return true;  // ← Only if everything succeeded
}
```

---

## Performance Optimizations (Medium Priority)

---

## FIX 10: LVGL Buffer Size Optimization

**File**: [include/core/constants.h](include/core/constants.h#L27)
**Priority**: MEDIUM (Performance)
**Effort**: 5 minutes + testing

### Current:
```cpp
constexpr int LVGL_BUF_LINES    = 40;    // 480 * 40 * 4 bytes = 76.8KB per buffer
```

### Recommendation:
```cpp
// For RPi 3A+ with 512MB RAM:
// Current: 480 * 40 * 4 * 2 = 153.6KB total (too much)
// Optimized: 480 * 20 * 4 * 2 = 76.8KB total (acceptable)
// Further optimized: 480 * 15 * 4 * 2 = 57.6KB (minimum safe)
constexpr int LVGL_BUF_LINES    = 20;    // Reduced from 40
```

### Testing:
```bash
# Monitor frame rate and mem usage:
top -b -n 1 | grep cinepi
# Check for frame drops in logs
grep "FPS:" cinepi.log
```

---

## FIX 11: Scene Manager - Async Status Bar Updates

**File**: [src/ui/scene_manager.cpp](src/ui/scene_manager.cpp#L59-95)
**Priority**: MEDIUM (Performance)
**Effort**: 30 minutes

### Current (Blocking):
```cpp
void SceneManager::update() {
    frame_count_++;
    
    if (frame_count_ % 30 == 0) {
        update_status_bar();  // ← Blocking I/O in render thread!
    }
}

void SceneManager::update_status_bar() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    
    struct statvfs stat;
    statvfs(cfg.photo_dir.c_str(), &stat);  // ← Filesystem syscall
    
    FILE* fp = fopen(BATTERY_PATH, "r");  // ← I/O syscall
    if (fp) {
        fscanf(fp, "%d", &battery_pct);
        fclose(fp);
    }
    
    // Update UI (fast)
    lv_label_set_text(label, buf);
}
```

### Optimized (Async):
```cpp
// In header:
class SceneManager {
private:
    std::atomic<float> cached_free_gb_{0.f};
    std::atomic<int> cached_battery_pct_{100};
    std::atomic<uint64_t> last_stat_update_ms_{0};
    
    void async_update_status();  // Does I/O in background
};

// In implementation:
void SceneManager::update() {
    frame_count_++;
    
    // Only update UI label from cached values (fast)
    if (frame_count_ % 30 == 0) {
        update_status_bar_from_cache();
    }
    
    // Periodically refresh cache asynchronously
    if (frame_count_ % 300 == 0) {  // Every 10 seconds
        // Queue async update (don't wait for it)
        std::thread t([this]() { async_update_status(); });
        t.detach();  // Or use thread pool
    }
}

void SceneManager::async_update_status() {
    // This runs in background thread
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    if (t) {
        // cache time
    }
    
    struct statvfs stat;
    if (statvfs(cfg.photo_dir.c_str(), &stat) == 0) {
        float free_gb = (float)(stat.f_bavail * stat.f_frsize) / (1024.0f * 1024.0f * 1024.0f);
        cached_free_gb_.store(free_gb);
    }
    
    FILE* fp = fopen(BATTERY_PATH, "r");
    if (fp) {
        int pct = 100;
        if (fscanf(fp, "%d", &pct) == 1) {
            cached_battery_pct_.store(pct);
        }
        fclose(fp);
    }
    
    last_stat_update_ms_.store(now_ms());
}

void SceneManager::update_status_bar_from_cache() {
    if (!ui_INFOSONSCREEN) return;
    
    char buf[64];
    float free_gb = cached_free_gb_.load();
    int battery_pct = cached_battery_pct_.load();
    
    snprintf(buf, sizeof(buf), "%.1fGB   %d%%", free_gb, battery_pct);
    
    lv_obj_t* label = find_or_create_status_label();
    lv_label_set_text(label, buf);
}
```

---

## FIX 12: GPIO Polling Optimization

**File**: [src/drivers/gpio_driver.cpp](src/drivers/gpio_driver.cpp#L180+)
**Priority**: MEDIUM (Performance)
**Effort**: 20 minutes

### Current (Polls all buttons every 10ms):
```cpp
void GpioDriver::poll_thread() {
    while (running_) {
        // Read shutter button
        if (shutter_line_) {
            int val = gpiod_line_get_value(shutter_line_);  // syscall
        }
        
        // Read encoder pins...
        if (enc_clk_line_) {
            int val = gpiod_line_get_value(enc_clk_line_);  // syscall
        }
        // ... more pins ...
        
        usleep(10000);  // Only 10ms sleep = 100 polls/sec
    }
}
```

### Optimized (Increase sleep interval):
```cpp
constexpr int GPIO_POLL_MS = 50;  // Increase to 50ms = 20 polls/sec

void GpioDriver::poll_thread() {
    while (running_) {
        if (shutter_line_) {
            int val = gpiod_line_get_value(shutter_line_);
            // ... handle press ...
        }
        
        if (enc_clk_line_) {
            int val = gpiod_line_get_value(enc_clk_line_);
            // ... handle rotation ...
        }
        
        usleep(GPIO_POLL_MS * 1000);  // 50ms = reduced syscalls
    }
}
```

**Note**: Consider interrupt-driven GPIO in future (not polling).

---

## Testing Checklist

```bash
# After applying critical fixes:

# 1. Memory leak check
valgrind --leak-check=full ./cinepi 2>&1 | grep -i "definitely lost"

# 2. Thread safety
helgrind --tool=helgrind ./cinepi 2>&1 | grep "race"

# 3. Basic functionality test
./cinepi &
sleep 5
kill %1
# Check no crashes or hangs

# 4. Performance baseline
time ./cinepi --run-for 30s
# Check FPS and memory usage

# 5. JPEG write verification
ls -lh /home/pi/photos/*.jpg
file /home/pi/photos/*.jpg  # Should show JPEG format
```

