#pragma once
#ifndef NOVACUBE_PLAYER_INPUT_H_
#define NOVACUBE_PLAYER_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>
#include <novacube/entity_command.h>
#include <novacube/gui_view.h>
#include <novacube/renderer.h>

typedef uint8_t nc_player_input_control_id_t;
enum {
    NC_PLAYER_INPUT_CONTROL_LEFT,
    NC_PLAYER_INPUT_CONTROL_RIGHT,
    NC_PLAYER_INPUT_CONTROL_FORWARD,
    NC_PLAYER_INPUT_CONTROL_BACKWARD,
    NC_PLAYER_INPUT_CONTROL_UP,
    NC_PLAYER_INPUT_CONTROL_DOWN,
    NC_PLAYER_INPUT_CONTROL_PLACE_BLOCK,
    NC_PLAYER_INPUT_CONTROL_REMOVE_BLOCK,

    NC_PLAYER_INPUT_CONTROL_NONE,
    NC_PLAYER_INPUT_CONTROL_COUNT = NC_PLAYER_INPUT_CONTROL_NONE,
};

typedef struct nc_player_input_overlay_t {
    SDL_FRect control_rects[NC_PLAYER_INPUT_CONTROL_COUNT];
    bool controls_visible[NC_PLAYER_INPUT_CONTROL_COUNT];
    bool controls_pressed[NC_PLAYER_INPUT_CONTROL_COUNT];
    vkm_vec2 analog_stick_ring_positions[2];
    vkm_vec2 analog_stick_positions[2];
    bool analog_sticks_active[2];
    float analog_stick_ring_radius;
    float analog_stick_ring_thickness;
    float analog_stick_radius;
    bool touch_controls_enabled;
} nc_player_input_overlay_t;

typedef struct nc_player_input_t nc_player_input_t;

nc_player_input_t* nc_player_input_init(const nc_gui_view_t* view);
// Updates the coordinate mapping and rebuilds touch-control rectangles from the current cvars.
void nc_player_input_update_view(nc_player_input_t* input, const nc_gui_view_t* view);
// Decays the short activity accumulator used to switch between touch and mouse input.
void nc_player_input_update(nc_player_input_t* input, float delta_time);
// Updates modality and multitouch capture state and accumulates gameplay actions.
bool nc_player_input_handle_event(nc_player_input_t* input, nc_renderer_t* renderer, const SDL_Event* event);
// Combines keyboard, mouse, buttons, and analog touches into one command and clears frame deltas.
void nc_player_input_get_entity_command(nc_player_input_t* input, float delta_time, nc_entity_command_t* command);
// Returns the same button rectangles/state that the Clay HUD draws this frame.
void nc_player_input_get_overlay(const nc_player_input_t* input, nc_player_input_overlay_t* overlay);
void nc_player_input_fini(nc_player_input_t* input);

#endif
