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

typedef enum nc_video_mode_t {
#define X(id, str) NC_VIDEO_MODE_##id,
    NC_VIDEO_MODE_TABLE(X)
#undef X
} nc_video_mode_t;

// type, name, visible in file by default (YES or NO), comment (can be omitted!), parse function, print function, default value.
#define NC_CONFIG_TABLE(X) \
    X(vkm_usvec2, resolution, YES,, nc__parse_usvec2, nc__print_usvec2, 640, 480) \
    X(uint16_t, refresh_rate, YES, "; 0 = autodetect\n", nc__parse_uint16, nc__print_int, 0) \
    X(      nc_video_mode_t, \
            video_mode, \
            YES, \
            "; window, borderless, fullscreen, exclusive_fullscreen\n", \
            nc__parse_video_mode, \
            nc__print_video_mode, \
            NC_VIDEO_MODE_WINDOW) \

#define X(type, name, ...) \
    type nc_config_get_##name(void); \
    void nc_config_set_##name(type new_##name);
NC_CONFIG_TABLE(X)
#undef X

bool nc_configuration_load(void);
bool nc_configuration_save(void);

#endif
