#pragma once
#ifndef NOVACUBE_TERRAIN_H_
#define NOVACUBE_TERRAIN_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/camera.h>
#include <novacube/block.h>
#include <novacube/cvkm.h>
#include <novacube/mesher.h>
#include <novacube/renderer.h>

#define NC_TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE 5.0f

typedef struct nc_terrain_t nc_terrain_t;

typedef struct nc_terrain_raycast_hit_t {
    vkm_ivec3 block_position;
    vkm_bvec3 normal;
    float distance;
} nc_terrain_raycast_hit_t;

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer);
void nc_terrain_load_or_replace_chunk(
        nc_terrain_t* terrain,
        const vkm_ivec3* coords,
        const uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK]);
void nc_terrain_unload_chunk(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_ivec3* coords);
bool nc_terrain_get_block(const nc_terrain_t* terrain, const vkm_ivec3* block_coords, uint16_t* block);
uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain);
bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer);
void nc_terrain_get_opaque_draws(
        const nc_terrain_t* terrain,
        const vkm_mat4* view_projection,
        nc_renderer_chunk_opaque_draw_vec* draws);
bool nc_terrain_raycast(
        const nc_terrain_t* terrain,
        const nc_camera_t* camera,
        float max_distance,
        nc_terrain_raycast_hit_t* hit);
void nc_terrain_get_block_highlight_draw(
        const nc_terrain_t* terrain,
        const vkm_mat4* view_projection,
        float time,
        const nc_camera_t* camera,
        nc_renderer_block_highlight_draw_t* draw);
void nc_terrain_set_block(nc_terrain_t* terrain, const vkm_ivec3* block_coords, nc_block_type_t new_block);
void nc_terrain_entity_set_block(nc_terrain_t* terrain, const nc_camera_t* camera, nc_block_type_t new_block);
int32_t nc_terrain_get_top_solid_block(const nc_terrain_t* terrain, const vkm_ivec2 block_column_coords);
void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer);

#endif
