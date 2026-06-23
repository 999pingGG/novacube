#pragma once
#ifndef NOVACUBE_TERRAIN_H_
#define NOVACUBE_TERRAIN_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/cvkm.h>
#include <novacube/renderer.h>

typedef uint8_t nc_block_type_t;

enum {
    NC_BLOCK_TYPE_AIR = 0,
    NC_BLOCK_TYPE_STONE = 1,
    NC_BLOCK_TYPE_DIRT = 2,
    NC_BLOCK_TYPE_GRASS = 3,
    NC_BLOCK_TYPE_COUNT = 3,
};

typedef struct nc_terrain_t nc_terrain_t;

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer);
bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer);
void nc_terrain_get_opaque_draw(
    const nc_terrain_t* terrain,
    const vkm_mat4* view_projection,
    nc_renderer_opaque_draw_t* draw);
void nc_terrain_modify_block(
    nc_terrain_t* terrain,
    const vkm_vec3* camera_position,
    float camera_yaw,
    float camera_pitch,
    nc_block_type_t new_block);
void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer);

#endif
