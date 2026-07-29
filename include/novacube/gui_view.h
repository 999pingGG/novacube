#pragma once
#ifndef NOVACUBE_GUI_VIEW_H_
#define NOVACUBE_GUI_VIEW_H_

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>

// Clay lays out directly in oriented framebuffer coordinates. SDL reports safe areas in window
// coordinates, so gui_safe_area is converted to this space when the view is built.
typedef struct nc_gui_view_t {
    vkm_usvec2 framebuffer_size;
    SDL_FRect gui_safe_area;
} nc_gui_view_t;

#endif
