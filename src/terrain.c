#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rapidhash.h>

#include <novacube/camera.h>
#include <novacube/cvkm.h>
#include <novacube/macros.h>
#include <novacube/mesher.h>
#include <novacube/standard_functions.h>
#include <novacube/terrain.h>

#ifdef ANDROID
#define NC__TERRAIN_ASSETS_BASE_PATH ""
#define NC__TERRAIN_TEXTURE_EXTENSION ".astc"
#else
#define NC__TERRAIN_ASSETS_BASE_PATH "assets/"
#define NC__TERRAIN_TEXTURE_EXTENSION ".png"
#endif

#define NC__TERRAIN_MIN_CHUNK_COORD (INT32_MIN / NC_MESHER_CHUNK_SIZE)
#define NC__TERRAIN_MAX_CHUNK_COORD (INT32_MAX / NC_MESHER_CHUNK_SIZE)
#define NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z) ((x) + (z) * NC_MESHER_CHUNK_SIZE)
#define NC__TERRAIN_BLOCK_LIGHT_MASK 0x0f
#define NC__TERRAIN_SKY_LIGHT_SHIFT 4

#define TDS_TYPE light_node_queue
#define TDS_BIT_COUNT NC_MESHER_BLOCKS_PER_CHUNK
#include <tds/bitset.h>

typedef uint8_t nc__terrain_chunk_flags_t;
enum {
    NC__TERRAIN_CHUNK_MESH_DIRTY_BIT = 1 << 0,
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

typedef struct nc__terrain_chunk_t {
    vkm_ivec3 coords;
    uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK];
    // Low nibble: block light. High nibble: sky light.
    uint8_t light_levels[NC_MESHER_BLOCKS_PER_CHUNK];
    // Lazily allocated queue-membership bitsets. Relighting releases them after both propagation passes drain.
    light_node_queue* queued_light_nodes[NC__TERRAIN_LIGHT_CHANNEL_COUNT];
    // SSBO containing an array of nc_mesh_quad_t.
    nc_renderer_buffer_t* quad_buffer;
    // SSBO containing an array of nc_mesh_face_data_t.
    nc_renderer_buffer_t* face_data_buffer;
    uint32_t quad_count;
    nc__terrain_chunk_flags_t flags;
} nc__terrain_chunk_t;

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
    nc__terrain_chunk_t* chunk;
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
#define TDS_VALUE_T nc__terrain_chunk_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y && (a).z == (b).z)
#include <tds/hashmap.h>

#define TDS_TYPE nc__terrain_chunk_column_dirty_bitset
#define TDS_BIT_COUNT (NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE)
#define TDS_WORD_T uint64_t
#include <tds/bitset.h>

typedef struct nc__terrain_chunk_column_t {
    int32_t top_light_blocking_blocks[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE];
    nc__terrain_chunk_column_dirty_bitset dirty_top_light_blocking_blocks;
    int32_t min_loaded_chunk_y;
    int32_t max_loaded_chunk_y;
    uint32_t ref_count;
} nc__terrain_chunk_column_t;

#define TDS_TYPE nc__terrain_chunk_column_map
#define TDS_KEY_T vkm_ivec2
#define TDS_VALUE_T nc__terrain_chunk_column_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_xz_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y)
#include <tds/hashmap.h>

#define TDS_TYPE nc__terrain_light_bfs_queue
#define TDS_VALUE_T nc__terrain_light_node_t
#include <tds/queue.h>

#define TDS_TYPE nc__terrain_light_removal_bfs_queue
#define TDS_VALUE_T nc__terrain_light_removal_node_t
#include <tds/queue.h>

typedef struct nc_terrain_t {
    nc__terrain_chunk_map chunks;
    nc__terrain_chunk_column_map chunk_columns;
    nc_renderer_texture_t* texture_array;
    nc_block_registry_t* block_registry;
    nc_mesher_t* mesher;
    nc__terrain_light_bfs_queue light_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_COUNT];
    nc__terrain_light_removal_bfs_queue light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_COUNT];
    bool sky_light_has_propagated;
    bool sky_light_needs_rebuild;
    bool light_queue_bitsets_allocated;
} nc_terrain_t;

static const vkm_bvec3 nc__terrain_light_neighbor_offsets[] = {
    { { -1,  0,  0 } },
    { {  1,  0,  0 } },
    { {  0, -1,  0 } },
    { {  0,  1,  0 } },
    { {  0,  0, -1 } },
    { {  0,  0,  1 } },
};

static uint8_t nc__terrain_get_light(const uint8_t packed_light, const nc__terrain_light_channel_t channel) {
    return packed_light >> channel * NC__TERRAIN_SKY_LIGHT_SHIFT & NC__TERRAIN_BLOCK_LIGHT_MASK;
}

static void nc__terrain_set_light(
    uint8_t* packed_light,
    const nc__terrain_light_channel_t channel,
    const uint8_t light
) {
    NC_ASSERT(light <= 15);
    const int shift = channel * NC__TERRAIN_SKY_LIGHT_SHIFT;
    const uint8_t mask = (uint8_t)(NC__TERRAIN_BLOCK_LIGHT_MASK << shift);
    *packed_light = (uint8_t)((*packed_light & ~mask) | light << shift);
}

static uint8_t nc__terrain_get_block_light(const uint8_t packed_light) {
    return nc__terrain_get_light(packed_light, NC__TERRAIN_LIGHT_CHANNEL_BLOCK);
}

static uint8_t nc__terrain_get_sky_light(const uint8_t packed_light) {
    return nc__terrain_get_light(packed_light, NC__TERRAIN_LIGHT_CHANNEL_SKY);
}

static void nc__terrain_set_block_light(uint8_t* packed_light, const uint8_t light) {
    nc__terrain_set_light(packed_light, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, light);
}

static void nc__terrain_set_sky_light(uint8_t* packed_light, const uint8_t light) {
    nc__terrain_set_light(packed_light, NC__TERRAIN_LIGHT_CHANNEL_SKY, light);
}

static bool nc__terrain_block_has_direct_sky(
    const nc__terrain_chunk_column_t* column,
    const int column_index,
    const int32_t world_y,
    const nc_block_t* block
) {
    return !(block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
            && world_y > column->top_light_blocking_blocks[column_index];
}

static vkm_ivec2 nc__terrain_chunk_column_coords(const vkm_ivec3* chunk_coords) {
    return (vkm_ivec2){ { chunk_coords->x, chunk_coords->z } };
}

static int32_t nc__terrain_chunk_local_to_block_coord(const int32_t chunk_coord, const int32_t local_coord) {
    NC_ASSERT(local_coord >= 0 && local_coord < NC_MESHER_CHUNK_SIZE);
    return chunk_coord * NC_MESHER_CHUNK_SIZE + local_coord;
}

static void nc__terrain_update_chunk_column_from_chunk(
    const nc_terrain_t* terrain,
    nc__terrain_chunk_column_t* column,
    const nc__terrain_chunk_t* chunk
) {
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
            for (int y = NC_MESHER_CHUNK_SIZE - 1; y >= 0; y--) {
                const uint16_t block = chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z)];
                if (nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block)->flags
                        & NC_BLOCK_FLAG_BLOCKS_LIGHT) {
                    const int32_t world_y = nc__terrain_chunk_local_to_block_coord(chunk->coords.y, y);
                    if (world_y > column->top_light_blocking_blocks[column_index]) {
                        column->top_light_blocking_blocks[column_index] = world_y;
                    }
                    break;
                }
            }
        }
    }
}

// Returns NULL if the chunk isn't loaded.
static nc__terrain_chunk_t* nc__terrain_get_chunk(const nc_terrain_t* terrain, const vkm_ivec3* coords) {
    nc__terrain_chunk_t** chunk = nc__terrain_chunk_map_get(&terrain->chunks, *coords);
    return chunk ? *chunk : NULL;
}

static int32_t nc__terrain_floor_divide_by_chunk_size(const int32_t value) {
    int32_t result = value / NC_MESHER_CHUNK_SIZE;
    if (value < 0 && value % NC_MESHER_CHUNK_SIZE != 0) {
        result--;
    }
    return result;
}

static int32_t nc__terrain_block_to_chunk_local_coord(const int32_t block_coord, const int32_t chunk_coord) {
    const int32_t result = (int32_t)((int64_t)block_coord - (int64_t)chunk_coord * NC_MESHER_CHUNK_SIZE);
    NC_ASSERT(result >= 0 && result < NC_MESHER_CHUNK_SIZE);
    return result;
}

static void nc__terrain_update_dirty_chunk_column(
    const nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    nc__terrain_chunk_column_t* column
) {
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
            if (!nc__terrain_chunk_column_dirty_bitset_get(
                    &column->dirty_top_light_blocking_blocks,
                    column_index)) {
                continue;
            }

            const int32_t top_chunk_y = nc__terrain_floor_divide_by_chunk_size(
                    column->top_light_blocking_blocks[column_index]);
            column->top_light_blocking_blocks[column_index] = INT32_MIN;
            for (int32_t chunk_y = top_chunk_y; chunk_y >= column->min_loaded_chunk_y; chunk_y--) {
                const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
                const nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
                if (!chunk) {
                    continue;
                }

                for (int y = NC_MESHER_CHUNK_SIZE - 1; y >= 0; y--) {
                    const uint16_t block = chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z)];
                    if (nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block)->flags
                            & NC_BLOCK_FLAG_BLOCKS_LIGHT) {
                        column->top_light_blocking_blocks[column_index] =
                                nc__terrain_chunk_local_to_block_coord(chunk_y, y);
                        goto found_top_light_blocking_block;
                    }
                }
            }

found_top_light_blocking_block:
            nc__terrain_chunk_column_dirty_bitset_clear(
                    &column->dirty_top_light_blocking_blocks,
                    column_index);
        }
    }
}

static const char* nc__terrain_texture_paths[] = {
    NC__TERRAIN_ASSETS_BASE_PATH "textures/stone" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/dirt" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/grass" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/torch" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/torch-top" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/testbox" NC__TERRAIN_TEXTURE_EXTENSION,
};

// Make sure the chunk doesn't contain blocks whose coords overflow int32_t.
static bool nc__terrain_chunk_coords_are_valid(const vkm_ivec3* coords) {
    return coords->x >= NC__TERRAIN_MIN_CHUNK_COORD && coords->x <= NC__TERRAIN_MAX_CHUNK_COORD
        && coords->y >= NC__TERRAIN_MIN_CHUNK_COORD && coords->y <= NC__TERRAIN_MAX_CHUNK_COORD
        && coords->z >= NC__TERRAIN_MIN_CHUNK_COORD && coords->z <= NC__TERRAIN_MAX_CHUNK_COORD;
}

// Offsets the chunk coords and returns false if the new coords would fall outside the valid range.
static bool nc__terrain_offset_chunk_coords(
    const vkm_ivec3* coords,
    const int offset_x,
    const int offset_y,
    const int offset_z,
    vkm_ivec3* result
) {
    const int64_t x = (int64_t)coords->x + offset_x;
    const int64_t y = (int64_t)coords->y + offset_y;
    const int64_t z = (int64_t)coords->z + offset_z;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || z < INT32_MIN || z > INT32_MAX) {
        return false;
    }

    *result = (vkm_ivec3){ { (int32_t)x, (int32_t)y, (int32_t)z } };
    return nc__terrain_chunk_coords_are_valid(result);
}

// Marks the chunk and every one of its 26 neighbors as dirty.
// TODO: For every caller of this function, check if we can instead only mark the chunks that could have changed.
static void nc__terrain_mark_chunk_and_neighbors_dirty(const nc_terrain_t* terrain, const vkm_ivec3* coords) {
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vkm_ivec3 neighbor_coords;
                if (!nc__terrain_offset_chunk_coords(coords, x, y, z, &neighbor_coords)) {
                    continue;
                }

                nc__terrain_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
                if (neighbor) {
                    neighbor->flags |= NC__TERRAIN_CHUNK_MESH_DIRTY_BIT;
                }
            }
        }
    }
}

// A block edit can only affect chunks whose one-block meshing border contains the edited block.
static void nc__terrain_mark_block_chunks_dirty(
    const nc_terrain_t* terrain,
    const vkm_ivec3* chunk_coords,
    const vkm_ivec3* local_coords
) {
    const int min_x = local_coords->x == 0 ? -1 : 0;
    const int max_x = local_coords->x == NC_MESHER_CHUNK_SIZE - 1 ? 1 : 0;
    const int min_y = local_coords->y == 0 ? -1 : 0;
    const int max_y = local_coords->y == NC_MESHER_CHUNK_SIZE - 1 ? 1 : 0;
    const int min_z = local_coords->z == 0 ? -1 : 0;
    const int max_z = local_coords->z == NC_MESHER_CHUNK_SIZE - 1 ? 1 : 0;

    for (int z = min_z; z <= max_z; z++) {
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                vkm_ivec3 affected_coords;
                if (!nc__terrain_offset_chunk_coords(chunk_coords, x, y, z, &affected_coords)) {
                    continue;
                }

                nc__terrain_chunk_t* affected = nc__terrain_get_chunk(terrain, &affected_coords);
                if (affected) {
                    affected->flags |= NC__TERRAIN_CHUNK_MESH_DIRTY_BIT;
                }
            }
        }
    }
}

static void nc__terrain_block_to_chunk_coords(const vkm_ivec3* block_coords, vkm_ivec3* result) {
    *result = (vkm_ivec3){ {
        nc__terrain_floor_divide_by_chunk_size(block_coords->x),
        nc__terrain_floor_divide_by_chunk_size(block_coords->y),
        nc__terrain_floor_divide_by_chunk_size(block_coords->z),
    } };
}

static void nc__terrain_block_column_to_chunk_column_coords(const vkm_ivec2* block_coords, vkm_ivec2* result) {
    *result = (vkm_ivec2){ {
        nc__terrain_floor_divide_by_chunk_size(block_coords->x),
        nc__terrain_floor_divide_by_chunk_size(block_coords->y),
    } };
}

static void nc__terrain_block_to_chunk_local_coords(
    const vkm_ivec3* block_coords,
    const vkm_ivec3* chunk_coords,
    vkm_ivec3* result
) {
    *result = (vkm_ivec3){ {
        nc__terrain_block_to_chunk_local_coord(block_coords->x, chunk_coords->x),
        nc__terrain_block_to_chunk_local_coord(block_coords->y, chunk_coords->y),
        nc__terrain_block_to_chunk_local_coord(block_coords->z, chunk_coords->z),
    } };
}

static void nc__terrain_block_column_to_chunk_local_coords(
    const vkm_ivec2* block_coords,
    const vkm_ivec2* chunk_coords,
    vkm_ivec2* result
) {
    *result = (vkm_ivec2){ {
        nc__terrain_block_to_chunk_local_coord(block_coords->x, chunk_coords->x),
        nc__terrain_block_to_chunk_local_coord(block_coords->y, chunk_coords->y),
    } };
}

static void nc__terrain_chunk_index_to_local_coords(const uint16_t index, vkm_ivec3* result) {
    NC_ASSERT(index < NC_MESHER_BLOCKS_PER_CHUNK);

    NC_MESHER_CHUNK_INDEX_TO_COORDS(index, result->x, result->y, result->z);
}

static void nc__terrain_chunk_local_to_block_coords(
    const vkm_ivec3* chunk_coords,
    const vkm_ivec3* local_coords,
    vkm_ivec3* result
) {
    NC_ASSERT(local_coords->x >= 0 && local_coords->x < NC_MESHER_CHUNK_SIZE);
    NC_ASSERT(local_coords->y >= 0 && local_coords->y < NC_MESHER_CHUNK_SIZE);
    NC_ASSERT(local_coords->z >= 0 && local_coords->z < NC_MESHER_CHUNK_SIZE);

    *result = (vkm_ivec3){ {
        nc__terrain_chunk_local_to_block_coord(chunk_coords->x, local_coords->x),
        nc__terrain_chunk_local_to_block_coord(chunk_coords->y, local_coords->y),
        nc__terrain_chunk_local_to_block_coord(chunk_coords->z, local_coords->z),
    } };
}

static bool nc__terrain_get_light_neighbor(
    const nc_terrain_t* terrain,
    nc__terrain_chunk_t* chunk,
    const uint16_t index,
    const vkm_ivec3* local_coords,
    const vkm_bvec3 offset,
    nc__terrain_block_location_t* result
) {
    vkm_ivec3 neighbor_local_coords = { {
        local_coords->x + offset.x,
        local_coords->y + offset.y,
        local_coords->z + offset.z,
    } };

    // Almost every neighbor remains in the current chunk. Avoid a hashmap lookup in that common case.
    if (neighbor_local_coords.x >= 0 && neighbor_local_coords.x < NC_MESHER_CHUNK_SIZE
            && neighbor_local_coords.y >= 0 && neighbor_local_coords.y < NC_MESHER_CHUNK_SIZE
            && neighbor_local_coords.z >= 0 && neighbor_local_coords.z < NC_MESHER_CHUNK_SIZE) {
        *result = (nc__terrain_block_location_t){
            .chunk = chunk,
            .index = (uint16_t)(index
                    + offset.x
                    + offset.y * NC_MESHER_CHUNK_SIZE
                    + offset.z * NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE),
        };
        return true;
    }

    // Crossing a chunk boundary wraps the local coordinate and offsets the owning chunk by one.
    const int chunk_offset_x = neighbor_local_coords.x < 0
            ? -1
            : neighbor_local_coords.x >= NC_MESHER_CHUNK_SIZE ? 1 : 0;
    const int chunk_offset_y = neighbor_local_coords.y < 0
            ? -1
            : neighbor_local_coords.y >= NC_MESHER_CHUNK_SIZE ? 1 : 0;
    const int chunk_offset_z = neighbor_local_coords.z < 0
            ? -1
            : neighbor_local_coords.z >= NC_MESHER_CHUNK_SIZE ? 1 : 0;
    vkm_ivec3 neighbor_chunk_coords;
    if (!nc__terrain_offset_chunk_coords(
            &chunk->coords,
            chunk_offset_x,
            chunk_offset_y,
            chunk_offset_z,
            &neighbor_chunk_coords)) {
        return false;
    }

    nc__terrain_chunk_t* neighbor_chunk = nc__terrain_get_chunk(terrain, &neighbor_chunk_coords);
    if (!neighbor_chunk) {
        return false;
    }

    if (neighbor_local_coords.x < 0) {
        neighbor_local_coords.x = NC_MESHER_CHUNK_SIZE - 1;
    } else if (neighbor_local_coords.x >= NC_MESHER_CHUNK_SIZE) {
        neighbor_local_coords.x = 0;
    }
    if (neighbor_local_coords.y < 0) {
        neighbor_local_coords.y = NC_MESHER_CHUNK_SIZE - 1;
    } else if (neighbor_local_coords.y >= NC_MESHER_CHUNK_SIZE) {
        neighbor_local_coords.y = 0;
    }
    if (neighbor_local_coords.z < 0) {
        neighbor_local_coords.z = NC_MESHER_CHUNK_SIZE - 1;
    } else if (neighbor_local_coords.z >= NC_MESHER_CHUNK_SIZE) {
        neighbor_local_coords.z = 0;
    }

    *result = (nc__terrain_block_location_t){
        .chunk = neighbor_chunk,
        .index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(
                neighbor_local_coords.x,
                neighbor_local_coords.y,
                neighbor_local_coords.z),
    };
    return true;
}

static void nc__terrain_queue_light_node(
    nc_terrain_t* terrain,
    const nc__terrain_light_channel_t channel,
    const nc__terrain_block_location_t* location
) {
    light_node_queue** queued_nodes = &location->chunk->queued_light_nodes[channel];
    if (!*queued_nodes) {
        *queued_nodes = calloc(1, sizeof(**queued_nodes));
        terrain->light_queue_bitsets_allocated = true;
    }

    if (light_node_queue_get(*queued_nodes, location->index)) {
        return;
    }
    light_node_queue_set(*queued_nodes, location->index);

    nc__terrain_light_bfs_queue_push(&terrain->light_bfs_queues[channel], (nc__terrain_light_node_t){
        .chunk_coords = location->chunk->coords,
        .index = location->index,
    });
}

static void nc__terrain_queue_light_neighbors(
    nc_terrain_t* terrain,
    const nc__terrain_light_channel_t channel,
    nc__terrain_chunk_t* chunk,
    const uint16_t index,
    const vkm_ivec3* local_coords
) {
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        nc__terrain_block_location_t neighbor;
        if (nc__terrain_get_light_neighbor(
                terrain,
                chunk,
                index,
                local_coords,
                nc__terrain_light_neighbor_offsets[i],
                &neighbor)
                && nc__terrain_get_light(neighbor.chunk->light_levels[neighbor.index], channel) > 1) {
            nc__terrain_queue_light_node(terrain, channel, &neighbor);
        }
    }
}

static void nc__terrain_queue_chunk_boundary_light_removal(nc_terrain_t* terrain, const nc__terrain_chunk_t* chunk) {
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                if (x != 0 && x != NC_MESHER_CHUNK_SIZE - 1
                        && y != 0 && y != NC_MESHER_CHUNK_SIZE - 1
                        && z != 0 && z != NC_MESHER_CHUNK_SIZE - 1) {
                    continue;
                }

                const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z);
                const uint8_t block_light = nc__terrain_get_block_light(chunk->light_levels[index]);
                if (block_light) {
                    nc__terrain_light_removal_bfs_queue_push(
                            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_BLOCK],
                            (nc__terrain_light_removal_node_t){
                                .chunk_coords = chunk->coords,
                                .index = index,
                                .light_level = block_light,
                            });
                }
                const uint8_t sky_light = nc__terrain_get_sky_light(chunk->light_levels[index]);
                if (sky_light) {
                    nc__terrain_light_removal_bfs_queue_push(
                            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY],
                            (nc__terrain_light_removal_node_t){
                                .chunk_coords = chunk->coords,
                                .index = index,
                                .light_level = sky_light,
                            });
                }
            }
        }
    }
}

static void nc__terrain_seed_chunk_light(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    for (uint16_t index = 0; index < NC_MESHER_BLOCKS_PER_CHUNK; index++) {
        const nc_block_t* block = nc_block_registry_get(
                terrain->block_registry,
                (nc_block_type_t)chunk->blocks[index]);
        if (block->light_emission) {
            nc__terrain_set_block_light(&chunk->light_levels[index], block->light_emission);
            const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
            nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &location);
        }
    }

    // Existing light in adjacent chunks only needs to be requeued along the newly loaded boundary. Fetch each
    // neighboring chunk once rather than performing a hashmap lookup for every block on every face.
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
        vkm_ivec3 neighbor_coords;
        if (!nc__terrain_offset_chunk_coords(
                &chunk->coords, offset.x, offset.y, offset.z, &neighbor_coords)) {
            continue;
        }
        nc__terrain_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
        if (!neighbor) {
            continue;
        }

        for (int b = 0; b < NC_MESHER_CHUNK_SIZE; b++) {
            for (int a = 0; a < NC_MESHER_CHUNK_SIZE; a++) {
                const int x = offset.x < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.x > 0 ? 0 : a;
                const int y = offset.y < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.y > 0 ? 0 : offset.x ? a : b;
                const int z = offset.z < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.z > 0 ? 0 : b;
                const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z);
                if (nc__terrain_get_block_light(neighbor->light_levels[index]) > 1) {
                    const nc__terrain_block_location_t location = { .chunk = neighbor, .index = index };
                    nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &location);
                }
            }
        }
    }
}

static void nc__terrain_queue_sky_light_edge(
    nc_terrain_t* terrain,
    const nc__terrain_block_location_t* source,
    const nc__terrain_block_location_t* target,
    const int target_offset_y
) {
    const uint8_t source_light = nc__terrain_get_sky_light(source->chunk->light_levels[source->index]);
    if (source_light <= 1) {
        return;
    }
    const uint8_t propagated = source_light == 15 && target_offset_y < 0
            ? 15
            : (uint8_t)(source_light - 1);
    if (nc__terrain_get_sky_light(target->chunk->light_levels[target->index]) >= propagated) {
        return;
    }
    const nc_block_t* target_block = nc_block_registry_get(
            terrain->block_registry,
            (nc_block_type_t)target->chunk->blocks[target->index]);
    if (!(target_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)) {
        nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, source);
    }
}

static void nc__terrain_queue_sky_light_edge_both_ways(
    nc_terrain_t* terrain,
    const nc__terrain_block_location_t* a,
    const nc__terrain_block_location_t* b,
    const int b_offset_y
) {
    nc__terrain_queue_sky_light_edge(terrain, a, b, b_offset_y);
    nc__terrain_queue_sky_light_edge(terrain, b, a, -b_offset_y);
}

static void nc__terrain_queue_chunk_sky_light_frontier(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(&chunk->coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    NC_ASSERT(column);
    const int64_t chunk_min_y = (int64_t)chunk->coords.y * NC_MESHER_CHUNK_SIZE;
    const int64_t chunk_max_y = chunk_min_y + NC_MESHER_CHUNK_SIZE - 1;

    // Within a chunk, direct sky can enter indirect space only across a horizontal heightmap discontinuity. Check only
    // the vertical interval between the two top blockers instead of inspecting all six neighbors of every voxel.
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
            const int32_t top = (*column)->top_light_blocking_blocks[column_index];
            for (int direction = 0; direction < 2; direction++) {
                const int neighbor_x = x + (direction == 0);
                const int neighbor_z = z + (direction == 1);
                if (neighbor_x >= NC_MESHER_CHUNK_SIZE || neighbor_z >= NC_MESHER_CHUNK_SIZE) {
                    continue;
                }
                const int neighbor_column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(neighbor_x, neighbor_z);
                const int32_t neighbor_top = (*column)->top_light_blocking_blocks[neighbor_column_index];
                if (top == neighbor_top) {
                    continue;
                }

                const int32_t lower_top = top < neighbor_top ? top : neighbor_top;
                const int32_t upper_top = top > neighbor_top ? top : neighbor_top;
                const int64_t first_world_y = chunk_min_y > (int64_t)lower_top + 1
                        ? chunk_min_y
                        : (int64_t)lower_top + 1;
                const int64_t last_world_y = chunk_max_y < upper_top ? chunk_max_y : upper_top;
                for (int64_t world_y = first_world_y; world_y <= last_world_y; world_y++) {
                    const int y = (int)(world_y - chunk_min_y);
                    const nc__terrain_block_location_t location = {
                        .chunk = chunk,
                        .index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z),
                    };
                    const nc__terrain_block_location_t neighbor = {
                        .chunk = chunk,
                        .index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(neighbor_x, y, neighbor_z),
                    };
                    nc__terrain_queue_sky_light_edge_both_ways(terrain, &location, &neighbor, 0);
                }
            }
        }
    }

    // Chunk loading creates new light paths only across the six touching faces. Fetch each neighbor once and compare
    // the two faces directly; both directions are checked so either chunk can refill the other.
    for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
        const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
        vkm_ivec3 neighbor_coords;
        if (!nc__terrain_offset_chunk_coords(
                &chunk->coords, offset.x, offset.y, offset.z, &neighbor_coords)) {
            continue;
        }
        nc__terrain_chunk_t* neighbor_chunk = nc__terrain_get_chunk(terrain, &neighbor_coords);
        if (!neighbor_chunk) {
            continue;
        }

        for (int b = 0; b < NC_MESHER_CHUNK_SIZE; b++) {
            for (int a = 0; a < NC_MESHER_CHUNK_SIZE; a++) {
                const int x = offset.x < 0 ? 0 : offset.x > 0 ? NC_MESHER_CHUNK_SIZE - 1 : a;
                const int y = offset.y < 0 ? 0 : offset.y > 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.x ? a : b;
                const int z = offset.z < 0 ? 0 : offset.z > 0 ? NC_MESHER_CHUNK_SIZE - 1 : b;
                const int neighbor_x = offset.x < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.x > 0 ? 0 : x;
                const int neighbor_y = offset.y < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.y > 0 ? 0 : y;
                const int neighbor_z = offset.z < 0 ? NC_MESHER_CHUNK_SIZE - 1 : offset.z > 0 ? 0 : z;
                const nc__terrain_block_location_t location = {
                    .chunk = chunk,
                    .index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z),
                };
                const nc__terrain_block_location_t neighbor = {
                    .chunk = neighbor_chunk,
                    .index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(neighbor_x, neighbor_y, neighbor_z),
                };
                nc__terrain_queue_sky_light_edge_both_ways(terrain, &location, &neighbor, offset.y);
            }
        }
    }
}

static void nc__terrain_seed_chunk_sky_light(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(&chunk->coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
    NC_ASSERT(column);

    // The heightmap already guarantees that every block above its top blocker is transparent. Assign only that
    // direct-sky interval without repeating a block-registry lookup for every voxel or queueing every voxel. A later
    // frontier scan queues only sources that can actually improve a darker neighbor.
    const int64_t chunk_min_y = (int64_t)chunk->coords.y * NC_MESHER_CHUNK_SIZE;
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
            int64_t first_y = (int64_t)(*column)->top_light_blocking_blocks[column_index] + 1 - chunk_min_y;
            if (first_y < 0) {
                first_y = 0;
            }
            if (first_y >= NC_MESHER_CHUNK_SIZE) {
                continue;
            }

            for (int y = (int)first_y; y < NC_MESHER_CHUNK_SIZE; y++) {
                const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z);
                nc__terrain_set_sky_light(&chunk->light_levels[index], 15);
            }
        }
    }
}

static void nc__terrain_reconcile_sky_light_cell(
    nc_terrain_t* terrain,
    const nc__terrain_chunk_column_t* column,
    nc__terrain_chunk_t* chunk,
    const vkm_ivec3 local_coords,
    const nc__terrain_sky_light_reconcile_mode_t mode
) {
    const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(
            local_coords.x, local_coords.y, local_coords.z);
    const nc_block_t* block = nc_block_registry_get(
            terrain->block_registry,
            (nc_block_type_t)chunk->blocks[index]);
    const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(local_coords.x, local_coords.z);
    const int32_t world_y = nc__terrain_chunk_local_to_block_coord(chunk->coords.y, local_coords.y);
    const uint8_t old_light = nc__terrain_get_sky_light(chunk->light_levels[index]);
    const bool direct_sky = nc__terrain_block_has_direct_sky(column, column_index, world_y, block);

    if (direct_sky) {
        if (old_light == 15) {
            return;
        }
        nc__terrain_set_sky_light(&chunk->light_levels[index], 15);
        if (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL) {
            const nc__terrain_block_location_t location = { .chunk = chunk, .index = index };
            nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, &location);
        }
    } else {
        if (!old_light || (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_SEEDED && old_light != 15)) {
            return;
        }
        nc__terrain_set_sky_light(&chunk->light_levels[index], 0);
        if (mode == NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL || terrain->sky_light_has_propagated) {
            nc__terrain_light_removal_bfs_queue_push(
                    &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY],
                    (nc__terrain_light_removal_node_t){
                        .chunk_coords = chunk->coords,
                        .index = index,
                        .light_level = old_light,
                    });
        }
    }
    nc__terrain_mark_block_chunks_dirty(terrain, &chunk->coords, &local_coords);
}

static void nc__terrain_reconcile_chunk_column_sky_light(
    nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    const nc__terrain_chunk_column_t* column,
    const int32_t old_top_light_blocking_blocks[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE]
) {
    nc__terrain_sky_light_chunk_range_t ranges[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE];
    int32_t first_affected_chunk_y = INT32_MAX;
    int32_t last_affected_chunk_y = INT32_MIN;

    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
            nc__terrain_sky_light_chunk_range_t* range = &ranges[column_index];
            const int32_t old_top = old_top_light_blocking_blocks[column_index];
            const int32_t new_top = column->top_light_blocking_blocks[column_index];
            if (old_top == new_top) {
                range->first_chunk_y = 1;
                range->last_chunk_y = 0;
                continue;
            }

            // Only blocks above the lower height and at or below the higher height changed direct-sky status.
            range->lower_top = old_top < new_top ? old_top : new_top;
            range->upper_top = old_top > new_top ? old_top : new_top;
            range->first_chunk_y = nc__terrain_floor_divide_by_chunk_size(range->lower_top + 1);
            range->last_chunk_y = nc__terrain_floor_divide_by_chunk_size(range->upper_top);
            if (range->first_chunk_y < column->min_loaded_chunk_y) {
                range->first_chunk_y = column->min_loaded_chunk_y;
            }
            if (range->last_chunk_y > column->max_loaded_chunk_y) {
                range->last_chunk_y = column->max_loaded_chunk_y;
            }
            if (range->first_chunk_y > range->last_chunk_y) {
                continue;
            }
            if (range->first_chunk_y < first_affected_chunk_y) {
                first_affected_chunk_y = range->first_chunk_y;
            }
            if (range->last_chunk_y > last_affected_chunk_y) {
                last_affected_chunk_y = range->last_chunk_y;
            }
        }
    }

    for (int32_t chunk_y = first_affected_chunk_y; chunk_y <= last_affected_chunk_y; chunk_y++) {
        const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
        if (!chunk) {
            continue;
        }

        const int64_t chunk_min_y = (int64_t)chunk_y * NC_MESHER_CHUNK_SIZE;
        const int64_t chunk_max_y = chunk_min_y + NC_MESHER_CHUNK_SIZE - 1;
        for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(x, z);
                const nc__terrain_sky_light_chunk_range_t* range = &ranges[column_index];
                if (chunk_y < range->first_chunk_y || chunk_y > range->last_chunk_y) {
                    continue;
                }

                const int64_t first_y = chunk_min_y > (int64_t)range->lower_top + 1
                        ? chunk_min_y
                        : (int64_t)range->lower_top + 1;
                const int64_t last_y = chunk_max_y < range->upper_top ? chunk_max_y : range->upper_top;
                NC_ASSERT(first_y >= chunk_min_y && first_y <= last_y && last_y <= chunk_max_y);
                for (int64_t world_y = first_y; world_y <= last_y; world_y++) {
                    const int y = (int)(world_y - chunk_min_y);
                    nc__terrain_reconcile_sky_light_cell(
                            terrain,
                            column,
                            chunk,
                            (vkm_ivec3){ { x, y, z } },
                            NC__TERRAIN_SKY_LIGHT_RECONCILE_SEEDED);
                }
            }
        }
    }
}

static void nc__terrain_reconcile_block_column_sky_light(
    nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    const nc__terrain_chunk_column_t* column,
    const int x,
    const int z
) {
    for (int32_t chunk_y = column->min_loaded_chunk_y; chunk_y <= column->max_loaded_chunk_y; chunk_y++) {
        const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
        if (!chunk) {
            continue;
        }
        for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
            nc__terrain_reconcile_sky_light_cell(
                    terrain,
                    column,
                    chunk,
                    (vkm_ivec3){ { x, y, z } },
                    NC__TERRAIN_SKY_LIGHT_RECONCILE_INCREMENTAL);
        }
    }
}

static bool nc__terrain_offset_block_coords(const vkm_ivec3 coords, const vkm_bvec3 offset, vkm_ivec3* result) {
    const int64_t x = (int64_t)coords.x + offset.x;
    const int64_t y = (int64_t)coords.y + offset.y;
    const int64_t z = (int64_t)coords.z + offset.z;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || z < INT32_MIN || z > INT32_MAX) {
        return false;
    }

    *result = (vkm_ivec3){ { (int32_t)x, (int32_t)y, (int32_t)z } };
    return true;
}

static bool nc__terrain_normal_is_zero(const vkm_bvec3 normal) {
    return normal.x == 0 && normal.y == 0 && normal.z == 0;
}

bool nc_terrain_get_block(const nc_terrain_t* terrain, const vkm_ivec3* block_coords, uint16_t* block) {
    vkm_ivec3 chunk_coords;
    nc__terrain_block_to_chunk_coords(block_coords, &chunk_coords);
    const nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    vkm_ivec3 local_coords;
    nc__terrain_block_to_chunk_local_coords(block_coords, &chunk_coords, &local_coords);

    if (block) {
        *block = chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z)];
    }
    return true;
}

static bool nc__terrain_set_world_block(nc_terrain_t* terrain, const vkm_ivec3* block_coords, const uint16_t block) {
    vkm_ivec3 chunk_coords;
    nc__terrain_block_to_chunk_coords(block_coords, &chunk_coords);
    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    vkm_ivec3 local_coords;
    nc__terrain_block_to_chunk_local_coords(block_coords, &chunk_coords, &local_coords);

    const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z);
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

        nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                &terrain->chunk_columns,
                nc__terrain_chunk_column_coords(&chunk_coords));
        NC_ASSERT(column);

        const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(local_coords.x, local_coords.z);
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
                    nc__terrain_chunk_column_coords(&chunk_coords),
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
                    nc__terrain_chunk_column_coords(&chunk_coords),
                    *column,
                    local_coords.x,
                    local_coords.z);
        }

        nc__terrain_mark_block_chunks_dirty(terrain, &chunk_coords, &local_coords);
    }

    return true;
}

static void nc__terrain_chunk_fini(nc_renderer_t* renderer, nc__terrain_chunk_t* chunk) {
    if (!chunk) {
        return;
    }

    nc_renderer_destroy_buffer(renderer, chunk->face_data_buffer);
    nc_renderer_destroy_buffer(renderer, chunk->quad_buffer);
    for (int i = 0; i < NC__TERRAIN_LIGHT_CHANNEL_COUNT; i++) {
        free(chunk->queued_light_nodes[i]);
    }
    free(chunk);
}

// The blocks pointer is optional. If NULL, then the chunk will be filled with air.
void nc_terrain_load_or_replace_chunk(
    nc_terrain_t* terrain,
    const vkm_ivec3* coords,
    const uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK]
) {
    NC_ASSERT(nc__terrain_chunk_coords_are_valid(coords));

    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (chunk) {
        int32_t old_top_light_blocking_blocks[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE];
        nc__terrain_queue_chunk_boundary_light_removal(terrain, chunk);
        memset(chunk->light_levels, 0, sizeof(chunk->light_levels));
        if (blocks) {
            memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
        } else {
            memset(chunk->blocks, 0, sizeof(chunk->blocks));
        }
        nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                &terrain->chunk_columns,
                nc__terrain_chunk_column_coords(coords));
        NC_ASSERT(column);
        memcpy(old_top_light_blocking_blocks,
                (*column)->top_light_blocking_blocks,
                sizeof(old_top_light_blocking_blocks));
        nc__terrain_chunk_column_dirty_bitset_set_all(&(*column)->dirty_top_light_blocking_blocks);
        nc__terrain_update_dirty_chunk_column(terrain, nc__terrain_chunk_column_coords(coords), *column);
        nc__terrain_reconcile_chunk_column_sky_light(
                terrain,
                nc__terrain_chunk_column_coords(coords),
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

    chunk = calloc(1, sizeof(*chunk));
    chunk->coords = *coords;
    if (blocks) {
        memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
    } else {
        memset(chunk->blocks, 0, sizeof(chunk->blocks));
    }

    chunk->quad_buffer = nc_renderer_create_buffer(NULL, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 0);
    chunk->face_data_buffer = nc_renderer_create_buffer(NULL, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 0);

    const int chunk_was_added = nc__terrain_chunk_map_set(&terrain->chunks, *coords, chunk);
    NC_ASSERT(chunk_was_added);
    (void)chunk_was_added;

    const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
            &terrain->chunk_columns,
            column_coords);
    int32_t old_top_light_blocking_blocks[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE];
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
        nc__terrain_chunk_column_t* new_column = calloc(1, sizeof(*new_column));
        new_column->ref_count = 1;
        new_column->min_loaded_chunk_y = coords->y;
        new_column->max_loaded_chunk_y = coords->y;
        for (int i = 0; i < NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE; i++) {
            new_column->top_light_blocking_blocks[i] = INT32_MIN;
        }
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

void nc_terrain_unload_chunk(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_ivec3* coords) {
    NC_ASSERT(nc__terrain_chunk_coords_are_valid(coords));

    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (!chunk) {
        return;
    }

    const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, column_coords);
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
        free(*column);
        const int column_was_removed = nc__terrain_chunk_column_map_remove(&terrain->chunk_columns, column_coords);
        NC_ASSERT(column_was_removed);
        (void)column_was_removed;
    } else if (coords->y == (*column)->min_loaded_chunk_y || coords->y == (*column)->max_loaded_chunk_y) {
        int32_t min_loaded_chunk_y = INT32_MAX;
        int32_t max_loaded_chunk_y = INT32_MIN;
        nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&it)) {
            if (it.key.x == coords->x && it.key.z == coords->z) {
                if (it.key.y < min_loaded_chunk_y) {
                    min_loaded_chunk_y = it.key.y;
                }
                if (it.key.y > max_loaded_chunk_y) {
                    max_loaded_chunk_y = it.key.y;
                }
            }
        }
        NC_ASSERT(min_loaded_chunk_y != INT32_MAX && max_loaded_chunk_y != INT32_MIN);
        (*column)->min_loaded_chunk_y = min_loaded_chunk_y;
        (*column)->max_loaded_chunk_y = max_loaded_chunk_y;
    }

    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
    nc__terrain_chunk_fini(renderer, chunk);
}

uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain) {
    return nc__terrain_chunk_map_count(&terrain->chunks);
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

    if (!((result->mesher = nc_mesher_init(result->block_registry)))) {
        goto error;
    }

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
                if (!nc__terrain_offset_chunk_coords(coords, x, y, z, &neighbor_coords)) {
                    continue;
                }

                const nc__terrain_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
                if (neighbor) {
                    blocks[x + 1][y + 1][z + 1] = neighbor->blocks;
                    light_levels[x + 1][y + 1][z + 1] = neighbor->light_levels;
                }
            }
        }
    }
}

static bool nc__terrain_remesh(const nc_terrain_t* terrain, nc_renderer_t* renderer, nc__terrain_chunk_t* chunk) {
    const uint16_t* chunk_and_neighbors[3][3][3];
    const uint8_t* light_levels_and_neighbors[3][3][3];
    nc__terrain_get_chunk_data_and_neighbors(terrain, &chunk->coords, chunk_and_neighbors, light_levels_and_neighbors);

    nc_mesh_quad_vec quads;
    nc_mesh_face_data_vec face_data;
    nc_mesher_compute_chunk(terrain->mesher, chunk_and_neighbors, light_levels_and_neighbors, &quads, &face_data);

    bool result = true;
    chunk->quad_count = quads.count;
    if (chunk->quad_count == 0) {
        goto done;
    }

    result = nc_renderer_queue_buffer_upload(
            renderer,
            chunk->quad_buffer,
            quads.array,
            sizeof(*quads.array) * quads.count);
    result &= nc_renderer_queue_buffer_upload(
            renderer,
            chunk->face_data_buffer,
            face_data.array,
            sizeof(*face_data.array) * nc_mesh_face_data_vec_count(&face_data));

done:
    if (result) {
        chunk->flags &= ~NC__TERRAIN_CHUNK_MESH_DIRTY_BIT;
    }

    nc_mesh_face_data_vec_fini(&face_data);
    nc_mesh_quad_vec_fini(&quads);
    return result;
}

static void nc__terrain_remove_light(nc_terrain_t* terrain) {
    nc__terrain_light_removal_bfs_queue* queue =
            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_BLOCK];
    while (nc__terrain_light_removal_bfs_queue_count(queue)) {
        const nc__terrain_light_removal_node_t node = nc__terrain_light_removal_bfs_queue_pop(
                queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            // Dangling pointer successfully avoided!
            continue;
        }
        vkm_ivec3 local_coords;
        nc__terrain_chunk_index_to_local_coords(node.index, &local_coords);

        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain,
                    chunk,
                    node.index,
                    &local_coords,
                    nc__terrain_light_neighbor_offsets[i],
                    &neighbor)) {
                continue;
            }

            const uint8_t neighbor_light_level = nc__terrain_get_block_light(
                    neighbor.chunk->light_levels[neighbor.index]);
            if (!neighbor_light_level) {
                continue;
            }

            if (neighbor_light_level < node.light_level) {
                const nc_block_t* neighbor_block = nc_block_registry_get(
                        terrain->block_registry,
                        (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
                nc__terrain_set_block_light(
                        &neighbor.chunk->light_levels[neighbor.index],
                        neighbor_block->light_emission);

                vkm_ivec3 neighbor_local_coords;
                nc__terrain_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
                nc__terrain_mark_block_chunks_dirty(
                        terrain,
                        &neighbor.chunk->coords,
                        &neighbor_local_coords);
                nc__terrain_light_removal_bfs_queue_push(
                        queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = neighbor.chunk->coords,
                            .index = neighbor.index,
                            .light_level = neighbor_light_level,
                        });

                if (neighbor_block->light_emission) {
                    nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &neighbor);
                }
            } else {
                // This light came from another source and will refill the invalidated region after removal finishes.
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK, &neighbor);
            }
        }
    }
}

static void nc__terrain_propagate_light(nc_terrain_t* terrain, const nc__terrain_light_channel_t channel) {
    nc__terrain_light_bfs_queue* queue = &terrain->light_bfs_queues[channel];
    while (nc__terrain_light_bfs_queue_count(queue)) {
        const nc__terrain_light_node_t node = nc__terrain_light_bfs_queue_pop(queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            // Dangling pointer successfully avoided!
            continue;
        }
        NC_ASSERT(chunk->queued_light_nodes[channel]);
        light_node_queue_clear(chunk->queued_light_nodes[channel], node.index);

        vkm_ivec3 local_coords;
        nc__terrain_chunk_index_to_local_coords(node.index, &local_coords);

        const uint8_t light_level = nc__terrain_get_light(chunk->light_levels[node.index], channel);
        if (light_level <= 1) {
            continue;
        }

        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain,
                    chunk,
                    node.index,
                    &local_coords,
                    offset,
                    &neighbor)) {
                continue;
            }

            const nc_block_t* neighbor_block = nc_block_registry_get(
                    terrain->block_registry,
                    (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
            const uint8_t propagated = channel == NC__TERRAIN_LIGHT_CHANNEL_SKY
                    && light_level == 15
                    && offset.y < 0
                    ? 15
                    : (uint8_t)(light_level - 1);
            if ((neighbor_block->flags & NC_BLOCK_FLAG_BLOCKS_LIGHT)
                    || nc__terrain_get_light(neighbor.chunk->light_levels[neighbor.index], channel) >= propagated) {
                continue;
            }

            nc__terrain_set_light(&neighbor.chunk->light_levels[neighbor.index], channel, propagated);
            vkm_ivec3 neighbor_local_coords;
            nc__terrain_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
            nc__terrain_mark_block_chunks_dirty(terrain, &neighbor.chunk->coords, &neighbor_local_coords);
            nc__terrain_queue_light_node(terrain, channel, &neighbor);
        }
    }
}

static void nc__terrain_remove_sky_light(nc_terrain_t* terrain) {
    // Do not use the block-light level comparison here. Sky light can contain equal-level plateaus fed by several
    // entrances; after the last entrance closes, treating an equal neighbor as an independent source leaves stale
    // light behind. Flood all non-direct sky light, stop at authoritative direct-sky cells, then propagate them again.
    nc__terrain_light_removal_bfs_queue* queue =
            &terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY];
    while (nc__terrain_light_removal_bfs_queue_count(queue)) {
        const nc__terrain_light_removal_node_t node = nc__terrain_light_removal_bfs_queue_pop(queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            continue;
        }
        vkm_ivec3 local_coords;
        nc__terrain_chunk_index_to_local_coords(node.index, &local_coords);
        for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
            const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
            nc__terrain_block_location_t neighbor;
            if (!nc__terrain_get_light_neighbor(
                    terrain, chunk, node.index, &local_coords, offset, &neighbor)) {
                continue;
            }
            const uint8_t neighbor_light = nc__terrain_get_sky_light(
                    neighbor.chunk->light_levels[neighbor.index]);
            if (!neighbor_light) {
                continue;
            }
            vkm_ivec3 neighbor_local_coords;
            nc__terrain_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
            const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(&neighbor.chunk->coords);
            nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                    &terrain->chunk_columns,
                    column_coords);
            NC_ASSERT(column);
            const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(
                    neighbor_local_coords.x,
                    neighbor_local_coords.z);
            const int32_t world_y = nc__terrain_chunk_local_to_block_coord(
                    neighbor.chunk->coords.y,
                    neighbor_local_coords.y);
            const nc_block_t* neighbor_block = nc_block_registry_get(
                    terrain->block_registry,
                    (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
            const bool direct_sky = nc__terrain_block_has_direct_sky(
                    *column, column_index, world_y, neighbor_block);
            if (!direct_sky) {
                nc__terrain_set_sky_light(&neighbor.chunk->light_levels[neighbor.index], 0);
                nc__terrain_mark_block_chunks_dirty(
                        terrain, &neighbor.chunk->coords, &neighbor_local_coords);
                nc__terrain_light_removal_bfs_queue_push(
                        queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = neighbor.chunk->coords,
                            .index = neighbor.index,
                            .light_level = neighbor_light,
                        });
            } else {
                nc__terrain_queue_light_node(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY, &neighbor);
            }
        }
    }
}

static bool nc__terrain_prepare_chunk_render(
    const nc_terrain_t* terrain,
    nc_renderer_t* renderer,
    nc__terrain_chunk_t* chunk
) {
    if (chunk->flags & NC__TERRAIN_CHUNK_MESH_DIRTY_BIT) {
        return nc__terrain_remesh(terrain, renderer, chunk);
    }

    return true;
}

static void nc__terrain_release_light_queue_memory(nc_terrain_t* terrain) {
    // Relighting is bursty. Do not retain a queue's peak allocation or per-chunk membership bitsets between passes.
    for (int i = 0; i < NC__TERRAIN_LIGHT_CHANNEL_COUNT; i++) {
        nc__terrain_light_bfs_queue_reclaim(&terrain->light_bfs_queues[i]);
        nc__terrain_light_removal_bfs_queue_reclaim(&terrain->light_removal_bfs_queues[i]);
    }
    if (!terrain->light_queue_bitsets_allocated) {
        return;
    }

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        for (int i = 0; i < NC__TERRAIN_LIGHT_CHANNEL_COUNT; i++) {
            free((*it.value)->queued_light_nodes[i]);
            (*it.value)->queued_light_nodes[i] = NULL;
        }
    }
    terrain->light_queue_bitsets_allocated = false;
}

bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    const bool rebuild_sky_light = terrain->sky_light_needs_rebuild;
    nc__terrain_remove_light(terrain);
    if (rebuild_sky_light) {
        // Bulk edits before the first propagation pass are cheaper to resolve once than as a removal wave per block.
        nc__terrain_light_bfs_queue_clear(&terrain->light_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY]);
        nc__terrain_light_removal_bfs_queue_clear(&terrain->light_removal_bfs_queues[NC__TERRAIN_LIGHT_CHANNEL_SKY]);
        nc__terrain_chunk_map_iter_t clear_it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&clear_it)) {
            nc__terrain_chunk_t* chunk = *clear_it.value;
            for (int i = 0; i < NC_MESHER_BLOCKS_PER_CHUNK; i++) {
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
    } else {
        nc__terrain_remove_sky_light(terrain);
    }
    if (rebuild_sky_light || !terrain->sky_light_has_propagated) {
        nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&it)) {
            nc__terrain_queue_chunk_sky_light_frontier(terrain, *it.value);
        }
    }
    nc__terrain_propagate_light(terrain, NC__TERRAIN_LIGHT_CHANNEL_BLOCK);
    nc__terrain_propagate_light(terrain, NC__TERRAIN_LIGHT_CHANNEL_SKY);
    terrain->sky_light_has_propagated = true;

    nc__terrain_release_light_queue_memory(terrain);

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        if (!nc__terrain_prepare_chunk_render(terrain, renderer, *it.value)) {
            return false;
        }
    }

    return true;
}

void nc_terrain_get_opaque_draws(const nc_terrain_t* terrain, nc_renderer_chunk_opaque_draw_vec* draws) {
    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        const nc__terrain_chunk_t* chunk = *it.value;
        if (chunk->quad_count == 0) {
            continue;
        }

        const vkm_ivec3 local_coords = CVKM_IVEC3_ZERO;
        vkm_ivec3 block_coords;
        nc__terrain_chunk_local_to_block_coords(&chunk->coords, &local_coords, &block_coords);

        nc_renderer_chunk_opaque_draw_vec_append(draws, (nc_renderer_chunk_opaque_draw_t){
            .chunk_buffer = chunk->quad_buffer,
            .quad_count = chunk->quad_count,
            .face_data_buffer = chunk->face_data_buffer,
            .texture = terrain->texture_array,
            .position = { {
                .x = (float)block_coords.x,
                .y = (float)block_coords.y,
                .z = (float)block_coords.z,
            } },
        });
    }
}

static bool nc__terrain_floor_float_to_block_coord(const float value, int32_t* result) {
    // TODO: Add floor function to cvkm
    const double floored = floor((double)value);
    if (floored < (double)INT32_MIN || floored > (double)INT32_MAX) {
        return false;
    }

    *result = (int32_t)floored;
    return true;
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
    if (!nc__terrain_floor_float_to_block_coord(camera->position.x, &block_position.x)
            || !nc__terrain_floor_float_to_block_coord(camera->position.y, &block_position.y)
            || !nc__terrain_floor_float_to_block_coord(camera->position.z, &block_position.z)) {
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
        if (nc__terrain_offset_block_coords(hit.block_position, hit.normal, &placement_position)) {
            nc__terrain_set_world_block(terrain, &placement_position, new_block);
        }
    }
}

int32_t nc_terrain_get_top_light_blocking_block(const nc_terrain_t* terrain, const vkm_ivec2 block_column_coords) {
    vkm_ivec2 chunk_column_coords;
    nc__terrain_block_column_to_chunk_column_coords(&block_column_coords, &chunk_column_coords);
    nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
            &terrain->chunk_columns,
            chunk_column_coords);
    if (!column) {
        return INT32_MIN;
    }

    vkm_ivec2 local_coords;
    nc__terrain_block_column_to_chunk_local_coords(&block_column_coords, &chunk_column_coords, &local_coords);
    const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(local_coords.x, local_coords.y);

    return (*column)->top_light_blocking_blocks[column_index];
}

void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    if (!terrain) {
        return;
    }

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        nc__terrain_chunk_fini(renderer, *it.value);
    }
    nc__terrain_chunk_map_fini(&terrain->chunks);

    nc__terrain_chunk_column_map_iter_t column_it = nc__terrain_chunk_column_map_iter(&terrain->chunk_columns);
    while (nc__terrain_chunk_column_map_next(&column_it)) {
        free(*column_it.value);
    }
    nc__terrain_chunk_column_map_fini(&terrain->chunk_columns);
    for (int i = 0; i < NC__TERRAIN_LIGHT_CHANNEL_COUNT; i++) {
        nc__terrain_light_removal_bfs_queue_fini(&terrain->light_removal_bfs_queues[i]);
        nc__terrain_light_bfs_queue_fini(&terrain->light_bfs_queues[i]);
    }

    nc_mesher_fini(terrain->mesher);
    nc_block_registry_fini(terrain->block_registry);
    nc_renderer_destroy_texture(renderer, terrain->texture_array);

    free(terrain);
}
