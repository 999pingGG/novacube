#pragma once
#ifndef NOVACUBE_CHUNK_H_
#define NOVACUBE_CHUNK_H_

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <novacube/cvkm.h>
#include <novacube/renderer.h>

// Chunk dimensions and block-array layout live here so terrain, generation and meshing cannot disagree about them.
// Local coordinates passed to the indexing macros must be in [0, NC_CHUNK_SIZE).
#define NC_CHUNK_SIZE 16
#define NC_PADDED_CHUNK_SIZE (NC_CHUNK_SIZE + 2)
#define NC_BLOCKS_PER_CHUNK (NC_CHUNK_SIZE * NC_CHUNK_SIZE * NC_CHUNK_SIZE)
#define NC_BLOCK_COLUMNS_PER_CHUNK (NC_CHUNK_SIZE * NC_CHUNK_SIZE)
#define NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z) ((x) + (z) * NC_CHUNK_SIZE)
#define NC_CHUNK_COORDS_TO_INDEX(x, y, z) \
        ((x) + (y) * NC_CHUNK_SIZE + (z) * NC_CHUNK_SIZE * NC_CHUNK_SIZE)
#define NC_CHUNK_INDEX_TO_COORDS(index, x, y, z) \
        do { \
            (x) = (uint8_t)((index) % NC_CHUNK_SIZE); \
            (y) = (uint8_t)((index) / NC_CHUNK_SIZE % NC_CHUNK_SIZE); \
            (z) = (uint8_t)((index) / (NC_CHUNK_SIZE * NC_CHUNK_SIZE)); \
        } while (false)

void nc_chunk_index_to_local_coords(uint16_t index, vkm_ivec3* result);
bool nc_block_offset_coords(vkm_ivec3 coords, vkm_bvec3 offset, vkm_ivec3* result);
bool nc_chunk_offset_block_index(
        const vkm_ivec3* chunk_coords,
        uint16_t index,
        vkm_bvec3 offset,
        vkm_ivec3* result_chunk_coords,
        uint16_t* result_index);

#define NC_MIN_CHUNK_COORD (INT32_MIN / NC_CHUNK_SIZE)
#define NC_MAX_CHUNK_COORD (INT32_MAX / NC_CHUNK_SIZE)

#define TDS_DECLARE
#define TDS_TYPE nc_chunk_column_dirty_bitset
#define TDS_BIT_COUNT NC_BLOCK_COLUMNS_PER_CHUNK
#define TDS_WORD_T uint64_t
#include <tds/bitset.h>

typedef uint8_t nc_chunk_flags_t;
enum {
    NC_CHUNK_MESH_PENDING_BIT = 1 << 0,
};

typedef struct nc_chunk_t {
    vkm_ivec3 coords;
    uint16_t blocks[NC_BLOCKS_PER_CHUNK];
    // Low nibble: block light. High nibble: sky light.
    uint8_t light_levels[NC_BLOCKS_PER_CHUNK];
    // Owned by terrain lighting. Kept opaque here to avoid leaking its private queue-bitset implementation.
    void* queued_light_nodes[2];
    nc_renderer_buffer_t* quad_buffer;
    nc_renderer_buffer_t* face_data_buffer;
    uint32_t quad_count;
    nc_chunk_flags_t flags;
} nc_chunk_t;

typedef struct nc_chunk_column_t {
    int32_t top_light_blocking_blocks[NC_BLOCK_COLUMNS_PER_CHUNK];
    nc_chunk_column_dirty_bitset dirty_top_light_blocking_blocks;
    int32_t min_loaded_chunk_y;
    int32_t max_loaded_chunk_y;
    uint32_t ref_count;
} nc_chunk_column_t;

struct nc_block_registry_t;
typedef nc_chunk_t* (*nc_chunk_lookup_fn)(void* context, const vkm_ivec3* coords);

nc_chunk_t* nc_chunk_init(
        nc_renderer_t* renderer,
        const vkm_ivec3* coords,
        const uint16_t blocks[NC_BLOCKS_PER_CHUNK]);
void nc_chunk_replace_blocks(nc_chunk_t* chunk, const uint16_t blocks[NC_BLOCKS_PER_CHUNK]);
void nc_chunk_fini(nc_renderer_t* renderer, nc_chunk_t* chunk);
nc_chunk_column_t* nc_chunk_column_init(const nc_chunk_t* chunk);
void nc_chunk_column_fini(nc_chunk_column_t* column);
void nc_chunk_column_include_chunk(
        nc_chunk_column_t* column,
        const nc_chunk_t* chunk,
        const struct nc_block_registry_t* block_registry);
void nc_chunk_column_update_dirty(
        nc_chunk_column_t* column,
        const vkm_ivec2* column_coords,
        const struct nc_block_registry_t* block_registry,
        nc_chunk_lookup_fn lookup_chunk,
        void* lookup_context);

// Block-to-chunk conversion is floor division, not C's truncating division: block -1 belongs to chunk -1, not 0.
static inline int32_t nc_block_coord_to_chunk_coord(const int32_t block_coord) {
    int32_t result = block_coord / NC_CHUNK_SIZE;
    if (block_coord < 0 && block_coord % NC_CHUNK_SIZE != 0) {
        result--;
    }
    return result;
}

static inline int32_t nc_block_coord_to_chunk_local_coord(
    const int32_t block_coord,
    const int32_t chunk_coord
) {
    return (int32_t)((int64_t)block_coord - (int64_t)chunk_coord * NC_CHUNK_SIZE);
}

static inline int32_t nc_chunk_local_coord_to_block_coord(
    const int32_t chunk_coord,
    const int32_t local_coord
) {
    return chunk_coord * NC_CHUNK_SIZE + local_coord;
}

static inline void nc_block_to_chunk_coords(const vkm_ivec3* block_coords, vkm_ivec3* chunk_coords) {
    *chunk_coords = (vkm_ivec3){ {
        nc_block_coord_to_chunk_coord(block_coords->x),
        nc_block_coord_to_chunk_coord(block_coords->y),
        nc_block_coord_to_chunk_coord(block_coords->z),
    } };
}

static inline void nc_block_to_chunk_local_coords(
    const vkm_ivec3* block_coords,
    const vkm_ivec3* chunk_coords,
    vkm_ivec3* local_coords
) {
    *local_coords = (vkm_ivec3){ {
        nc_block_coord_to_chunk_local_coord(block_coords->x, chunk_coords->x),
        nc_block_coord_to_chunk_local_coord(block_coords->y, chunk_coords->y),
        nc_block_coord_to_chunk_local_coord(block_coords->z, chunk_coords->z),
    } };
}

static inline vkm_ivec2 nc_chunk_to_chunk_column_coords(const vkm_ivec3* chunk_coords) {
    return (vkm_ivec2){ { chunk_coords->x, chunk_coords->z } };
}

static inline void nc_block_column_to_chunk_column_coords(
    const vkm_ivec2* block_coords,
    vkm_ivec2* chunk_coords
) {
    *chunk_coords = (vkm_ivec2){ {
        nc_block_coord_to_chunk_coord(block_coords->x),
        nc_block_coord_to_chunk_coord(block_coords->y),
    } };
}

static inline void nc_block_column_to_chunk_local_coords(
    const vkm_ivec2* block_coords,
    const vkm_ivec2* chunk_coords,
    vkm_ivec2* local_coords
) {
    *local_coords = (vkm_ivec2){ {
        nc_block_coord_to_chunk_local_coord(block_coords->x, chunk_coords->x),
        nc_block_coord_to_chunk_local_coord(block_coords->y, chunk_coords->y),
    } };
}

static inline bool nc_chunk_coords_are_valid(const vkm_ivec3* coords) {
    return coords->x >= NC_MIN_CHUNK_COORD && coords->x <= NC_MAX_CHUNK_COORD
            && coords->y >= NC_MIN_CHUNK_COORD && coords->y <= NC_MAX_CHUNK_COORD
            && coords->z >= NC_MIN_CHUNK_COORD && coords->z <= NC_MAX_CHUNK_COORD;
}

// Checked chunk arithmetic also guarantees that every block coordinate inside the result fits in an int32_t.
static inline bool nc_chunk_offset_coords(
    const vkm_ivec3* coords,
    const int offset_x,
    const int offset_y,
    const int offset_z,
    vkm_ivec3* result
) {
    const int64_t x = (int64_t)coords->x + offset_x;
    const int64_t y = (int64_t)coords->y + offset_y;
    const int64_t z = (int64_t)coords->z + offset_z;
    if (x < NC_MIN_CHUNK_COORD || x > NC_MAX_CHUNK_COORD
            || y < NC_MIN_CHUNK_COORD || y > NC_MAX_CHUNK_COORD
            || z < NC_MIN_CHUNK_COORD || z > NC_MAX_CHUNK_COORD) {
        return false;
    }
    *result = (vkm_ivec3){ { (int32_t)x, (int32_t)y, (int32_t)z } };
    return true;
}

// World positions use the containing block (mathematical floor), then reuse the same block-to-chunk conversion path.
static inline bool nc_world_position_coord_to_block_coord(const float position, int32_t* block_coord) {
    if (!isfinite(position)) {
        return false;
    }
    const double floored = floor(position);
    if (floored < INT32_MIN || floored > INT32_MAX) {
        return false;
    }
    *block_coord = (int32_t)floored;
    return true;
}

static inline bool nc_world_position_to_block_coords(const vkm_vec3* position, vkm_ivec3* block_coords) {
    return nc_world_position_coord_to_block_coord(position->x, &block_coords->x)
            && nc_world_position_coord_to_block_coord(position->y, &block_coords->y)
            && nc_world_position_coord_to_block_coord(position->z, &block_coords->z);
}

static inline bool nc_world_position_to_chunk_coords(const vkm_vec3* position, vkm_ivec3* chunk_coords) {
    vkm_ivec3 block_coords;
    if (!nc_world_position_to_block_coords(position, &block_coords)) {
        return false;
    }
    nc_block_to_chunk_coords(&block_coords, chunk_coords);
    return true;
}

#endif
