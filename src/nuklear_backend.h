#pragma once
#ifndef NOVACUBE_NUKLEAR_BACKEND_H_
#define NOVACUBE_NUKLEAR_BACKEND_H_

#include <stdbool.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>
#include <novacube/renderer.h>

#include "nuklear_config.h"

typedef struct nc_nuklear_view_t {
    vkm_usvec2 window_size;
    vkm_usvec2 pixel_viewport;
    float scale;
} nc_nuklear_view_t;

typedef struct nc_nuklear_backend_t nc_nuklear_backend_t;

nc_nuklear_backend_t* nc_nuklear_backend_init(nc_renderer_t* renderer, float scale);
bool nc_nuklear_backend_set_scale(
        nc_nuklear_backend_t* backend,
        nc_renderer_t* renderer,
        float scale);
struct nk_context* nc_nuklear_backend_get_context(nc_nuklear_backend_t* backend);
bool nc_nuklear_backend_handle_event(
        nc_nuklear_backend_t* backend,
        const nc_nuklear_view_t* view,
        const SDL_Event* event,
        bool mouse_input_enabled);
bool nc_nuklear_backend_is_keyboard_captured(const nc_nuklear_backend_t* backend);
void nc_nuklear_backend_begin_frame(nc_nuklear_backend_t* backend, float delta_time);
bool nc_nuklear_backend_end_frame(
        nc_nuklear_backend_t* backend,
        nc_renderer_t* renderer,
        const nc_nuklear_view_t* view);
void nc_nuklear_backend_get_draw(
        const nc_nuklear_backend_t* backend,
        const nc_nuklear_view_t* view,
        nc_renderer_overlay_draw_t* draw);
void nc_nuklear_backend_fini(nc_nuklear_backend_t* backend, nc_renderer_t* renderer);

#endif
