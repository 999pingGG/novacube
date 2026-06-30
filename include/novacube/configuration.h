#pragma once
#ifndef NOVACUBE_CONFIGURATION_H_
#define NOVACUBE_CONFIGURATION_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/cvkm.h>

#define NC_VIDEO_MODE_TABLE(X) \
    X(WINDOW, "window") \
    X(BORDERLESS, "borderless") \
    X(FULLSCREEN, "fullscreen") \
    X(EXCLUSIVE_FULLSCREEN, "exclusive_fullscreen")

#define NC_TOUCH_MOVEMENT_MODE_TABLE(X) \
    X(ANALOG, "analog") \
    X(BUTTONS, "buttons")

#define NC_TOUCH_CAMERA_MODE_TABLE(X) \
    X(ANALOG, "analog") \
    X(FREE_DRAG, "free_drag")

typedef enum nc_video_mode_t {
#define X(id, str) NC_VIDEO_MODE_##id,
    NC_VIDEO_MODE_TABLE(X)
#undef X
} nc_video_mode_t;

typedef enum nc_touch_movement_mode_t {
#define X(id, str) NC_TOUCH_MOVEMENT_MODE_##id,
    NC_TOUCH_MOVEMENT_MODE_TABLE(X)
#undef X
} nc_touch_movement_mode_t;

typedef enum nc_touch_camera_mode_t {
#define X(id, str) NC_TOUCH_CAMERA_MODE_##id,
    NC_TOUCH_CAMERA_MODE_TABLE(X)
#undef X
} nc_touch_camera_mode_t;

// type, name, visible in file by default (YES or NO), comment (can be omitted!), parse function, print function, default value.
#define NC_CONFIG_TABLE(X) \
    X(vkm_usvec2, resolution, YES, "; width, height\n", nc__parse_usvec2, nc__print_usvec2, 640, 480) \
    X(uint16_t, refresh_rate, YES, "\n; 0 = match desktop refresh rate\n", nc__parse_uint16, nc__print_int, 0) \
    X(      nc_video_mode_t, \
            video_mode, \
            YES, \
            "\n; window, borderless, fullscreen, exclusive_fullscreen\n", \
            nc__parse_video_mode, \
            nc__print_video_mode, \
            NC_VIDEO_MODE_WINDOW) \
    X(      nc_touch_movement_mode_t, \
            touch_movement_mode, \
            YES, \
            "\n; analog, buttons\n", \
            nc__parse_touch_movement_mode, \
            nc__print_touch_movement_mode, \
            NC_TOUCH_MOVEMENT_MODE_ANALOG) \
    X(      nc_touch_camera_mode_t, \
            touch_camera_mode, \
            YES, \
            "\n; analog, free_drag\n", \
            nc__parse_touch_camera_mode, \
            nc__print_touch_camera_mode, \
            NC_TOUCH_CAMERA_MODE_ANALOG) \
    X(      double, mouse_sensitivity, YES, "\n; degrees per pixel\n", nc__parse_double, nc__print_double, 0.2) \
    X(      double, \
            touch_camera_drag_sensitivity, \
            YES, \
            "\n; radians per physical pixel\n", \
            nc__parse_double, \
            nc__print_double, \
            0.0046875) \
    X(      double, \
            touch_camera_stick_sensitivity, \
            YES, \
            "\n; radians per second at full stick tilt\n", \
            nc__parse_double, \
            nc__print_double, \
            3.0) \

#define X(type, name, ...) \
    type nc_config_get_##name(void); \
    void nc_config_set_##name(type new_##name);
NC_CONFIG_TABLE(X)
#undef X

bool nc_configuration_load(void);
bool nc_configuration_save(void);

#endif
