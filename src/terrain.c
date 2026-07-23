#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <novacube/macros.h>
NC_IGNORE_ALL_WARNINGS_BEGIN
#include <FastNoiseLite.h>
NC_IGNORE_ALL_WARNINGS_END
#include <SDL3/SDL.h>
#include <rapidhash.h>

#include <novacube/camera.h>
#include <novacube/cvar.h>
#include <novacube/cvkm.h>
#include <novacube/mesher.h>
#include <novacube/standard_functions.h>
#include <novacube/terrain.h>
#include <novacube/terrain_generation.h>
#include <novacube/terrain_lighting.h>

#ifdef ANDROID
#define NC__TERRAIN_ASSETS_BASE_PATH ""
#define NC__TERRAIN_TEXTURE_EXTENSION ".astc"
#else
#define NC__TERRAIN_ASSETS_BASE_PATH "assets/"
#define NC__TERRAIN_TEXTURE_EXTENSION ".png"
#endif

#define NC__TERRAIN_BLOCK_LIGHT_MASK 0x0f
#define NC__TERRAIN_SKY_LIGHT_SHIFT 4

#define TDS_TYPE light_node_queue
#define TDS_BIT_COUNT NC_BLOCKS_PER_CHUNK
#include <tds/bitset.h>

typedef uint8_t nc__terrain_chunk_flags_t;
enum {
    // The chunk's CPU/GPU mesh no longer matches its blocks, light, or loaded neighbors, and mesh_queue contains one
    // rebuild ticket for its coordinates. Repeated invalidations do not add tickets. The bit is cleared immediately
    // before that ticket is rebuilt; a duplicate ticket left by unloading/reloading the same coordinates is ignored.
    NC__TERRAIN_CHUNK_MESH_PENDING_BIT = 1 << 0,
};

typedef enum nc__terrain_light_channel_t {
    NC__TERRAIN_LIGHT_CHANNEL_BLOCK,
    NC__TERRAIN_LIGHT_CHANNEL_SKY,
    NC__TERRAIN_LIGHT_CHANNEL_COUNT,
} nc__terrain_light_channel_t;

typedef enum nc__terrain_sky_light_reconcile_mode_t {
    NC__TERRAIN_SKY_LIGHT_RECONCILE_SEEDED,
    NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL,
} nc__terrain_sky_light_reconcile_mode_t;

typedef struct nc__terrain_sky_light_chunk_range_t {
    int32_t lower_top;
    int32_t upper_top;
    int32_t first_chunk_y;
    int32_t last_chunk_y;
} nc__terrain_sky_light_chunk_range_t;

typedef struct nc__terrain_frustum_plane_t {
    float x;
    float y;
    float z;
    float distance;
    float aabb_radius;
} nc__terrain_frustum_plane_t;

typedef struct nc__terrain_light_node_t {
    // Keep coordinates rather than a chunk pointer because edits can remain queued after the chunk is unloaded.
    vkm_ivec3 chunk_coords;
    uint16_t index;
} nc__terrain_light_node_t;

typedef struct nc__terrain_light_removal_node_t {
    vkm_ivec3 chunk_coords;
    uint16_t index;
    uint8_t light_level;
} nc__terrain_light_removal_node_t;

typedef struct nc__terrain_block_location_t {
    nc_chunk_t* chunk;
    uint16_t index;
} nc__terrain_block_location_t;

static uint64_t nc__terrain_hash_chunk_coords(const vkm_ivec3* coords) {
    return rapidhashNano(coords, sizeof(*coords));
}

static uint64_t nc__terrain_hash_chunk_xz_coords(const vkm_ivec2* coords) {
    return rapidhashNano(coords, sizeof(*coords));
}

#define TDS_TYPE nc__terrain_chunk_map
#define TDS_KEY_T vkm_ivec3
#define TDS_VALUE_T nc_chunk_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y && (a).z == (b).z)
#include <tds/hashmap.h>

#define nc__terrain_chunk_column_dirty_bitset_get nc_chunk_column_dirty_bitset_get
#define nc__terrain_chunk_column_dirty_bitset_set nc_chunk_column_dirty_bitset_set
#define nc__terrain_chunk_column_dirty_bitset_clear nc_chunk_column_dirty_bitset_clear
#define nc__terrain_chunk_column_dirty_bitset_set_all nc_chunk_column_dirty_bitset_set_all
#define nc__terrain_chunk_column_dirty_bitset_any nc_chunk_column_dirty_bitset_any

#define TDS_TYPE nc__terrain_chunk_column_map
#define TDS_KEY_T vkm_ivec2
#define TDS_VALUE_T nc_chunk_column_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_xz_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y)
#include <tds/hashmap.h>

#define TDS_TYPE nc__terrain_light_bfs_queue
#define TDS_VALUE_T nc__terrain_light_node_t
#include <tds/queue.h>

#define TDS_TYPE nc__terrain_light_removal_bfs_queue
#define TDS_VALUE_T nc__terrain_light_removal_node_t
#include <tds/queue.h>

#define TDS_TYPE nc__terrain_stream_queue
#define TDS_VALUE_T vkm_ivec3
#include <tds/queue.h>

typedef struct nc_terrain_t {
    nc__terrain_chunk_map chunks;
    nc__terrain_chunk_column_map chunk_columns;
    nc_renderer_texture_t* texture_array;
    nc_block_registry_t* block_registry;
    // The built-in generator is part of this terrain's identity and lifetime. A polymorphic generator can replace
    // this value later without imposing a second allocation today.
    nc_terrain_generator_t generator;
    // Coordinate-only queues deliberately tolerate stale entries: consumers recheck current residency and range.
    // clear() cancels pending work in O(1) while retaining queue storage; it never changes loaded chunks by itself.
    nc__terrain_stream_queue load_queue;
    nc__terrain_stream_queue unload_queue;
    // Chunks set MESH_PENDING before adding their coordinate, preventing duplicate rebuild work while loaded.
    nc__terrain_stream_queue mesh_queue;
    vkm_ivec3 streaming_center;
    uint16_t last_streaming_radius_xz;
    uint16_t last_streaming_radius_y;
    uint16_t last_streaming_hysteresis;
    bool streaming_center_is_valid;
    nc__terrain_light_bfs_queue light_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_COUNT];
    nc__terrain_light_removal_bfs_queue light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_COUNT];
    nc_terrain_lighting_t lighting;
    uint64_t prepare_budget_ns;
    nc_terrain_timing_stats_t timing_stats;
} nc_terrain_t;

#define sky_light_has_propagated lighting.sky_light_has_propagated
#define sky_light_needs_rebuild lighting.sky_light_needs_rebuild
#define sky_light_frontier_queued lighting.sky_light_frontier_queued

static const vkm_bvec3 nc__terrain_light_neighbor_offsets[] = {
    { { -1,  0,  0 } },
    { {  1,  0,  0 } },
    { {  0, -1,  0 } },
    { {  0,  1,  0 } },
    { {  0,  0, -1 } },
    { {  0,  0,  1 } },
};

#define nc__terrain_get_light(packed, channel) \
        nc_terrain_light_get((packed), (nc_terrain_light_channel_t)(channel))
#define nc__terrain_set_light(packed, channel, light) \
        nc_terrain_light_set((packed), (nc_terrain_light_channel_t)(channel), (light))
#define nc__terrain_get_block_light nc_terrain_light_get_block
#define nc__terrain_get_sky_light nc_terrain_light_get_sky
#define nc__terrain_set_block_light nc_terrain_light_set_block
#define nc__terrain_set_sky_light nc_terrain_light_set_sky

static void nc__terrain_update_chunk_column_from_chunk(
    const nc_terrain_t* terrain,
    nc_chunk_column_t* column,
    const nc_chunk_t* chunk
) {
    nc_chunk_column_include_chunk(column, chunk, terrain->block_registry);
}

// Returns NULL if the chunk isn't loaded.
static nc_chunk_t* nc__terrain_get_chunk(const nc_terrain_t* terrain, const vkm_ivec3* coords) {
    nc_chunk_t** chunk = nc__terrain_chunk_map_get(&terrain->chunks, *coords);
    return chunk ? *chunk : NULL;
}

static nc_chunk_t* nc__terrain_lookup_chunk(void* context, const vkm_ivec3* coords) {
    return nc__terrain_get_chunk(context, coords);
}

static void nc__terrain_update_dirty_chunk_column(
    const nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    nc_chunk_column_t* column
) {
    nc_chunk_column_update_dirty(
            column,
            &column_coords,
            terrain->block_registry,
            nc__terrain_lookup_chunk,
            (void*)terrain);
}

static const char* nc__terrain_texture_paths[] = {
    NC__TERRAIN_ASSETS_BASE_PATH "textures/stone" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/dirt" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/grass" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/torch" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/torch-top" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/testbox" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/water" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/sand" NC__TERRAIN_TEXTURE_EXTENSION,
};

static void nc__terrain_mark_chunk_dirty(nc_terrain_t* terrain, nc_chunk_t* chunk) {
    if (chunk->flags & NC__TERRAIN_CHUNK_MESH_PENDING_BIT) {
        return;
    }
    chunk->flags |= NC__TERRAIN_CHUNK_MESH_PENDING_BIT;
    nc__terrain_stream_queue_push(&terrain->mesh_queue, chunk->coords);
}

// Loading, replacing, or unloading a whole chunk can change face visibility, corner lighting, and ambient occlusion
// samples in any of the 26 adjacent chunks, so the complete 3x3x3 neighborhood must be rebuilt.
static void nc__terrain_mark_chunk_and_neighbors_dirty(nc_terrain_t* terrain, const vkm_ivec3* coords) {
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vkm_ivec3 neighbor_coords;
                if (!nc_chunk_offset_coords(coords, x, y, z, &neighbor_coords)) {
                    continue;
                }

                nc_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
                if (neighbor) {
                    nc__terrain_mark_chunk_dirty(terrain, neighbor);
                }
            }
        }
    }
}

// A block edit can only affect chunks whose one-block meshing border contains the edited block.
static void nc__terrain_mark_block_chunks_dirty(
    nc_terrain_t* terrain,
    const vkm_ivec3* chunk_coords,
    const vkm_ivec3* local_coords
) {
    const int min_x = local_coords->x == 0 ? -1 : 0;
    const int max_x = local_coords->x == NC_CHUNK_SIZE - 1 ? 1 : 0;
    const int min_y = local_coords->y == 0 ? -1 : 0;
    const int max_y = local_coords->y == NC_CHUNK_SIZE - 1 ? 1 : 0;
    const int min_z = local_coords->z == 0 ? -1 : 0;
    const int max_z = local_coords->z == NC_CHUNK_SIZE - 1 ? 1 : 0;

    for (int z = min_z; z <= max_z; z++) {
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                vkm_ivec3 affected_coords;
                if (!nc_chunk_offset_coords(chunk_coords, x, y, z, &affected_coords)) {
                    continue;
                }

                nc_chunk_t* affected = nc__terrain_get_chunk(terrain, &affected_coords);
                if (affected) {
                    nc__terrain_mark_chunk_dirty(terrain, affected);
                }
            }
        }
    }
}

// TODO: what...
#include "terrain_lighting.c"

static bool nc__terrain_normal_is_zero(const vkm_bvec3 normal) {
    return normal.x == 0 && normal.y == 0 && normal.z == 0;
}

bool nc_terrain_get_block(const nc_terrain_t* terrain, const vkm_ivec3* block_coords, uint16_t* block) {
    vkm_ivec3 chunk_coords;
    nc_block_to_chunk_coords(block_coords, &chunk_coords);
    const nc_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    vkm_ivec3 local_coords;
    nc_block_to_chunk_local_coords(block_coords, &chunk_coords, &local_coords);

    if (block) {
        *block = chunk->blocks[NC_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z)];
    }
    return true;
}

static bool nc__terrain_set_world_block(nc_terrain_t* terrain, const vkm_ivec3* block_coords, const uint16_t block) {
    vkm_ivec3 chunk_coords;
    nc_block_to_chunk_coords(block_coords, &chunk_coords);
    nc_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    vkm_ivec3 local_coords;
    nc_block_to_chunk_local_coords(block_coords, &chunk_coords, &local_coords);

    const uint16_t index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z);
    uint16_t* slot = &chunk->blocks[index];
    if (*slot != block) {
        const nc_block_t* old_block = nc_block_registry_get(terrain->block_registry, (nc_block_type_t)*slot);
        const uint8_t old_light_level = nc__terrain_get_block_light(chunk->light_levels[index]);
        const uint8_t old_sky_light_level = nc__terrain_get_sky_light(chunk->light_levels[index]);
        *slot = block;

        const nc_block_t* new_block = nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block);
        if (!terrain->sky_light_has_propagated
                && (old_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
                        != (new_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)) {
            // Bulk terrain construction is cheaper and safer to resolve in one pass before the first render.
            terrain->sky_light_needs_rebuild = true;
        }
        if (old_block->light_emission != new_block->light_emission
                || (old_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
                        != (new_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)) {
            if (old_light_level) {
                nc__terrain_light_removal_bfs_queue_push(
                        &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_BLOCK],
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = chunk_coords,
                            .index = index,
                            .light_level = old_light_level,
                        });
            }

            nc__terrain_set_block_light(&chunk->light_levels[index], new_block->light_emission);
            if (new_block->light_emission) {
                const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &location);
            }

            // Requeue nearby surviving light. This is necessary when an opaque block becomes transparent and also
            // refills any gaps left by the removal pass.
            nc__terrain_queue_light_neighbors(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, chunk, index, &local_coords);
        }

        nc_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                &terrain->chunk_columns,
                nc_chunk_to_chunk_column_coords(&chunk_coords));
        NC_ASSERT(column);

        const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(local_coords.x, local_coords.z);
        if (new_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT) {
            if (old_sky_light_level
                    && terrain->sky_light_has_propagated
                    && !terrain->sky_light_needs_rebuild) {
                nc__terrain_light_removal_bfs_queue_push(
                        &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY],
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = chunk_coords,
                            .index = index,
                            .light_level = old_sky_light_level,
                        });
            }
            nc__terrain_set_sky_light(&chunk->light_levels[index], 0);
            if (block_coords->y > (*column)->top_light_blocking_blocks[column_index]) {
                (*column)->top_light_blocking_blocks[column_index] = block_coords->y;
            }
        } else if (old_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT
                && block_coords->y == (*column)->top_light_blocking_blocks[column_index]) {
            nc__terrain_chunk_column_dirty_bitset_set(
                    &(*column)->dirty_top_light_blocking_blocks,
                    column_index);
            nc__terrain_update_dirty_chunk_column(
                    terrain,
                    nc_chunk_to_chunk_column_coords(&chunk_coords),
                    *column);
        }

        if (!terrain->sky_light_needs_rebuild) {
            if (!(new_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
                    && block_coords->y > (*column)->top_light_blocking_blocks[column_index]) {
                nc__terrain_set_sky_light(&chunk->light_levels[index], 15);
                const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, &location);
            }
            if (!(new_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)) {
                nc__terrain_queue_light_neighbors(
                        terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, chunk, index, &local_coords);
            }
            nc__terrain_reconcile_block_column_sky_light(
                    terrain,
                    nc_chunk_to_chunk_column_coords(&chunk_coords),
                    *column,
                    local_coords.x,
                    local_coords.z);
        }

        nc__terrain_mark_block_chunks_dirty(terrain, &chunk_coords, &local_coords);
    }

    return true;
}

// The blocks pointer is optional. If NULL, then the chunk will be filled with air.
static void nc__terrain_load_or_replace_chunk(
    nc_terrain_t* terrain,
    const vkm_ivec3* coords,
    const uint16_t blocks[NC_BLOCKS_PER_CHUNK]
) {
    NC_ASSERT(nc_chunk_coords_are_valid(coords));

    nc_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (chunk) {
        int32_t old_top_light_blocking_blocks[NC_BLOCK_COLUMNS_PER_CHUNK];
        nc__terrain_queue_chunk_boundary_light_removal(terrain, chunk);
        memset(chunk->light_levels, 0, sizeof(chunk->light_levels));
        nc_chunk_replace_blocks(chunk, blocks);
        nc_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                &terrain->chunk_columns,
                nc_chunk_to_chunk_column_coords(coords));
        NC_ASSERT(column);
        memcpy(old_top_light_blocking_blocks,
                (*column)->top_light_blocking_blocks,
                sizeof(old_top_light_blocking_blocks));
        nc__terrain_chunk_column_dirty_bitset_set_all(&(*column)->dirty_top_light_blocking_blocks);
        nc__terrain_update_dirty_chunk_column(terrain, nc_chunk_to_chunk_column_coords(coords), *column);
        nc__terrain_reconcile_chunk_column_sky_light(
                terrain,
                nc_chunk_to_chunk_column_coords(coords),
                *column,
                old_top_light_blocking_blocks);
        nc__terrain_seed_chunk_light(terrain, chunk);
        nc__terrain_seed_chunk_sky_light(terrain, chunk);
        if (terrain->sky_light_has_propagated) {
            nc__terrain_queue_chunk_sky_light_frontier(terrain, chunk);
        }
        nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
        return;
    }

    chunk = nc_chunk_init(NULL, coords, blocks);

    const int chunk_was_added = nc__terrain_chunk_map_set(&terrain->chunks, *coords, chunk);
    NC_ASSERT(chunk_was_added);
    (void)chunk_was_added;

    const vkm_ivec2 column_coords = nc_chunk_to_chunk_column_coords(coords);
    nc_chunk_column_t** column = nc__terrain_chunk_column_map_get(
            &terrain->chunk_columns,
            column_coords);
    int32_t old_top_light_blocking_blocks[NC_BLOCK_COLUMNS_PER_CHUNK];
    if (column) {
        memcpy(old_top_light_blocking_blocks,
                (*column)->top_light_blocking_blocks,
                sizeof(old_top_light_blocking_blocks));
        NC_ASSERT((*column)->ref_count < UINT32_MAX);
        (*column)->ref_count++;
        if (coords->y < (*column)->min_loaded_chunk_y) {
            (*column)->min_loaded_chunk_y = coords->y;
        }
        if (coords->y > (*column)->max_loaded_chunk_y) {
            (*column)->max_loaded_chunk_y = coords->y;
        }
        nc__terrain_update_chunk_column_from_chunk(terrain, *column, chunk);
    } else {
        nc_chunk_column_t* new_column = nc_chunk_column_init(chunk);
        nc__terrain_update_chunk_column_from_chunk(terrain, new_column, chunk);
        const int column_was_added = nc__terrain_chunk_column_map_set(
                &terrain->chunk_columns,
                column_coords,
                new_column);
        NC_ASSERT(column_was_added);
        (void)column_was_added;
        memcpy(
                old_top_light_blocking_blocks,
                new_column->top_light_blocking_blocks,
                sizeof(old_top_light_blocking_blocks));
        column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    }
    NC_ASSERT(column);
    nc__terrain_reconcile_chunk_column_sky_light(
            terrain,
            column_coords,
            *column,
            old_top_light_blocking_blocks);
    nc__terrain_seed_chunk_light(terrain, chunk);
    nc__terrain_seed_chunk_sky_light(terrain, chunk);
    if (terrain->sky_light_has_propagated) {
        nc__terrain_queue_chunk_sky_light_frontier(terrain, chunk);
    }
    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
}

static void nc__terrain_unload_chunk(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_ivec3* coords) {
    NC_ASSERT(nc_chunk_coords_are_valid(coords));

    nc_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (!chunk) {
        return;
    }

    const vkm_ivec2 column_coords = nc_chunk_to_chunk_column_coords(coords);
    nc_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    NC_ASSERT(column && (*column)->ref_count > 0);
    if (nc__terrain_chunk_column_dirty_bitset_any(&(*column)->dirty_top_light_blocking_blocks)) {
        nc__terrain_update_dirty_chunk_column(terrain, column_coords, *column);
    }

    // WARNING: Unloaded chunks provide no new light, but unloading never retracts light already propagated into
    // remaining chunks. Future light-sensitive gameplay must not run in chunks touching an unloaded chunk; their
    // lighting is deliberately an unresolved streaming-boundary approximation.

    nc__terrain_chunk_map_remove(&terrain->chunks, *coords);

    (*column)->ref_count--;
    if ((*column)->ref_count == 0) {
        nc_chunk_column_fini(*column);
        const int column_was_removed = nc__terrain_chunk_column_map_remove(&terrain->chunk_columns, column_coords);
        NC_ASSERT(column_was_removed);
        (void)column_was_removed;
    } else if (coords->y == (*column)->min_loaded_chunk_y || coords->y == (*column)->max_loaded_chunk_y) {
        // Only an extreme can change. Find the new min/max chunk.
        if (coords->y == (*column)->min_loaded_chunk_y) {
            int32_t new_min_loaded_chunk_y = INT32_MAX;
            for (int64_t y = (int64_t)coords->y + 1; y <= (*column)->max_loaded_chunk_y; y++) {
                const vkm_ivec3 candidate_coords = { { coords->x, (int32_t)y, coords->z } };
                if (nc__terrain_get_chunk(terrain, &candidate_coords)) {
                    new_min_loaded_chunk_y = (int32_t)y;
                    break;
                }
            }
            NC_ASSERT(new_min_loaded_chunk_y != INT32_MAX);
            (*column)->min_loaded_chunk_y = new_min_loaded_chunk_y;
        }
        if (coords->y == (*column)->max_loaded_chunk_y) {
            int32_t new_max_loaded_chunk_y = INT32_MIN;
            for (int64_t y = (int64_t)coords->y - 1; y >= (*column)->min_loaded_chunk_y; y--) {
                const vkm_ivec3 candidate_coords = { { coords->x, (int32_t)y, coords->z } };
                if (nc__terrain_get_chunk(terrain, &candidate_coords)) {
                    new_max_loaded_chunk_y = (int32_t)y;
                    break;
                }
            }
            NC_ASSERT(new_max_loaded_chunk_y != INT32_MIN);
            (*column)->max_loaded_chunk_y = new_max_loaded_chunk_y;
        }
    }

    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
    nc_chunk_fini(renderer, chunk);
}

uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain) {
    return nc__terrain_chunk_map_count(&terrain->chunks);
}

static bool nc__terrain_chunk_is_in_ellipsoid(
    const vkm_ivec3* coords,
    const vkm_ivec3* center,
    const uint32_t radius_xz,
    const uint32_t radius_y
) {
    const int64_t dx = (int64_t)coords->x - center->x;
    const int64_t dy = (int64_t)coords->y - center->y;
    const int64_t dz = (int64_t)coords->z - center->z;
    if ((radius_xz == 0 && (dx != 0 || dz != 0)) || (radius_y == 0 && dy != 0)) {
        return false;
    }

    double distance = 0.0;
    if (radius_xz) {
        distance += ((double)dx * (double)dx + (double)dz * (double)dz)
                / ((double)radius_xz * (double)radius_xz);
    }
    if (radius_y) {
        distance += (double)dy * (double)dy / ((double)radius_y * (double)radius_y);
    }
    return distance <= 1.0;
}

static uint32_t nc__terrain_abs_chunk_delta(const int32_t a, const int32_t b) {
    const int64_t delta = (int64_t)a - b;
    return (uint32_t)(delta < 0 ? -delta : delta);
}

// Appends every currently missing chunk in the load ellipsoid, nearest shells first. "Shell" is the integer
// Chebyshev distance max(|dx|, |dy|, |dz|) from center. Iterating shell before y/z/x makes the FIFO load the center,
// then its surrounding cube surface, and so on instead of drawing a snake from one corner of the bounding box.
//
// The four loops are therefore one priority loop plus three coordinate loops. The coordinate loops visit the
// ellipsoid's bounding box; the shell-distance and ellipsoid tests reject points that do not belong to this shell or
// to the ellipsoid. This repeats the bounding-box walk per shell, but queue construction is infrequent and the simple
// ordering avoids another sortable work array.
static void nc__terrain_enqueue_load_region(
    nc_terrain_t* terrain,
    const vkm_ivec3* center,
    const uint32_t radius_xz,
    const uint32_t radius_y
) {
    const uint32_t max_radius = radius_xz > radius_y ? radius_xz : radius_y;
    for (uint32_t shell = 0; shell <= max_radius; shell++) {
        for (int64_t y = (int64_t)center->y - radius_y; y <= (int64_t)center->y + radius_y; y++) {
            for (int64_t z = (int64_t)center->z - radius_xz; z <= (int64_t)center->z + radius_xz; z++) {
                for (int64_t x = (int64_t)center->x - radius_xz; x <= (int64_t)center->x + radius_xz; x++) {
                    if (x < NC_MIN_CHUNK_COORD || x > NC_MAX_CHUNK_COORD
                            || y < NC_MIN_CHUNK_COORD || y > NC_MAX_CHUNK_COORD
                            || z < NC_MIN_CHUNK_COORD || z > NC_MAX_CHUNK_COORD) {
                        continue;
                    }
                    const vkm_ivec3 coords = { { (int32_t)x, (int32_t)y, (int32_t)z } };
                    const uint32_t dx = nc__terrain_abs_chunk_delta(coords.x, center->x);
                    const uint32_t dy = nc__terrain_abs_chunk_delta(coords.y, center->y);
                    const uint32_t dz = nc__terrain_abs_chunk_delta(coords.z, center->z);
                    const uint32_t shell_distance = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
                    if (shell_distance != shell
                            || !nc__terrain_chunk_is_in_ellipsoid(&coords, center, radius_xz, radius_y)
                            || nc__terrain_get_chunk(terrain, &coords)) {
                        continue;
                    }
                    nc__terrain_stream_queue_push(&terrain->load_queue, coords);
                }
            }
        }
    }
}

// Enqueues only the retention-ellipsoid slice abandoned by an ordinary one-chunk move. Iterating the old bounding
// box is bounded by the configured radii and avoids distance-testing the entire loaded hashmap. Queue entries store
// coordinates, so a later move or unload cannot leave dangling chunk pointers; pop-time validation rejects stale
// entries if the player moves back into their retention range.
static void nc__terrain_enqueue_incremental_unloads(
    nc_terrain_t* terrain,
    const vkm_ivec3* old_center,
    const vkm_ivec3* new_center,
    const uint32_t radius_xz,
    const uint32_t radius_y
) {
    for (int64_t y = (int64_t)old_center->y - radius_y; y <= (int64_t)old_center->y + radius_y; y++) {
        for (int64_t z = (int64_t)old_center->z - radius_xz; z <= (int64_t)old_center->z + radius_xz; z++) {
            for (int64_t x = (int64_t)old_center->x - radius_xz; x <= (int64_t)old_center->x + radius_xz; x++) {
                if (x < NC_MIN_CHUNK_COORD || x > NC_MAX_CHUNK_COORD
                        || y < NC_MIN_CHUNK_COORD || y > NC_MAX_CHUNK_COORD
                        || z < NC_MIN_CHUNK_COORD || z > NC_MAX_CHUNK_COORD) {
                    continue;
                }
                const vkm_ivec3 coords = { { (int32_t)x, (int32_t)y, (int32_t)z } };
                if (nc__terrain_chunk_is_in_ellipsoid(&coords, old_center, radius_xz, radius_y)
                        && !nc__terrain_chunk_is_in_ellipsoid(&coords, new_center, radius_xz, radius_y)) {
                    nc__terrain_stream_queue_push(&terrain->unload_queue, coords);
                }
            }
        }
    }
}

static void nc__terrain_rebuild_streaming_queues(
    nc_terrain_t* terrain,
    const vkm_ivec3* center,
    const uint32_t retain_radius_xz,
    const uint32_t retain_radius_y
) {
    // Clearing a TDS queue drops pending entries but retains its allocation. It does not load/unload any chunk.
    // Teleports invalidate both old priorities: rebuild unloads from actual residency and loads around the new center.
    nc__terrain_stream_queue_clear(&terrain->load_queue);
    nc__terrain_stream_queue_clear(&terrain->unload_queue);

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        if (!nc__terrain_chunk_is_in_ellipsoid(&it.key, center, retain_radius_xz, retain_radius_y)) {
            nc__terrain_stream_queue_push(&terrain->unload_queue, it.key);
        }
    }
    nc__terrain_enqueue_load_region(
            terrain,
            center,
            terrain->last_streaming_radius_xz,
            terrain->last_streaming_radius_y);
}

void nc_terrain_update(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_vec3* player_position) {
    const uint64_t start = SDL_GetTicksNS();
    terrain->timing_stats = (nc_terrain_timing_stats_t){ 0 };
    terrain->prepare_budget_ns = 0;
    vkm_ivec3 center;
    if (!nc_world_position_to_chunk_coords(player_position, &center)) {
        return;
    }

    const uint16_t radius_xz = nc_cvar_get_terrain_load_radius_xz();
    const uint16_t radius_y = nc_cvar_get_terrain_load_radius_y();
    const uint16_t hysteresis = nc_cvar_get_terrain_hysteresis();
    const uint32_t retain_radius_xz = (uint32_t)radius_xz + hysteresis;
    const uint32_t retain_radius_y = (uint32_t)radius_y + hysteresis;
    const bool configuration_changed = radius_xz != terrain->last_streaming_radius_xz
            || radius_y != terrain->last_streaming_radius_y
            || hysteresis != terrain->last_streaming_hysteresis;
    const bool center_changed = !terrain->streaming_center_is_valid
            || center.x != terrain->streaming_center.x
            || center.y != terrain->streaming_center.y
            || center.z != terrain->streaming_center.z;

    if (center_changed || configuration_changed) {
        const bool teleport = !terrain->streaming_center_is_valid || configuration_changed
                || nc__terrain_abs_chunk_delta(center.x, terrain->streaming_center.x) > 1
                || nc__terrain_abs_chunk_delta(center.y, terrain->streaming_center.y) > 1
                || nc__terrain_abs_chunk_delta(center.z, terrain->streaming_center.z) > 1;
        const vkm_ivec3 old_center = terrain->streaming_center;
        terrain->streaming_center = center;
        terrain->last_streaming_radius_xz = radius_xz;
        terrain->last_streaming_radius_y = radius_y;
        terrain->last_streaming_hysteresis = hysteresis;
        terrain->streaming_center_is_valid = true;

        if (teleport) {
            nc__terrain_rebuild_streaming_queues(terrain, &center, retain_radius_xz, retain_radius_y);
        } else {
            nc__terrain_enqueue_incremental_unloads(
                    terrain, &old_center, &center, retain_radius_xz, retain_radius_y);
            // A FIFO assembled over several positions makes terrain crawl along the old path. Refresh only the
            // missing-load backlog so the current center is first. Queue clear cancels work only; resident chunks
            // remain in the hashmap, and enqueue_load_region filters them with coordinate lookups.
            nc__terrain_stream_queue_clear(&terrain->load_queue);
            nc__terrain_enqueue_load_region(terrain, &center, radius_xz, radius_y);
        }
    }

    // TODO: Make the streaming budget adaptable with FPS.
    // In the future, when we have a proper server-client, it will be adaptable to tickrate instead of FPS.
    const double budget_ms = nc_cvar_get_terrain_streaming_budget_ms();
    if (!(budget_ms > 0.0)) {
        return;
    }
    const uint64_t budget_ns = budget_ms >= (double)UINT64_MAX / 1000000.0
            ? UINT64_MAX
            : (uint64_t)(budget_ms * 1000000.0);
    // Residency owns the first half of the configured terrain budget; the other half is carried into lighting and
    // meshing by prepare_budget_ns. Within residency, reserve half (one quarter of the total) for unloads so an
    // expensive generation cannot starve memory reclamation. Unloads run first and donate unused time to loads.
    // Each chunk operation is atomic, so one operation may overrun its boundary; the next operation never starts
    // after the relevant deadline.
    const uint64_t residency_budget_ns = budget_ns / 2;
    const uint64_t residency_start = SDL_GetTicksNS();
    const uint64_t unload_budget_ns = residency_budget_ns / 2;
    while (nc__terrain_stream_queue_count(&terrain->unload_queue)
            && SDL_GetTicksNS() - residency_start < unload_budget_ns) {
        const vkm_ivec3 coords = nc__terrain_stream_queue_pop(&terrain->unload_queue);
        if (nc__terrain_get_chunk(terrain, &coords)
                && !nc__terrain_chunk_is_in_ellipsoid(
                        &coords, &terrain->streaming_center, retain_radius_xz, retain_radius_y)) {
            nc__terrain_unload_chunk(terrain, renderer, &coords);
        }
    }
    const uint64_t unloading_end = SDL_GetTicksNS();
    terrain->timing_stats.unloading_ms = (double)(unloading_end - residency_start) / 1000000.0;

    // Unused unload time is donated to loading, but loading can never consume the unload reservation.
    // This ensures the memory usage never keeps increasing until the time comes to unload chunks.
    while (nc__terrain_stream_queue_count(&terrain->load_queue)
            && SDL_GetTicksNS() - residency_start < residency_budget_ns) {
        const vkm_ivec3 coords = nc__terrain_stream_queue_pop(&terrain->load_queue);
        if (!nc__terrain_get_chunk(terrain, &coords)
                && nc__terrain_chunk_is_in_ellipsoid(
                        &coords, &terrain->streaming_center, radius_xz, radius_y)) {
            uint16_t blocks[NC_BLOCKS_PER_CHUNK];
            nc_terrain_generator_generate_chunk(&terrain->generator, &coords, blocks);
            nc__terrain_load_or_replace_chunk(terrain, &coords, blocks);
        }
    }
    terrain->timing_stats.loading_ms = (double)(SDL_GetTicksNS() - unloading_end) / 1000000.0;
    const uint64_t elapsed = SDL_GetTicksNS() - start;
    terrain->timing_stats.residency_ms = (double)elapsed / 1000000.0;
    terrain->prepare_budget_ns = elapsed < budget_ns ? budget_ns - elapsed : 0;
}

void nc_terrain_get_timing_stats(const nc_terrain_t* terrain, nc_terrain_timing_stats_t* stats) {
    *stats = terrain->timing_stats;
}

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer) {
    nc_terrain_t* result = calloc(1, sizeof(*result));

    if (!((result->texture_array = nc_renderer_create_texture_array_from_files(
            renderer,
            NC_RENDERER_TEXTURE_TYPE_COLOR,
            nc__terrain_texture_paths,
            NC_COUNTOF(nc__terrain_texture_paths))))) {
        goto error;
    }

    if (!((result->block_registry = nc_block_registry_init()))) {
        goto error;
    }

    result->generator.noise_state = fnlCreateState();
    result->generator.noise_state.octaves = 3;
    result->generator.noise_state.fractal_type = FNL_FRACTAL_FBM;

    return result;

error:
    nc_terrain_fini(result, renderer);
    return NULL;
}

static void nc__terrain_get_chunk_data_and_neighbors(
    const nc_terrain_t* terrain,
    const vkm_ivec3* coords,
    const uint16_t* blocks[3][3][3],
    const uint8_t* light_levels[3][3][3]
) {
    memset(blocks, 0, sizeof(const uint16_t*) * 3 * 3 * 3);
    memset(light_levels, 0, sizeof(const uint8_t*) * 3 * 3 * 3);

    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vkm_ivec3 neighbor_coords;
                if (!nc_chunk_offset_coords(coords, x, y, z, &neighbor_coords)) {
                    continue;
                }

                const nc_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
                if (neighbor) {
                    blocks[x + 1][y + 1][z + 1] = neighbor->blocks;
                    light_levels[x + 1][y + 1][z + 1] = neighbor->light_levels;
                }
            }
        }
    }
}

static bool nc__terrain_remesh(const nc_terrain_t* terrain, nc_renderer_t* renderer, nc_chunk_t* chunk) {
    const uint16_t* chunk_and_neighbors[3][3][3];
    const uint8_t* light_levels_and_neighbors[3][3][3];
    nc__terrain_get_chunk_data_and_neighbors(terrain, &chunk->coords, chunk_and_neighbors, light_levels_and_neighbors);

    return nc_mesher_upload_chunk(
            terrain->block_registry,
            renderer,
            chunk,
            chunk_and_neighbors,
            light_levels_and_neighbors);
}


bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    const uint64_t prepare_start = SDL_GetTicksNS();
    const uint64_t deadline = UINT64_MAX - prepare_start < terrain->prepare_budget_ns
            ? UINT64_MAX
            : prepare_start + terrain->prepare_budget_ns;
    const bool rebuild_sky_light = terrain->sky_light_needs_rebuild;
    if (!nc__terrain_remove_light(terrain, deadline)) {
        terrain->timing_stats.lighting_ms = (double)(SDL_GetTicksNS() - prepare_start) / 1000000.0;
        return true;
    }
    if (rebuild_sky_light) {
        // Bulk edits before the first propagation pass are cheaper to resolve once than as a removal wave per block.
        nc__terrain_light_bfs_queue_clear(&terrain->light_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY]);
        nc__terrain_light_removal_bfs_queue_clear(&terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY]);
        nc__terrain_chunk_map_iter_t clear_it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&clear_it)) {
            nc_chunk_t* chunk = *clear_it.value;
            for (int i = 0; i < NC_BLOCKS_PER_CHUNK; i++) {
                nc__terrain_set_sky_light(&chunk->light_levels[i], 0);
            }
            free(chunk->queued_light_nodes[NC__TERRAIN_LIGHT_CHANNEL_SKY]);
            chunk->queued_light_nodes[NC__TERRAIN_LIGHT_CHANNEL_SKY] = NULL;
            nc__terrain_mark_chunk_and_neighbors_dirty(terrain, &chunk->coords);
        }
        nc__terrain_chunk_map_iter_t seed_it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&seed_it)) {
            nc__terrain_seed_chunk_sky_light(terrain, *seed_it.value);
        }
        terrain->sky_light_needs_rebuild = false;
        terrain->sky_light_frontier_queued = false;
    } else {
        if (!nc__terrain_remove_sky_light(terrain, deadline)) {
            terrain->timing_stats.lighting_ms = (double)(SDL_GetTicksNS() - prepare_start) / 1000000.0;
            return true;
        }
    }
    if ((rebuild_sky_light || !terrain->sky_light_has_propagated) && !terrain->sky_light_frontier_queued) {
        nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&it)) {
            nc__terrain_queue_chunk_sky_light_frontier(terrain, *it.value);
        }
        terrain->sky_light_frontier_queued = true;
    }
    if (!nc__terrain_propagate_light(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, deadline)
            || !nc__terrain_propagate_light(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, deadline)) {
        terrain->timing_stats.lighting_ms = (double)(SDL_GetTicksNS() - prepare_start) / 1000000.0;
        return true;
    }
    terrain->sky_light_has_propagated = true;

    const uint64_t lighting_end = SDL_GetTicksNS();
    terrain->timing_stats.lighting_ms = (double)(lighting_end - prepare_start) / 1000000.0;

    while (nc__terrain_stream_queue_count(&terrain->mesh_queue) && SDL_GetTicksNS() < deadline) {
        const vkm_ivec3 coords = nc__terrain_stream_queue_pop(&terrain->mesh_queue);
        nc_chunk_t* chunk = nc__terrain_get_chunk(terrain, &coords);
        if (!chunk) {
            continue;
        }
        if (!(chunk->flags & NC__TERRAIN_CHUNK_MESH_PENDING_BIT)) {
            continue;
        }
        chunk->flags &= ~NC__TERRAIN_CHUNK_MESH_PENDING_BIT;
        if (!nc__terrain_remesh(terrain, renderer, chunk)) {
            return false;
        }
    }
    terrain->timing_stats.meshing_ms = (double)(SDL_GetTicksNS() - lighting_end) / 1000000.0;

    return true;
}

static nc__terrain_frustum_plane_t nc__terrain_make_frustum_plane(
    const float x,
    const float y,
    const float z,
    const float distance
) {
    const float half_chunk_size = (float)NC_CHUNK_SIZE * 0.5f;
    return (nc__terrain_frustum_plane_t){
        .x = x,
        .y = y,
        .z = z,
        .distance = distance,
        .aabb_radius = half_chunk_size * (vkm_abs(x) + vkm_abs(y) + vkm_abs(z)),
    };
}

static bool nc__terrain_chunk_is_outside_frustum(const nc__terrain_frustum_plane_t planes[6], const vkm_vec3* center) {
    for (int i = 0; i < 6; i++) {
        const nc__terrain_frustum_plane_t* plane = &planes[i];
        if (plane->x * center->x + plane->y * center->y + plane->z * center->z
                + plane->distance + plane->aabb_radius < 0.0f) {
            return true;
        }
    }

    return false;
}

void nc_terrain_get_chunk_draws(
    const nc_terrain_t* terrain,
    const vkm_mat4* view_projection,
    nc_renderer_chunk_draw_vec* opaque,
    nc_renderer_chunk_draw_vec* transparent,
    nc_terrain_frustum_culling_stats_t* stats
) {
    const nc__terrain_frustum_plane_t planes[6] = {
        nc__terrain_make_frustum_plane(
                view_projection->m03 + view_projection->m00,
                view_projection->m13 + view_projection->m10,
                view_projection->m23 + view_projection->m20,
                view_projection->m33 + view_projection->m30),
        nc__terrain_make_frustum_plane(
                view_projection->m03 - view_projection->m00,
                view_projection->m13 - view_projection->m10,
                view_projection->m23 - view_projection->m20,
                view_projection->m33 - view_projection->m30),
        nc__terrain_make_frustum_plane(
                view_projection->m03 + view_projection->m01,
                view_projection->m13 + view_projection->m11,
                view_projection->m23 + view_projection->m21,
                view_projection->m33 + view_projection->m31),
        nc__terrain_make_frustum_plane(
                view_projection->m03 - view_projection->m01,
                view_projection->m13 - view_projection->m11,
                view_projection->m23 - view_projection->m21,
                view_projection->m33 - view_projection->m31),
        nc__terrain_make_frustum_plane(
                view_projection->m02,
                view_projection->m12,
                view_projection->m22,
                view_projection->m32),
        nc__terrain_make_frustum_plane(
                view_projection->m03 - view_projection->m02,
                view_projection->m13 - view_projection->m12,
                view_projection->m23 - view_projection->m22,
                view_projection->m33 - view_projection->m32),
    };

    // Keep this struct in cache by using a local variable.
    nc_terrain_frustum_culling_stats_t culling_stats = { 0 };
    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        const nc_chunk_t* chunk = *it.value;
        culling_stats.loaded_chunk_count++;
        if (chunk->opaque_quad_count == 0 && chunk->transparent_quad_count == 0) {
            culling_stats.empty_chunk_count++;
            continue;
        }

        const vkm_ivec3 block_coords = { {
            chunk->coords.x * NC_CHUNK_SIZE,
            chunk->coords.y * NC_CHUNK_SIZE,
            chunk->coords.z * NC_CHUNK_SIZE,
        } };
        const float half_chunk_size = (float)NC_CHUNK_SIZE * 0.5f;
        const vkm_vec3 center = { {
            (float)block_coords.x + half_chunk_size,
            (float)block_coords.y + half_chunk_size,
            (float)block_coords.z + half_chunk_size,
        } };

        if (nc__terrain_chunk_is_outside_frustum(planes, &center)) {
            culling_stats.culled_chunk_count++;
            continue;
        }

        if (chunk->opaque_quad_count) {
            nc_renderer_chunk_draw_vec_append(opaque, (nc_renderer_chunk_draw_t){
                .chunk_buffer = chunk->opaque_quad_buffer,
                .quad_count = chunk->opaque_quad_count,
                .face_data_buffer = chunk->opaque_face_data_buffer,
                .texture = terrain->texture_array,
                .position = { {
                    .x = (float)block_coords.x,
                    .y = (float)block_coords.y,
                    .z = (float)block_coords.z,
                } },
            });
            culling_stats.opaque_drawn_chunk_count++;
        }

        if (chunk->transparent_quad_count) {
            nc_renderer_chunk_draw_vec_append(transparent, (nc_renderer_chunk_draw_t){
                .chunk_buffer = chunk->transparent_quad_buffer,
                .quad_count = chunk->transparent_quad_count,
                .face_data_buffer = chunk->transparent_face_data_buffer,
                .texture = terrain->texture_array,
                .position = { {
                    .x = (float)block_coords.x,
                    .y = (float)block_coords.y,
                    .z = (float)block_coords.z,
                } },
            });
            culling_stats.transparent_drawn_chunk_count++;
        }
    }

    *stats = culling_stats;
}

static float nc__terrain_initial_t_max(
    const float origin,
    const int32_t block,
    const float direction,
    const int step
) {
    if (step > 0) {
        return (float)(((double)block + 1.0 - origin) / direction);
    }
    if (step < 0) {
        return (float)((origin - (double)block) / -direction);
    }

    return FLT_MAX;
}

static bool nc__terrain_step_raycast_axis(int32_t* coord, const int step) {
    if ((step > 0 && *coord == INT32_MAX) || (step < 0 && *coord == INT32_MIN)) {
        return false;
    }

    *coord += step;
    return true;
}

// Amanatides/Woo grid traversal.
bool nc_terrain_raycast(
    const nc_terrain_t* terrain,
    const nc_camera_t* camera,
    const float max_distance,
    nc_terrain_raycast_hit_t* hit
) {
    vkm_vec3 direction;
    vkm_vec3 right;
    vkm_vec3 up;
    nc_camera_get_basis(camera, &direction, &right, &up);

    vkm_ivec3 block_position;
    if (!nc_world_position_coord_to_block_coord(camera->position.x, &block_position.x)
            || !nc_world_position_coord_to_block_coord(camera->position.y, &block_position.y)
            || !nc_world_position_coord_to_block_coord(camera->position.z, &block_position.z)) {
        return false;
    }

    const vkm_bvec3 step = { {
        direction.x > 0.0f ? 1 : direction.x < 0.0f ? -1 : 0,
        direction.y > 0.0f ? 1 : direction.y < 0.0f ? -1 : 0,
        direction.z > 0.0f ? 1 : direction.z < 0.0f ? -1 : 0,
    } };

    const vkm_vec3 t_delta = { {
        step.x ? vkm_abs(1.0f / direction.x) : FLT_MAX,
        step.y ? vkm_abs(1.0f / direction.y) : FLT_MAX,
        step.z ? vkm_abs(1.0f / direction.z) : FLT_MAX,
    } };

    vkm_vec3 t_max = { {
        nc__terrain_initial_t_max(camera->position.x, block_position.x, direction.x, step.x),
        nc__terrain_initial_t_max(camera->position.y, block_position.y, direction.y, step.y),
        nc__terrain_initial_t_max(camera->position.z, block_position.z, direction.z, step.z),
    } };

    float distance = 0.0f;
    vkm_bvec3 normal = { 0 };
    while (distance <= max_distance) {
        uint16_t block;
        if (nc_terrain_get_block(terrain, &block_position, &block) && block != 0) {
            *hit = (nc_terrain_raycast_hit_t){
                .block_position = block_position,
                .normal = normal,
                .distance = distance,
            };
            return true;
        }

        if (t_max.x <= t_max.y && t_max.x <= t_max.z) {
            if (t_max.x > max_distance) {
                break;
            }
            if (!nc__terrain_step_raycast_axis(&block_position.x, step.x)) {
                break;
            }
            distance = t_max.x;
            t_max.x += t_delta.x;
            normal = (vkm_bvec3){ { (int8_t)-step.x, 0, 0 } };
        } else if (t_max.y <= t_max.z) {
            if (t_max.y > max_distance) {
                break;
            }
            if (!nc__terrain_step_raycast_axis(&block_position.y, step.y)) {
                break;
            }
            distance = t_max.y;
            t_max.y += t_delta.y;
            normal = (vkm_bvec3){ { 0, (int8_t)-step.y, 0 } };
        } else {
            if (t_max.z > max_distance) {
                break;
            }
            if (!nc__terrain_step_raycast_axis(&block_position.z, step.z)) {
                break;
            }
            distance = t_max.z;
            t_max.z += t_delta.z;
            normal = (vkm_bvec3){ { 0, 0, (int8_t)-step.z } };
        }
    }

    return false;
}

void nc_terrain_get_block_highlight_draw(
    const nc_terrain_t* terrain,
    const float time,
    const nc_camera_t* camera,
    nc_renderer_block_highlight_draw_t* draw
) {
    nc_terrain_raycast_hit_t hit;
    if (!nc_terrain_raycast(terrain, camera, NC_TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE, &hit)) {
        *draw = (nc_renderer_block_highlight_draw_t){
            .shown = false,
        };
        return;
    }

    *draw = (nc_renderer_block_highlight_draw_t){
        .position = { { (float)hit.block_position.x, (float)hit.block_position.y, (float)hit.block_position.z } },
        .normal = { { hit.normal.x, hit.normal.y, hit.normal.z } },
        .time = time,
        .shown = true,
    };
}

void nc_terrain_set_block(nc_terrain_t* terrain, const vkm_ivec3* block_coords, nc_block_type_t new_block) {
    if (new_block <= NC_BLOCK_TYPE_COUNT) {
        nc__terrain_set_world_block(terrain, block_coords, new_block);
    }
}

void nc_terrain_entity_set_block(nc_terrain_t* terrain, const nc_camera_t* camera, nc_block_type_t new_block) {
    nc_terrain_raycast_hit_t hit;
    if (!nc_terrain_raycast(terrain, camera, NC_TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE, &hit)) {
        return;
    }

    if (new_block == NC_BLOCK_TYPE_AIR) {
        nc__terrain_set_world_block(terrain, &hit.block_position, 0);
    } else if (new_block <= NC_BLOCK_TYPE_COUNT && !nc__terrain_normal_is_zero(hit.normal)) {
        vkm_ivec3 placement_position;
        if (nc_block_offset_coords(hit.block_position, hit.normal, &placement_position)) {
            nc__terrain_set_world_block(terrain, &placement_position, new_block);
        }
    }
}

int32_t nc_terrain_get_top_light_blocking_block(const nc_terrain_t* terrain, const vkm_ivec2 block_column_coords) {
    vkm_ivec2 chunk_column_coords;
    nc_block_column_to_chunk_column_coords(&block_column_coords, &chunk_column_coords);
    nc_chunk_column_t** column = nc__terrain_chunk_column_map_get(
            &terrain->chunk_columns,
            chunk_column_coords);
    if (!column) {
        return INT32_MIN;
    }

    vkm_ivec2 local_coords;
    nc_block_column_to_chunk_local_coords(&block_column_coords, &chunk_column_coords, &local_coords);
    const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(local_coords.x, local_coords.y);

    return (*column)->top_light_blocking_blocks[column_index];
}

void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    if (!terrain) {
        return;
    }

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        nc_chunk_fini(renderer, *it.value);
    }
    nc__terrain_chunk_map_fini(&terrain->chunks);

    nc__terrain_chunk_column_map_iter_t column_it = nc__terrain_chunk_column_map_iter(&terrain->chunk_columns);
    while (nc__terrain_chunk_column_map_next(&column_it)) {
        nc_chunk_column_fini(*column_it.value);
    }
    nc__terrain_chunk_column_map_fini(&terrain->chunk_columns);
    for (int i = 0; i < NC__TERRAIN_LIGHT_CHANNEL_COUNT; i++) {
        nc__terrain_light_removal_bfs_queue_fini(&terrain->light_removal_bfs_queues[i]);
        nc__terrain_light_bfs_queue_fini(&terrain->light_bfs_queues[i]);
    }

    nc_block_registry_fini(terrain->block_registry);
    nc_renderer_destroy_texture(renderer, terrain->texture_array);
    nc__terrain_stream_queue_fini(&terrain->load_queue);
    nc__terrain_stream_queue_fini(&terrain->unload_queue);
    nc__terrain_stream_queue_fini(&terrain->mesh_queue);

    free(terrain);
}
