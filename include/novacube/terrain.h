#pragma once
#ifndef NOVACUBE_TERRAIN_H_
#define NOVACUBE_TERRAIN_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/camera.h>
#include <novacube/cvkm.h>
#include <novacube/renderer.h>

typedef uint8_t nc_block_type_t;
enum {
    NC_BLOCK_TYPE_AIR = 0,
    NC_BLOCK_TYPE_STONE,
    NC_BLOCK_TYPE_DIRT,
    NC_BLOCK_TYPE_GRASS,

    NC_BLOCK_TYPE_COUNT = NC_BLOCK_TYPE_GRASS,
};

typedef struct nc_terrain_t nc_terrain_t;

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer);
bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer);
void nc_terrain_get_opaque_draw(
        const nc_terrain_t* terrain,
        const vkm_mat4* view_projection,
        nc_renderer_opaque_draw_t* draw);
void nc_terrain_get_block_highlight_draw(
        const nc_terrain_t* terrain,
        const vkm_mat4* view_projection,
        float time,
        const nc_camera_t* camera,
        nc_renderer_block_highlight_draw_t* draw);
void nc_terrain_modify_block(nc_terrain_t* terrain, const nc_camera_t* camera, nc_block_type_t new_block);
void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer);

#endif
