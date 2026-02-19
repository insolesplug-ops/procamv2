/**
 * CinePi Camera - Photo Capture Helper
 * JPEG encoding via libjpeg-turbo
 */

#include "camera/photo_capture.h"
#include "core/constants.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <turbojpeg.h>

namespace cinepi {

PhotoCapture::PhotoCapture() = default;
PhotoCapture::~PhotoCapture() = default;

bool PhotoCapture::encode_jpeg(const uint8_t* rgb_data, int width, int height,
                                int stride, int quality, const std::string& output_path) {
    tjhandle handle = tjInitCompress();
    if (!handle) {
        fprintf(stderr, "[Capture] tjInitCompress failed\n");
        return false;
    }

    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    int ret = tjCompress2(handle, rgb_data, width, stride, height,
                          TJPF_RGB, &jpeg_buf, &jpeg_size,
                          TJSAMP_420, quality, TJFLAG_FASTDCT);

    if (ret != 0) {
        fprintf(stderr, "[Capture] tjCompress2 failed: %s\n", tjGetErrorStr());
        tjDestroy(handle);
        return false;
    }

    FILE* fp = fopen(output_path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[Capture] Cannot open %s for writing\n", output_path.c_str());
        tjFree(jpeg_buf);
        tjDestroy(handle);
        return false;
    }

    fwrite(jpeg_buf, 1, jpeg_size, fp);
    fclose(fp);

    fprintf(stderr, "[Capture] Saved %s (%dx%d, %lu bytes)\n",
            output_path.c_str(), width, height, jpeg_size);

    tjFree(jpeg_buf);
    tjDestroy(handle);
    return true;
}

bool PhotoCapture::should_flash(const CaptureParams& params) {
    if (params.flash_mode == 1) return true;   // ON
    if (params.flash_mode == 0) return false;   // OFF
    // AUTO: flash if ambient light is low
    return params.ambient_lux < 50.0f;
}

std::string PhotoCapture::generate_filename(const std::string& dir) {
    // Ensure directory exists
    mkdir(dir.c_str(), 0755);

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s/IMG_%04d%02d%02d_%02d%02d%02d.jpg",
             dir.c_str(),
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return std::string(buf);
}

} // namespace cinepi
