#pragma once
#ifndef NOVACUBE_GUI_H_
#define NOVACUBE_GUI_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>
#include <novacube/renderer.h>

typedef uint8_t nc_gui_controls_t;
enum {
    NC_GUI_CONTROL_MOVE_LEFT =      1u << 0,
    NC_GUI_CONTROL_MOVE_RIGHT =     1u << 1,
    NC_GUI_CONTROL_MOVE_FORWARD =   1u << 2,
    NC_GUI_CONTROL_MOVE_BACKWARD =  1u << 3,
    NC_GUI_CONTROL_MOVE_UP =        1u << 4,
    NC_GUI_CONTROL_MOVE_DOWN =      1u << 5,
};

typedef uint8_t nc_gui_actions_t;
enum {
    NC_GUI_ACTION_PLACE_BLOCK =     1u << 0,
    NC_GUI_ACTION_REMOVE_BLOCK =    1u << 1,
};

typedef struct nc_gui_context_t nc_gui_context_t;

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer);
void nc_gui_set_window_size(nc_gui_context_t* context, uint16_t width, uint16_t height);
void nc_gui_set_pixel_viewport(nc_gui_context_t* context, uint16_t width, uint16_t height);
void nc_gui_set_window_display_scale(nc_gui_context_t* context, float window_display_scale);
bool nc_gui_handle_event(nc_gui_context_t* context, const SDL_Event* event);
nc_gui_controls_t nc_gui_get_controls(const nc_gui_context_t* context);
nc_gui_actions_t nc_gui_consume_actions(nc_gui_context_t* context);
bool nc_gui_consume_look_delta(nc_gui_context_t* context, vkm_vec2* delta);
bool nc_gui_is_touch_captured(const nc_gui_context_t* context, SDL_FingerID finger_id);
bool nc_gui_prepare_frame(nc_gui_context_t* context, nc_renderer_t* renderer, float delta_time);
void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw);
void nc_gui_fini(nc_gui_context_t* context, nc_renderer_t* renderer);
#endif
