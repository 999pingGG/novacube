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

typedef uint8_t nc__terrain_chunk_flags_t;
enum {
    NC__TERRAIN_CHUNK_MESH_DIRTY_BIT = 1 << 0,
};

typedef struct nc__terrain_chunk_t {
    vkm_ivec3 coords;
    uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK];
    uint8_t light_levels[NC_MESHER_BLOCKS_PER_CHUNK];
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
    int32_t top_solid_blocks[NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE];
    nc__terrain_chunk_column_dirty_bitset dirty_top_solid_blocks;
    int32_t min_loaded_chunk_y;
    uint32_t ref_count;
    bool dirty;
} nc__terrain_chunk_column_t;

#define TDS_TYPE nc__terrain_chunk_column_map
#define TDS_KEY_T vkm_ivec2
#define TDS_VALUE_T nc__terrain_chunk_column_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_xz_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y)
#include <tds/hashmap.h>

#define TDS_TYPE nc__terrain_chunk_column_coords_vec
#define TDS_VALUE_T vkm_ivec2
#include <tds/vector.h>

#define TDS_TYPE nc__terrain_light_bfs_queue
#define TDS_VALUE_T nc__terrain_light_node_t
#include <tds/queue.h>

#define TDS_TYPE nc__terrain_light_removal_bfs_queue
#define TDS_VALUE_T nc__terrain_light_removal_node_t
#include <tds/queue.h>

typedef struct nc_terrain_t {
    nc__terrain_chunk_map chunks;
    nc__terrain_chunk_column_map chunk_columns;
    nc__terrain_chunk_column_coords_vec dirty_chunk_columns;
    nc_renderer_texture_t* texture_array;
    nc_block_registry_t* block_registry;
    nc_mesher_t* mesher;
    nc__terrain_light_bfs_queue light_bfs_queue;
    nc__terrain_light_removal_bfs_queue light_removal_bfs_queue;
} nc_terrain_t;

static const vkm_bvec3 nc__terrain_light_neighbor_offsets[] = {
    { { -1,  0,  0 } },
    { {  1,  0,  0 } },
    { {  0, -1,  0 } },
    { {  0,  1,  0 } },
    { {  0,  0, -1 } },
    { {  0,  0,  1 } },
};

static void nc__terrain_remove_light(nc_terrain_t* terrain);

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
                if (nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block)->fully_solid) {
                    const int32_t world_y = nc__terrain_chunk_local_to_block_coord(chunk->coords.y, y);
                    if (world_y > column->top_solid_blocks[column_index]) {
                        column->top_solid_blocks[column_index] = world_y;
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
            if (!nc__terrain_chunk_column_dirty_bitset_get(&column->dirty_top_solid_blocks, column_index)) {
                continue;
            }

            const int32_t top_chunk_y = nc__terrain_floor_divide_by_chunk_size(column->top_solid_blocks[column_index]);
            column->top_solid_blocks[column_index] = INT32_MIN;
            for (int32_t chunk_y = top_chunk_y; chunk_y >= column->min_loaded_chunk_y; chunk_y--) {
                const vkm_ivec3 chunk_coords = { { column_coords.x, chunk_y, column_coords.y } };
                const nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
                if (!chunk) {
                    continue;
                }

                for (int y = NC_MESHER_CHUNK_SIZE - 1; y >= 0; y--) {
                    const uint16_t block = chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z)];
                    if (nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block)->fully_solid) {
                        column->top_solid_blocks[column_index] = nc__terrain_chunk_local_to_block_coord(chunk_y, y);
                        goto found_top_solid_block;
                    }
                }
            }

found_top_solid_block:
            nc__terrain_chunk_column_dirty_bitset_clear(&column->dirty_top_solid_blocks, column_index);
        }
    }
    column->dirty = false;
}

static void nc__terrain_queue_dirty_chunk_column(
    nc_terrain_t* terrain,
    const vkm_ivec2 column_coords,
    nc__terrain_chunk_column_t* column
) {
    if (!column->dirty) {
        nc__terrain_chunk_column_coords_vec_append(&terrain->dirty_chunk_columns, column_coords);
        column->dirty = true;
    }
}

static void nc__terrain_update_dirty_chunk_columns(nc_terrain_t* terrain) {
    for (uint32_t i = 0; i < nc__terrain_chunk_column_coords_vec_count(&terrain->dirty_chunk_columns); i++) {
        const vkm_ivec2 coords = nc__terrain_chunk_column_coords_vec_get(&terrain->dirty_chunk_columns, i);
        nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(&terrain->chunk_columns, coords);
        if (column) {
            nc__terrain_update_dirty_chunk_column(terrain, coords, *column);
        }
    }
    nc__terrain_chunk_column_coords_vec_clear(&terrain->dirty_chunk_columns);
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

    *result = (vkm_ivec3){ {
        index % NC_MESHER_CHUNK_SIZE,
        index / NC_MESHER_CHUNK_SIZE % NC_MESHER_CHUNK_SIZE,
        index / (NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE),
    } };
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
    nc__terrain_light_bfs_queue* queue,
    const nc__terrain_block_location_t* location
) {
    nc__terrain_light_bfs_queue_push(queue, (nc__terrain_light_node_t){
        .chunk_coords = location->chunk->coords,
        .index = location->index,
    });
}

static void nc__terrain_queue_light_neighbors(
    nc_terrain_t* terrain,
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
                && neighbor.chunk->light_levels[neighbor.index] > 1) {
            nc__terrain_queue_light_node(&terrain->light_bfs_queue, &neighbor);
        }
    }
}

static void nc__terrain_queue_chunk_boundary_light_removal(
    nc_terrain_t* terrain,
    const nc__terrain_chunk_t* chunk
) {
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                if (x != 0 && x != NC_MESHER_CHUNK_SIZE - 1
                        && y != 0 && y != NC_MESHER_CHUNK_SIZE - 1
                        && z != 0 && z != NC_MESHER_CHUNK_SIZE - 1) {
                    continue;
                }

                const uint16_t index = (uint16_t)NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z);
                const uint8_t light_level = chunk->light_levels[index];
                if (light_level) {
                    nc__terrain_light_removal_bfs_queue_push(
                            &terrain->light_removal_bfs_queue,
                            (nc__terrain_light_removal_node_t){
                                .chunk_coords = chunk->coords,
                                .index = index,
                                .light_level = light_level,
                            });
                }
            }
        }
    }
}

static void nc__terrain_seed_chunk_light(nc_terrain_t* terrain, nc__terrain_chunk_t* chunk) {
    // X must remain innermost so index matches NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z).
    uint16_t index = 0;
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++, index++) {
                const nc_block_t* block = nc_block_registry_get(
                        terrain->block_registry,
                        (nc_block_type_t)chunk->blocks[index]);
                if (block->light_emission) {
                    chunk->light_levels[index] = block->light_emission;
                    nc__terrain_light_bfs_queue_push(&terrain->light_bfs_queue, (nc__terrain_light_node_t){
                        .chunk_coords = chunk->coords,
                        .index = index,
                    });
                }

                // Existing light in adjacent chunks only needs to be requeued along the newly loaded boundary.
                if (x != 0 && x != NC_MESHER_CHUNK_SIZE - 1
                        && y != 0 && y != NC_MESHER_CHUNK_SIZE - 1
                        && z != 0 && z != NC_MESHER_CHUNK_SIZE - 1) {
                    continue;
                }

                const vkm_ivec3 local_coords = { { x, y, z } };
                for (int i = 0; i < (int)NC_COUNTOF(nc__terrain_light_neighbor_offsets); i++) {
                    const vkm_bvec3 offset = nc__terrain_light_neighbor_offsets[i];
                    if ((offset.x < 0 && x != 0)
                            || (offset.x > 0 && x != NC_MESHER_CHUNK_SIZE - 1)
                            || (offset.y < 0 && y != 0)
                            || (offset.y > 0 && y != NC_MESHER_CHUNK_SIZE - 1)
                            || (offset.z < 0 && z != 0)
                            || (offset.z > 0 && z != NC_MESHER_CHUNK_SIZE - 1)) {
                        continue;
                    }

                    nc__terrain_block_location_t neighbor;
                    if (nc__terrain_get_light_neighbor(
                            terrain,
                            chunk,
                            index,
                            &local_coords,
                            offset,
                            &neighbor)
                            && neighbor.chunk != chunk
                            && neighbor.chunk->light_levels[neighbor.index] > 1) {
                        nc__terrain_queue_light_node(&terrain->light_bfs_queue, &neighbor);
                    }
                }
            }
        }
    }
    NC_ASSERT(index == NC_MESHER_BLOCKS_PER_CHUNK);
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

static bool nc__terrain_set_world_block(
    nc_terrain_t* terrain,
    const vkm_ivec3* block_coords,
    const uint16_t block
) {
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
        const uint8_t old_light_level = chunk->light_levels[index];
        *slot = block;

        const nc_block_t* new_block = nc_block_registry_get(terrain->block_registry, (nc_block_type_t)block);
        if (old_block->light_emission != new_block->light_emission
                || old_block->fully_solid != new_block->fully_solid) {
            if (old_light_level) {
                nc__terrain_light_removal_bfs_queue_push(
                        &terrain->light_removal_bfs_queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = chunk_coords,
                            .index = index,
                            .light_level = old_light_level,
                        });
            }

            chunk->light_levels[index] = new_block->light_emission;
            if (new_block->light_emission) {
                nc__terrain_light_bfs_queue_push(&terrain->light_bfs_queue, (nc__terrain_light_node_t){
                    .chunk_coords = chunk_coords,
                    .index = index,
                });
            }

            // Requeue nearby surviving light. This is necessary when an opaque block becomes transparent and also
            // refills any gaps left by the removal pass.
            nc__terrain_queue_light_neighbors(terrain, chunk, index, &local_coords);
        }

        nc__terrain_chunk_column_t** column = nc__terrain_chunk_column_map_get(
                &terrain->chunk_columns,
                nc__terrain_chunk_column_coords(&chunk_coords));
        NC_ASSERT(column);

        const int column_index = NC__TERRAIN_CHUNK_COLUMN_BLOCK_INDEX(local_coords.x, local_coords.z);
        if (new_block->fully_solid) {
            if (block_coords->y > (*column)->top_solid_blocks[column_index]) {
                (*column)->top_solid_blocks[column_index] = block_coords->y;
            }
        } else if (old_block->fully_solid && block_coords->y == (*column)->top_solid_blocks[column_index]) {
            nc__terrain_chunk_column_dirty_bitset_set(&(*column)->dirty_top_solid_blocks, column_index);
            nc__terrain_queue_dirty_chunk_column(
                    terrain,
                    nc__terrain_chunk_column_coords(&chunk_coords),
                    *column);
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
        nc__terrain_chunk_column_dirty_bitset_set_all(&(*column)->dirty_top_solid_blocks);
        nc__terrain_queue_dirty_chunk_column(terrain, nc__terrain_chunk_column_coords(coords), *column);
        nc__terrain_update_chunk_column_from_chunk(terrain, *column, chunk);
        nc__terrain_seed_chunk_light(terrain, chunk);
        nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
        return;
    }

    chunk = malloc(sizeof(*chunk));
    memset(chunk->light_levels, 0, sizeof(chunk->light_levels));
    chunk->coords = *coords;
    if (blocks) {
        memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
    } else {
        memset(chunk->blocks, 0, sizeof(chunk->blocks));
    }

    chunk->quad_buffer = nc_renderer_create_buffer(NULL, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 0);
    chunk->face_data_buffer = nc_renderer_create_buffer(NULL, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 0);
    chunk->flags = 0;

    const int chunk_was_added = nc__terrain_chunk_map_set(&terrain->chunks, *coords, chunk);
    NC_ASSERT(chunk_was_added);
    (void)chunk_was_added;

    const vkm_ivec2 column_coords = nc__terrain_chunk_column_coords(coords);
    nc__terrain_chunk_column_t** existing_column = nc__terrain_chunk_column_map_get(
            &terrain->chunk_columns,
            column_coords);
    if (existing_column) {
        NC_ASSERT((*existing_column)->ref_count < UINT32_MAX);
        (*existing_column)->ref_count++;
        if (coords->y < (*existing_column)->min_loaded_chunk_y) {
            (*existing_column)->min_loaded_chunk_y = coords->y;
        }
        nc__terrain_update_chunk_column_from_chunk(terrain, *existing_column, chunk);
    } else {
        nc__terrain_chunk_column_t* column = calloc(1, sizeof(*column));
        column->ref_count = 1;
        column->min_loaded_chunk_y = coords->y;
        for (int i = 0; i < NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE; i++) {
            column->top_solid_blocks[i] = INT32_MIN;
        }
        nc__terrain_update_chunk_column_from_chunk(terrain, column, chunk);
        const int column_was_added = nc__terrain_chunk_column_map_set(
                &terrain->chunk_columns,
                column_coords,
                column);
        NC_ASSERT(column_was_added);
        (void)column_was_added;
    }
    nc__terrain_seed_chunk_light(terrain, chunk);
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
    if (nc__terrain_chunk_column_dirty_bitset_any(&(*column)->dirty_top_solid_blocks)) {
        nc__terrain_update_dirty_chunk_column(terrain, column_coords, *column);
    }

    // The removal nodes need the chunk to remain loaded while they walk across its boundary.
    nc__terrain_queue_chunk_boundary_light_removal(terrain, chunk);
    memset(chunk->light_levels, 0, sizeof(chunk->light_levels));
    nc__terrain_remove_light(terrain);

    nc__terrain_chunk_map_remove(&terrain->chunks, *coords);

    (*column)->ref_count--;
    if ((*column)->ref_count == 0) {
        free(*column);
        const int column_was_removed = nc__terrain_chunk_column_map_remove(&terrain->chunk_columns, column_coords);
        NC_ASSERT(column_was_removed);
        (void)column_was_removed;
    } else if (coords->y == (*column)->min_loaded_chunk_y) {
        int32_t min_loaded_chunk_y = INT32_MAX;
        nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
        while (nc__terrain_chunk_map_next(&it)) {
            if (it.key.x == coords->x && it.key.z == coords->z && it.key.y < min_loaded_chunk_y) {
                min_loaded_chunk_y = it.key.y;
            }
        }
        NC_ASSERT(min_loaded_chunk_y != INT32_MAX);
        (*column)->min_loaded_chunk_y = min_loaded_chunk_y;
    }

    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
    nc__terrain_chunk_fini(renderer, chunk);
}

uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain) {
    return nc__terrain_chunk_map_count(&terrain->chunks);
}

static void nc__terrain_initialize_test_chunks(nc_terrain_t* terrain) {
    for (int z = -10; z < 10; z++) {
        for (int y = 0; y < 3; y++) {
            for (int x = -10; x < 10; x++) {
                nc_terrain_load_or_replace_chunk(terrain, &(vkm_ivec3){ { x, y, z } }, NULL);
            }
        }
    }
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

    nc__terrain_initialize_test_chunks(result);

    // We're gonna need a few of these.
    nc__terrain_light_bfs_queue_reserve(&result->light_bfs_queue, 8192);
    nc__terrain_light_removal_bfs_queue_reserve(&result->light_removal_bfs_queue, 8192);

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

static bool nc__terrain_remesh(
    const nc_terrain_t* terrain,
    nc_renderer_t* renderer,
    nc__terrain_chunk_t* chunk
) {
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
    while (nc__terrain_light_removal_bfs_queue_count(&terrain->light_removal_bfs_queue)) {
        const nc__terrain_light_removal_node_t node = nc__terrain_light_removal_bfs_queue_pop(
                &terrain->light_removal_bfs_queue);
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

            const uint8_t neighbor_light_level = neighbor.chunk->light_levels[neighbor.index];
            if (!neighbor_light_level) {
                continue;
            }

            if (neighbor_light_level < node.light_level) {
                const nc_block_t* neighbor_block = nc_block_registry_get(
                        terrain->block_registry,
                        (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
                neighbor.chunk->light_levels[neighbor.index] = neighbor_block->light_emission;

                vkm_ivec3 neighbor_local_coords;
                nc__terrain_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
                nc__terrain_mark_block_chunks_dirty(
                        terrain,
                        &neighbor.chunk->coords,
                        &neighbor_local_coords);
                nc__terrain_light_removal_bfs_queue_push(
                        &terrain->light_removal_bfs_queue,
                        (nc__terrain_light_removal_node_t){
                            .chunk_coords = neighbor.chunk->coords,
                            .index = neighbor.index,
                            .light_level = neighbor_light_level,
                        });

                if (neighbor_block->light_emission) {
                    nc__terrain_queue_light_node(&terrain->light_bfs_queue, &neighbor);
                }
            } else {
                // This light came from another source and will refill the invalidated region after removal finishes.
                nc__terrain_queue_light_node(&terrain->light_bfs_queue, &neighbor);
            }
        }
    }
}

static void nc__terrain_propagate_light(nc_terrain_t* terrain) {
    while (nc__terrain_light_bfs_queue_count(&terrain->light_bfs_queue)) {
        const nc__terrain_light_node_t node = nc__terrain_light_bfs_queue_pop(&terrain->light_bfs_queue);
        nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &node.chunk_coords);
        if (!chunk) {
            // Dangling pointer successfully avoided!
            continue;
        }

        vkm_ivec3 local_coords;
        nc__terrain_chunk_index_to_local_coords(node.index, &local_coords);

        const uint8_t light_level = chunk->light_levels[node.index];
        if (light_level <= 1) {
            continue;
        }

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

            const nc_block_t* neighbor_block = nc_block_registry_get(
                    terrain->block_registry,
                    (nc_block_type_t)neighbor.chunk->blocks[neighbor.index]);
            const uint8_t neighbor_light_level = neighbor.chunk->light_levels[neighbor.index];
            if (neighbor_block->fully_solid || neighbor_light_level + 2 > light_level) {
                continue;
            }

            neighbor.chunk->light_levels[neighbor.index] = (uint8_t)(light_level - 1);
            vkm_ivec3 neighbor_local_coords;
            nc__terrain_chunk_index_to_local_coords(neighbor.index, &neighbor_local_coords);
            nc__terrain_mark_block_chunks_dirty(terrain, &neighbor.chunk->coords, &neighbor_local_coords);
            nc__terrain_queue_light_node(&terrain->light_bfs_queue, &neighbor);
        }
    }
}

static bool nc__terrain_prepare_chunk_render(
    const nc_terrain_t* terrain,
    nc_renderer_t* renderer,
    nc__terrain_chunk_t* chunk
) {
    if ((chunk->flags & NC__TERRAIN_CHUNK_MESH_DIRTY_BIT)) {
        return nc__terrain_remesh(terrain, renderer, chunk);
    }

    return true;
}

bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    // Top solid blocks are lighting data, so defer downward rescans until the terrain is prepared for rendering.
    nc__terrain_update_dirty_chunk_columns(terrain);

    nc__terrain_remove_light(terrain);
    nc__terrain_propagate_light(terrain);

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        if (!nc__terrain_prepare_chunk_render(terrain, renderer, *it.value)) {
            return false;
        }
    }

    return true;
}

void nc_terrain_get_opaque_draws(
    const nc_terrain_t* terrain,
    nc_renderer_chunk_opaque_draw_vec* draws
) {
    *draws = (nc_renderer_chunk_opaque_draw_vec){ 0 };

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

int32_t nc_terrain_get_top_solid_block(const nc_terrain_t* terrain, const vkm_ivec2 block_column_coords) {
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

    return (*column)->top_solid_blocks[column_index];
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
    nc__terrain_chunk_column_coords_vec_fini(&terrain->dirty_chunk_columns);
    nc__terrain_light_removal_bfs_queue_fini(&terrain->light_removal_bfs_queue);
    nc__terrain_light_bfs_queue_fini(&terrain->light_bfs_queue);

    nc_mesher_fini(terrain->mesher);
    nc_block_registry_fini(terrain->block_registry);
    nc_renderer_destroy_texture(renderer, terrain->texture_array);

    free(terrain);
}
