#pragma once
/**
 * CinePi Camera - Photo Capture Helper
 * Handles JPEG encoding and flash synchronization.
 */

#include <string>
#include <cstdint>

namespace cinepi {

class GpioDriver;

struct CaptureParams {
    int iso;
    int shutter_us;
    int wb_mode;
    int flash_mode;       // 0=off, 1=on, 2=auto
    float ambient_lux;    // from BH1750
};

class PhotoCapture {
public:
    PhotoCapture();
    ~PhotoCapture();

    // Encode raw buffer to JPEG file
    static bool encode_jpeg(const uint8_t* rgb_data, int width, int height,
                            int stride, int quality, const std::string& output_path);

    // Determine if flash should fire
    static bool should_flash(const CaptureParams& params);

    // Generate timestamped filename
    static std::string generate_filename(const std::string& dir);
};

} // namespace cinepi
