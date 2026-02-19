#pragma once
/**
 * CinePi Camera - libcamera Pipeline
 * Zero-copy preview via DMA-BUF + full-resolution JPEG capture
 */

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace libcamera {
    class CameraManager;
    class Camera;
    class CameraConfiguration;
    class FrameBufferAllocator;
    class Request;
    class FrameBuffer;
    class Stream;
}

namespace cinepi {

class DrmDisplay;

using CaptureCallback = std::function<void(const std::string& path, bool success)>;
using FrameCallback = std::function<void(int dmabuf_fd, int width, int height, int stride, uint32_t format)>;

class CameraPipeline {
public:
    CameraPipeline();
    ~CameraPipeline();

    bool init();
    void deinit();

    // Start/stop preview stream
    bool start_preview();
    void stop_preview();
    bool is_running() const { return running_.load(); }

    // Camera controls
    void set_iso(int iso);
    void set_shutter(int us);
    void set_white_balance(int mode);
    void set_digital_zoom(float factor);  // 1.0 - 4.0

    // Full-res capture
    void capture_photo(const std::string& output_path, CaptureCallback cb);

    // DMA-BUF frame callback for DRM display
    void set_frame_callback(FrameCallback cb);

    std::string get_sensor_name() const;

private:
    void request_complete(libcamera::Request* request);
    void configure_controls();

    std::unique_ptr<libcamera::CameraManager> cm_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::CameraConfiguration> config_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;

    libcamera::Stream* preview_stream_ = nullptr;

    std::atomic<bool> running_{false};
    std::mutex capture_mtx_;
    bool capturing_ = false;
    std::string capture_path_;
    CaptureCallback capture_cb_;
    FrameCallback frame_cb_;

    // Current settings
    int iso_ = 100;
    int shutter_us_ = 8333;
    int wb_mode_ = 0;
    float zoom_ = 1.0f;
};

} // namespace cinepi
