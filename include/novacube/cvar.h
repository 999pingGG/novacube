#pragma once
#ifndef NOVACUBE_CVAR_H_
#define NOVACUBE_CVAR_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/cvkm.h>

#define NC_VIDEO_MODE_TABLE(X) \
    X(WINDOW, "window") \
    X(BORDERLESS, "borderless") \
    X(FULLSCREEN, "fullscreen") \
    X(EXCLUSIVE_FULLSCREEN, "exclusive_fullscreen") \

#define NC_GPU_MEMORY_PREFERENCE_TABLE(X) \
    X(LARGER, "larger") \
    X(SMALLER, "smaller") \
    X(NONE, "none") \

#define NC_TOUCH_MOVEMENT_MODE_TABLE(X) \
    X(ANALOG, "analog") \
    X(BUTTONS, "buttons")

#define NC_TOUCH_CAMERA_MODE_TABLE(X) \
    X(ANALOG, "analog") \
    X(FREE_DRAG, "free_drag")

#define NC_BLOCK_HIGHLIGHT_EFFECT_TABLE(X) \
    X(OUTLINE, "outline") \
    X(VIGNETTE, "vignette") \
    X(PLASMA, "plasma") \

typedef enum nc_video_mode_t {
#define X(id, str) NC_VIDEO_MODE_##id,
    NC_VIDEO_MODE_TABLE(X)
#undef X
} nc_video_mode_t;

typedef enum nc_gpu_memory_preference_t {
#define X(id, str) NC_GPU_MEMORY_PREFERENCE_##id,
    NC_GPU_MEMORY_PREFERENCE_TABLE(X)
#undef X
} nc_gpu_memory_preference_t;

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

typedef enum nc_block_highlight_effect_t {
#define X(id, str) NC_BLOCK_HIGHLIGHT_EFFECT_##id,
    NC_BLOCK_HIGHLIGHT_EFFECT_TABLE(X)
#undef X
} nc_block_highlight_effect_t;

// type, name, visible in file by default (YES or NO), comment (can be omitted!), parse function, print function,
// default value string (can be omitted if it's not in the file by default), default value.
#define NC_CVAR_TABLE(X) \
    X(vkm_usvec2, resolution, YES, "; width, height\n", nc__parse_usvec2, nc__print_usvec2, "640, 480", 640, 480) \
    X(uint16_t, refresh_rate, YES, "\n; 0 = match desktop refresh rate\n", nc__parse_uint16, nc__print_int, "0", 0) \
    X(nc_video_mode_t, \
            video_mode, \
            YES, \
            "\n; window, borderless, fullscreen, exclusive_fullscreen\n", \
            nc__parse_video_mode, \
            nc__print_video_mode, \
            "window", \
            NC_VIDEO_MODE_WINDOW) \
    X(bool, prefer_low_power_gpu, YES, "\n", nc__parse_bool, nc__print_bool, "false", false) \
    X(nc_gpu_memory_preference_t, \
            gpu_memory_preference, \
            NO, \
            , \
            nc__parse_gpu_memory_preference, \
            nc__print_gpu_memory_preference, \
            , \
            NC_GPU_MEMORY_PREFERENCE_LARGER) \
    X(int, selected_gpu, NO,, nc__parse_int, nc__print_int,, -1) \
    X(nc_touch_movement_mode_t, \
            touch_movement_mode, \
            YES, \
            "\n; analog, buttons\n", \
            nc__parse_touch_movement_mode, \
            nc__print_touch_movement_mode, \
            "analog", \
            NC_TOUCH_MOVEMENT_MODE_ANALOG) \
    X(nc_touch_camera_mode_t, \
            touch_camera_mode, \
            YES, \
            "\n; analog, free_drag\n", \
            nc__parse_touch_camera_mode, \
            nc__print_touch_camera_mode, \
            "analog", \
            NC_TOUCH_CAMERA_MODE_ANALOG) \
    X(double, mouse_sensitivity, YES, "\n; degrees per pixel\n", nc__parse_double, nc__print_double, "0.2", 0.2) \
    X(double, \
            touch_camera_drag_sensitivity, \
            YES, \
            "\n; radians per physical pixel\n", \
            nc__parse_double, \
            nc__print_double, \
            "0.0046875", \
            0.0046875) \
    X(double, \
            touch_camera_stick_sensitivity, \
            YES, \
            "\n; radians per second at full stick tilt\n", \
            nc__parse_double, \
            nc__print_double, \
            "3.0", \
            3.0) \
    X(bool, show_fps, NO, , nc__parse_bool, nc__print_bool, , false) \
    X(bool, show_frame_time, NO, , nc__parse_bool, nc__print_bool, , false) \
    X(double, gui_button_size, NO, , nc__parse_double, nc__print_double, , 50.0) \
    X(double, crosshair_size, NO, , nc__parse_double, nc__print_double, , 28.0) \
    X(double, analog_stick_ring_radius, NO, , nc__parse_double, nc__print_double, , 64.0) \
    X(double, analog_stick_ring_thickness, NO, , nc__parse_double, nc__print_double, , 4.0) \
    X(double, analog_stick_radius, NO, , nc__parse_double, nc__print_double, , 28.0) \
    X(nc_block_highlight_effect_t, \
            block_highlight_effect, \
            NO, \
            , \
            nc__parse_block_highlight_effect, \
            nc__print_block_highlight_effect, \
            , \
            NC_BLOCK_HIGHLIGHT_EFFECT_OUTLINE) \
    X(vkm_ubvec4, \
            block_highlight_color, \
            NO, \
            , \
            nc__parse_ubvec4, \
            nc__print_ubvec4, \
            , \
            0, 0, 0, 127) \
    X(bool, show_target_block_debug_details, NO,, nc__parse_bool, nc__print_bool,, false) \

#define X(type, name, ...) \
    type nc_cvar_get_##name(void); \
    void nc_cvar_set_##name(type new_##name);
NC_CVAR_TABLE(X)
#undef X

bool nc_configuration_load(void);
bool nc_configuration_save(void);

#endif
