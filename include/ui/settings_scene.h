#pragma once
/**
 * CinePi Camera - Settings Scene
 * Extended settings beyond SquareLine quick-settings overlay.
 */

namespace cinepi {

class DrmDisplay;

class SettingsScene {
public:
    void init();
    void enter();
    void leave();
    void create_settings_ui();

private:
    bool initialized_ = false;
};

} // namespace cinepi
