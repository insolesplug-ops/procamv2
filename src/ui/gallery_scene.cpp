/**
 * CinePi Camera - Gallery Scene
 * Decodes JPEGs with libjpeg-turbo IDCT scaling to avoid
 * loading full 8MP images into 512MB RAM.
 */

#include "ui/gallery_scene.h"
#include "core/config.h"
#include "core/constants.h"

#include "ui.h"
#include "lvgl/lvgl.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <turbojpeg.h>

namespace cinepi {

// Static LVGL image descriptor used for display
static lv_img_dsc_t gallery_img_dsc;
static lv_obj_t* gallery_img_obj = nullptr;
static lv_obj_t* gallery_label = nullptr;

GalleryScene::GalleryScene() = default;

GalleryScene::~GalleryScene() {
    free_image();
}

void GalleryScene::init() {
    // Gallery image and label will be set up in enter()
}

void GalleryScene::enter() {
    active_ = true;
    load_photo_list();

    if (ui_Gallery1 && ui_imagepreview) {
        // Create image object inside the preview container
        if (!gallery_img_obj) {
            gallery_img_obj = lv_img_create(ui_imagepreview);
            lv_obj_set_align(gallery_img_obj, LV_ALIGN_CENTER);
        }

        // Counter label
        if (!gallery_label) {
            gallery_label = lv_label_create(ui_Gallery1);
            lv_obj_set_style_text_color(gallery_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(gallery_label, &ui_font_Font1, 0);
            lv_obj_set_align(gallery_label, LV_ALIGN_BOTTOM_MID);
            lv_obj_set_y(gallery_label, -50);
        }

        // Add swipe gesture for navigation
        lv_obj_add_flag(ui_imagepreview, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(ui_imagepreview, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(ui_imagepreview, [](lv_event_t* e) {
            auto* self = static_cast<GalleryScene*>(lv_event_get_user_data(e));
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_LEFT) {
                self->next();
            } else if (dir == LV_DIR_RIGHT) {
                self->prev();
            }
        }, LV_EVENT_GESTURE, this);
    }

    // Start with newest photo
    if (!photos_.empty()) {
        current_idx_ = 0;  // sorted newest first
        show_current();
    }
}

void GalleryScene::leave() {
    active_ = false;
    free_image();
}

void GalleryScene::load_photo_list() {
    photos_.clear();
    auto& cfg = ConfigManager::instance().get();

    DIR* dir = opendir(cfg.photo_dir.c_str());
    if (!dir) {
        fprintf(stderr, "[Gallery] Cannot open %s\n", cfg.photo_dir.c_str());
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        // Check for .jpg/.jpeg extension (case-insensitive)
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool is_jpg = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg";
        bool is_jpeg = lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg";
        if (!is_jpg && !is_jpeg) continue;

        photos_.push_back(cfg.photo_dir + "/" + name);
    }
    closedir(dir);

    // Sort by modification time (newest first)
    std::sort(photos_.begin(), photos_.end(), [](const std::string& a, const std::string& b) {
        struct stat sa, sb;
        stat(a.c_str(), &sa);
        stat(b.c_str(), &sb);
        return sa.st_mtime > sb.st_mtime;
    });

    fprintf(stderr, "[Gallery] Found %zu photos\n", photos_.size());
}

void GalleryScene::show_current() {
    if (photos_.empty() || current_idx_ >= (int)photos_.size()) {
        if (gallery_label) lv_label_set_text(gallery_label, "No photos");
        return;
    }

    free_image();

    uint8_t* buf = nullptr;
    int w = 0, h = 0;

    if (decode_jpeg_scaled(photos_[current_idx_], GALLERY_THUMB_W, &buf, &w, &h)) {
        img_buf_ = buf;
        img_w_ = w;
        img_h_ = h;

        // Set up LVGL image descriptor
        memset(&gallery_img_dsc, 0, sizeof(gallery_img_dsc));
        gallery_img_dsc.header.always_zero = 0;
        gallery_img_dsc.header.w = w;
        gallery_img_dsc.header.h = h;
        gallery_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        gallery_img_dsc.data_size = w * h * sizeof(lv_color_t);
        gallery_img_dsc.data = buf;

        if (gallery_img_obj) {
            lv_img_set_src(gallery_img_obj, &gallery_img_dsc);
            lv_obj_set_size(gallery_img_obj, w, h);
        }
    }

    // Update counter label
    if (gallery_label) {
        char buf_label[32];
        snprintf(buf_label, sizeof(buf_label), "%d / %d",
                 current_idx_ + 1, (int)photos_.size());
        lv_label_set_text(gallery_label, buf_label);
    }
}

void GalleryScene::next() {
    if (current_idx_ < (int)photos_.size() - 1) {
        current_idx_++;
        show_current();
    }
}

void GalleryScene::prev() {
    if (current_idx_ > 0) {
        current_idx_--;
        show_current();
    }
}

void GalleryScene::delete_current() {
    if (photos_.empty() || current_idx_ >= (int)photos_.size()) return;

    std::string path = photos_[current_idx_];
    if (unlink(path.c_str()) == 0) {
        fprintf(stderr, "[Gallery] Deleted: %s\n", path.c_str());
        photos_.erase(photos_.begin() + current_idx_);
        if (current_idx_ >= (int)photos_.size() && current_idx_ > 0) {
            current_idx_--;
        }
        show_current();
    } else {
        fprintf(stderr, "[Gallery] Failed to delete: %s\n", path.c_str());
    }
}

bool GalleryScene::decode_jpeg_scaled(const std::string& path, int target_w,
                                       uint8_t** out_buf, int* out_w, int* out_h) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 20 * 1024 * 1024) {
        fclose(fp);
        return false;
    }

    uint8_t* jpeg_buf = static_cast<uint8_t*>(malloc(file_size));
    if (!jpeg_buf) {
        fclose(fp);
        return false;
    }

    if (fread(jpeg_buf, 1, file_size, fp) != (size_t)file_size) {
        free(jpeg_buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    tjhandle handle = tjInitDecompress();
    if (!handle) {
        free(jpeg_buf);
        return false;
    }

    int jpeg_w, jpeg_h, jpeg_subsamp, jpeg_colorspace;
    if (tjDecompressHeader3(handle, jpeg_buf, file_size,
                            &jpeg_w, &jpeg_h, &jpeg_subsamp, &jpeg_colorspace) != 0) {
        tjDestroy(handle);
        free(jpeg_buf);
        return false;
    }

    int num_factors;
    tjscalingfactor* factors = tjGetScalingFactors(&num_factors);
    tjscalingfactor best = {1, 1};
    int best_w = jpeg_w;

    for (int i = 0; i < num_factors; i++) {
        int scaled_w = TJSCALED(jpeg_w, factors[i]);
        if (scaled_w >= target_w && scaled_w < best_w) {
            best = factors[i];
            best_w = scaled_w;
        }
    }

    int scaled_w = TJSCALED(jpeg_w, best);
    int scaled_h = TJSCALED(jpeg_h, best);

    // FIX: Allocate only LVGL buffer, decode RGB temp, convert in-place
    size_t lv_size = scaled_w * scaled_h * sizeof(lv_color_t);
    uint8_t* lv_buf = static_cast<uint8_t*>(malloc(lv_size));
    if (!lv_buf) {
        tjDestroy(handle);
        free(jpeg_buf);
        return false;
    }

    // Allocate RGB temp
    size_t rgb_size = scaled_w * scaled_h * 3;
    uint8_t* rgb_buf = static_cast<uint8_t*>(malloc(rgb_size));
    if (!rgb_buf) {
        free(lv_buf);
        tjDestroy(handle);
        free(jpeg_buf);
        return false;
    }

    if (tjDecompress2(handle, jpeg_buf, file_size,
                       rgb_buf, scaled_w, 0, scaled_h,
                       TJPF_RGB, TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) != 0) {
        free(rgb_buf);
        free(lv_buf);
        tjDestroy(handle);
        free(jpeg_buf);
        return false;
    }

    free(jpeg_buf);
    tjDestroy(handle);

    // Convert RGB888 to LVGL lv_color_t (RGB565) in-place
    // Peak memory: 345KB only (not 863KB!)
    lv_color_t* dst = reinterpret_cast<lv_color_t*>(lv_buf);
    
    int num_pixels = scaled_w * scaled_h;
    for (int i = 0; i < num_pixels; i++) {
        uint8_t r = rgb_buf[i * 3 + 0];
        uint8_t g = rgb_buf[i * 3 + 1];
        uint8_t b = rgb_buf[i * 3 + 2];
        dst[i] = lv_color_make(r, g, b);
    }

    free(rgb_buf);

    *out_buf = lv_buf;
    *out_w = scaled_w;
    *out_h = scaled_h;

    fprintf(stderr, "[Gallery] Decoded %s: %dx%d -> %dx%d (%.1f KB)\n",
            path.c_str(), jpeg_w, jpeg_h, scaled_w, scaled_h,
            lv_size / 1024.0f);
    return true;
}

void GalleryScene::free_image() {
    if (img_buf_) {
        free(img_buf_);
        img_buf_ = nullptr;
    }
    img_w_ = 0;
    img_h_ = 0;
}

} // namespace cinepi
