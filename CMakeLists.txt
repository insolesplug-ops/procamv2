cmake_minimum_required(VERSION 3.16)
project(cinepi_app VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Raspberry Pi cross-compile friendly
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# ─── Find system packages ───────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
pkg_check_modules(GPIOD REQUIRED libgpiod)

find_library(TURBOJPEG_LIB turbojpeg REQUIRED)
find_library(I2C_LIB i2c)

# nlohmann/json - header only
find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp
    PATHS /usr/include /usr/local/include
)

# ─── LVGL (built from source) ───────────────────────────────────────
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/lvgl)
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}. Run: git clone -b release/v8.3 https://github.com/lvgl/lvgl.git")
endif()

# LVGL config header location
set(LV_CONF_PATH ${CMAKE_SOURCE_DIR}/include/core/lv_conf.h)
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)

# Collect all LVGL sources
file(GLOB_RECURSE LVGL_SOURCES
    ${LVGL_DIR}/src/*.c
)

add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC
    ${CMAKE_SOURCE_DIR}/include/core  # lv_conf.h lives here
    ${CMAKE_SOURCE_DIR}               # so #include "lvgl/lvgl.h" works
)

# ─── SquareLine Studio UI sources ───────────────────────────────────
file(GLOB UI_SOURCES
    ${CMAKE_SOURCE_DIR}/UI/ui.c
    ${CMAKE_SOURCE_DIR}/UI/ui_helpers.c
    ${CMAKE_SOURCE_DIR}/UI/components/*.c
    ${CMAKE_SOURCE_DIR}/UI/screens/*.c
    ${CMAKE_SOURCE_DIR}/UI/fonts/*.c
    ${CMAKE_SOURCE_DIR}/UI/images/*.c
)

add_library(ui_squareline STATIC ${UI_SOURCES})
target_include_directories(ui_squareline PUBLIC
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}/include/core
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(ui_squareline PUBLIC lvgl)

# ─── Application sources ────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/core/config.cpp
    src/drivers/drm_display.cpp
    src/drivers/touch_input.cpp
    src/drivers/gpio_driver.cpp
    src/drivers/i2c_sensors.cpp
    src/camera/camera_pipeline.cpp
    src/camera/photo_capture.cpp
    src/ui/lvgl_driver.cpp
    src/ui/scene_manager.cpp
    src/ui/camera_scene.cpp
    src/ui/gallery_scene.cpp
    src/ui/settings_scene.cpp
    src/gallery/photo_manager.cpp
    src/power/power_manager.cpp
)

add_executable(cinepi_app ${APP_SOURCES})

target_include_directories(cinepi_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/core
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${LIBCAMERA_INCLUDE_DIRS}
    ${GPIOD_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE}
)

target_link_libraries(cinepi_app PRIVATE
    ui_squareline
    lvgl
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${LIBCAMERA_LIBRARIES}
    ${GPIOD_LIBRARIES}
    ${TURBOJPEG_LIB}
    pthread
    m
)

if(I2C_LIB)
    target_link_libraries(cinepi_app PRIVATE ${I2C_LIB})
endif()

target_link_directories(cinepi_app PRIVATE
    ${DRM_LIBRARY_DIRS}
    ${GBM_LIBRARY_DIRS}
    ${LIBCAMERA_LIBRARY_DIRS}
    ${GPIOD_LIBRARY_DIRS}
)

# ─── Install ────────────────────────────────────────────────────────
install(TARGETS cinepi_app DESTINATION /home/pi/cinepi_app/build)
install(FILES assets/boot_logo.png DESTINATION /home/pi/cinepi_app/assets)
install(FILES assets/Inter_regular.ttf assets/inter_bold.ttf
        DESTINATION /home/pi/cinepi_app/assets)
