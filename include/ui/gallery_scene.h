#pragma once
/**
 * CinePi Camera - Gallery Scene
 * Memory-optimized JPEG viewing with libjpeg-turbo IDCT scaling.
 */

#include <string>
#include <vector>
#include <cstdint>

struct _lv_obj_t;

namespace cinepi {

class GalleryScene {
public:
    GalleryScene();
    ~GalleryScene();

    void init();
    void enter();
    void leave();

    void load_photo_list();
    void show_current();
    void next();
    void prev();
    void delete_current();

    int count() const { return static_cast<int>(photos_.size()); }
    int index() const { return current_idx_; }

private:
    bool decode_jpeg_scaled(const std::string& path, int target_w,
                            uint8_t** out_buf, int* out_w, int* out_h);
    void free_image();

    std::vector<std::string> photos_;
    int current_idx_ = 0;

    // LVGL image descriptor
    uint8_t* img_buf_ = nullptr;
    int img_w_ = 0;
    int img_h_ = 0;
    bool active_ = false;
};

} // namespace cinepi
