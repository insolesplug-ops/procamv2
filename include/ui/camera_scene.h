#pragma once
/**
 * CinePi Camera - Camera Scene Extensions
 * Grid overlay and level indicator drawn on LVGL.
 */

namespace cinepi {

class CameraPipeline;
class I2CSensors;

class CameraScene {
public:
    void init();
    void update(const CameraPipeline& cam, const I2CSensors& sensors);
    void set_grid_visible(bool visible);
    void set_level_visible(bool visible);

private:
    void draw_grid();
    void draw_level(float roll_deg);

    bool grid_visible_ = false;
    bool level_visible_ = false;
};

} // namespace cinepi
