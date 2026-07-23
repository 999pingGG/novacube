#pragma once
#ifndef NOVACUBE_GUI_H_
#define NOVACUBE_GUI_H_

#include <stdbool.h>
#include <SDL3/SDL.h>

#include <novacube/player_input.h>
#include <novacube/renderer.h>

typedef struct nc_gui_context_t nc_gui_context_t;

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer);
bool nc_gui_update_window_metrics(nc_gui_context_t* context, nc_renderer_t* renderer);
float nc_gui_get_scale(const nc_gui_context_t* context);
bool nc_gui_handle_event(nc_gui_context_t* context, const SDL_Event* event, bool mouse_input_enabled);
bool nc_gui_is_keyboard_captured(const nc_gui_context_t* context);
bool nc_gui_prepare_frame(
        nc_gui_context_t* context,
        nc_renderer_t* renderer,
        const nc_player_input_overlay_t* player_input,
        float delta_time);
void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw);
void nc_gui_get_procedural_overlay_draw(
        const nc_gui_context_t* context,
        const nc_player_input_overlay_t* player_input,
        nc_renderer_procedural_overlay_draw_t* draw);
void nc_gui_append_debug_text(nc_gui_context_t* context, const char* text, size_t length);
void nc_gui_fini(nc_gui_context_t* context, nc_renderer_t* renderer);
#endif
