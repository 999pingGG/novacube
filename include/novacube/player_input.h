#pragma once
#ifndef NOVACUBE_PLAYER_INPUT_H_
#define NOVACUBE_PLAYER_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include <novacube/cvkm.h>
#include <novacube/entity_command.h>
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

    NC_PLAYER_INPUT_CONTROL_COUNT,
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

nc_player_input_t* nc_player_input_init(nc_renderer_t* renderer, float gui_scale);
void nc_player_input_update_view(nc_player_input_t* input, const nc_renderer_t* renderer, float gui_scale);
void nc_player_input_update(nc_player_input_t* input, float delta_time);
bool nc_player_input_handle_event(
        nc_player_input_t* input,
        nc_renderer_t* renderer,
        const SDL_Event* event,
        bool gui_captured);
void nc_player_input_get_entity_command(
        nc_player_input_t* input,
        bool keyboard_captured,
        float delta_time,
        nc_entity_command_t* command);
void nc_player_input_get_overlay(const nc_player_input_t* input, nc_player_input_overlay_t* overlay);
void nc_player_input_fini(nc_player_input_t* input);

#endif
