#pragma once
#ifndef NOVACUBE_TERRAIN_LIGHTING_H_
#define NOVACUBE_TERRAIN_LIGHTING_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum nc_terrain_light_channel_t {
    NC_TERRAIN_LIGHT_CHANNEL_BLOCK,
    NC_TERRAIN_LIGHT_CHANNEL_SKY,
    NC_TERRAIN_LIGHT_CHANNEL_COUNT,
} nc_terrain_light_channel_t;

typedef struct nc_terrain_lighting_t {
    bool sky_light_has_propagated;
    bool sky_light_needs_rebuild;
    bool sky_light_frontier_queued;
} nc_terrain_lighting_t;

uint8_t nc_terrain_light_get(uint8_t packed_light, nc_terrain_light_channel_t channel);
void nc_terrain_light_set(uint8_t* packed_light, nc_terrain_light_channel_t channel, uint8_t light);
uint8_t nc_terrain_light_get_block(uint8_t packed_light);
uint8_t nc_terrain_light_get_sky(uint8_t packed_light);
void nc_terrain_light_set_block(uint8_t* packed_light, uint8_t light);
void nc_terrain_light_set_sky(uint8_t* packed_light, uint8_t light);

#endif
