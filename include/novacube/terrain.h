#pragma once
#ifndef NOVACUBE_TERRAIN_H_
#define NOVACUBE_TERRAIN_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/camera.h>
#include <novacube/block.h>
#include <novacube/cvkm.h>
#include <novacube/renderer.h>

#define NC_TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE 5.0f

typedef struct nc_terrain_t nc_terrain_t;

typedef struct nc_terrain_raycast_hit_t {
    vkm_ivec3 block_position;
    vkm_bvec3 normal;
    float distance;
} nc_terrain_raycast_hit_t;

typedef struct nc_terrain_rendering_stats_t {
    uint32_t loaded_chunk_count;
    uint32_t empty_chunk_count;
    uint32_t culled_chunk_count;
    uint32_t opaque_drawn_chunk_count;
    uint32_t transparent_drawn_chunk_count;
    uint32_t total_opaque_quads_count;
    uint32_t total_transparent_quads_count;
    uint32_t culled_opaque_quads_count;
    uint32_t culled_transparent_quads_count;
} nc_terrain_rendering_stats_t;

typedef struct nc_terrain_timing_stats_t {
    double residency_ms;
    double loading_ms;
    double unloading_ms;
    double lighting_ms;
    double meshing_ms;
} nc_terrain_timing_stats_t;

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

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer, nc_asset_manager_t* asset_manager);
void nc_terrain_update(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_vec3* player_position);
uint8_t nc_terrain_light_get(uint8_t packed_light, nc_terrain_light_channel_t channel);
void nc_terrain_light_set(uint8_t* packed_light, nc_terrain_light_channel_t channel, uint8_t light);
uint8_t nc_terrain_light_get_block(uint8_t packed_light);
uint8_t nc_terrain_light_get_sky(uint8_t packed_light);
void nc_terrain_light_set_block(uint8_t* packed_light, uint8_t light);
void nc_terrain_light_set_sky(uint8_t* packed_light, uint8_t light);
bool nc_terrain_get_block(const nc_terrain_t* terrain, const vkm_ivec3* block_coords, uint16_t* block);
uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain);
void nc_terrain_get_timing_stats(const nc_terrain_t* terrain, nc_terrain_timing_stats_t* stats);
bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer);
void nc_terrain_get_chunk_draws(
        const nc_terrain_t* terrain,
        const vkm_mat4* view_projection,
        nc_renderer_chunk_draw_vec* opaque,
        nc_renderer_chunk_draw_vec* transparent,
        nc_terrain_rendering_stats_t* stats);
nc_renderer_texture_t* nc_terrain_get_textures(const nc_terrain_t* terrain);
bool nc_terrain_raycast(
        const nc_terrain_t* terrain,
        const nc_camera_t* camera,
        float max_distance,
        nc_terrain_raycast_hit_t* hit);
void nc_terrain_get_block_highlight_draw(
        const nc_terrain_t* terrain,
        float time,
        const nc_camera_t* camera,
        nc_renderer_block_highlight_draw_t* draw);
void nc_terrain_set_block(nc_terrain_t* terrain, const vkm_ivec3* block_coords, nc_block_type_t new_block);
void nc_terrain_entity_set_block(nc_terrain_t* terrain, const nc_camera_t* camera, nc_block_type_t new_block);
int32_t nc_terrain_get_top_light_blocking_block(
        const nc_terrain_t* terrain,
        const vkm_ivec2 block_column_coords);
void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer);

#endif
