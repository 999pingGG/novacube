#pragma once
#ifndef NOVACUBE_GUI_H_
#define NOVACUBE_GUI_H_

#include <stddef.h>

#include <novacube/gui_view.h>
#include <novacube/player_input.h>
#include <novacube/renderer.h>

typedef struct nc_gui_context_t nc_gui_context_t;

nc_gui_context_t* nc_gui_init(nc_renderer_t* renderer, nc_asset_manager_t* asset_manager);
// Refreshes and returns the framebuffer dimensions and safe area shared with player input.
const nc_gui_view_t* nc_gui_get_view(nc_gui_context_t* context);
// Updates Clay pointer state and returns whether the GUI captured the event.
bool nc_gui_handle_event(nc_gui_context_t* context, const SDL_Event* event);
// Runs Clay layout and builds the renderer draw list.
void nc_gui_prepare_frame(nc_gui_context_t* context, const nc_player_input_overlay_t* player_input, float delta_time);
void nc_gui_get_overlay_draw(const nc_gui_context_t* context, nc_renderer_overlay_draw_t* draw);
void nc_gui_get_procedural_overlay_draw(
        const nc_player_input_overlay_t* player_input,
        nc_renderer_procedural_overlay_draw_t* draw);
void nc_gui_append_debug_text(nc_gui_context_t* context, const char* text, size_t length);
void nc_gui_fini(nc_gui_context_t* context);
#endif
