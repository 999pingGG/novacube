#pragma once
#ifndef NOVACUBE_ENTITY_COMMAND_H_
#define NOVACUBE_ENTITY_COMMAND_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/cvkm.h>

typedef uint8_t nc_entity_actions_t;
enum {
    NC_ENTITY_ACTION_PLACE_BLOCK =  1 << 0,
    NC_ENTITY_ACTION_REMOVE_BLOCK = 1 << 1,
    NC_ENTITY_ACTION_TOGGLE_CLAY_DEBUG_MODE = 1 << 2,
};

// A device-independent request to control an entity for one simulation tick.
// Human input, network input and AI controllers can all produce this structure.
typedef struct nc_entity_command_t {
    // Local-space right/up/forward movement, with a maximum magnitude of one.
    vkm_vec3 movement;
    // Yaw and pitch deltas in radians.
    vkm_vec2 look_delta;
    nc_entity_actions_t actions;
    // TODO: Can this be an action or flag merged with the actions field?
    // Maybe add separate status flags?
    bool sprint;
} nc_entity_command_t;

#endif
