#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <novacube/block.h>
#include <novacube/chunk.h>
#include <novacube/standard_functions.h>

#define TDS_IMPLEMENT
#define TDS_TYPE nc_chunk_column_dirty_bitset
#define TDS_BIT_COUNT NC_BLOCK_COLUMNS_PER_CHUNK
#define TDS_WORD_T uint64_t
#include <tds/bitset.h>

bool nc_block_offset_coords(const vkm_ivec3 coords, const vkm_bvec3 offset, vkm_ivec3* result) {
    const int64_t x = (int64_t)coords.x + offset.x;
    const int64_t y = (int64_t)coords.y + offset.y;
    const int64_t z = (int64_t)coords.z + offset.z;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || z < INT32_MIN || z > INT32_MAX) {
        return false;
    }
    *result = (vkm_ivec3){ { (int32_t)x, (int32_t)y, (int32_t)z } };
    return true;
}

bool nc_chunk_offset_block_index(
    const vkm_ivec3* chunk_coords,
    const uint16_t index,
    const vkm_bvec3 offset,
    vkm_ivec3* result_chunk_coords,
    uint16_t* result_index
) {
    vkm_ivec3 local_coords;
    NC_CHUNK_INDEX_TO_LOCAL_COORDS(index, local_coords);
    local_coords.x += offset.x;
    local_coords.y += offset.y;
    local_coords.z += offset.z;

    const int chunk_offset_x = nc_block_coord_to_chunk_coord(local_coords.x);
    const int chunk_offset_y = nc_block_coord_to_chunk_coord(local_coords.y);
    const int chunk_offset_z = nc_block_coord_to_chunk_coord(local_coords.z);
    if (!nc_chunk_offset_coords(
            chunk_coords,
            chunk_offset_x,
            chunk_offset_y,
            chunk_offset_z,
            result_chunk_coords)) {
        return false;
    }

    local_coords.x = nc_block_coord_to_chunk_local_coord(local_coords.x, chunk_offset_x);
    local_coords.y = nc_block_coord_to_chunk_local_coord(local_coords.y, chunk_offset_y);
    local_coords.z = nc_block_coord_to_chunk_local_coord(local_coords.z, chunk_offset_z);
    *result_index = (uint16_t)NC_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z);
    return true;
}

nc_chunk_t* nc_chunk_init(
    const vkm_ivec3* coords,
    const uint16_t blocks[NC_BLOCKS_PER_CHUNK]
) {
    nc_chunk_t* result = calloc(1, sizeof(*result));
    result->coords = *coords;
    nc_chunk_replace_blocks(result, blocks);
    return result;
}

void nc_chunk_replace_blocks(nc_chunk_t* chunk, const uint16_t blocks[NC_BLOCKS_PER_CHUNK]) {
    if (blocks) {
        memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
    } else {
        memset(chunk->blocks, 0, sizeof(chunk->blocks));
    }
}

void nc_chunk_fini(nc_renderer_t* renderer, nc_chunk_t* chunk) {
    if (!chunk) {
        return;
    }
    nc_renderer_destroy_chunk_mesh(renderer, chunk->mesh);
    free(chunk->queued_light_nodes[0]);
    free(chunk->queued_light_nodes[1]);
    free(chunk);
}

nc_chunk_column_t* nc_chunk_column_init(const nc_chunk_t* chunk) {
    nc_chunk_column_t* result = calloc(1, sizeof(*result));
    result->ref_count = 1;
    result->min_loaded_chunk_y = chunk->coords.y;
    result->max_loaded_chunk_y = chunk->coords.y;
    for (int i = 0; i < NC_BLOCK_COLUMNS_PER_CHUNK; i++) {
        result->top_light_blocking_blocks[i] = INT32_MIN;
    }
    return result;
}

void nc_chunk_column_fini(nc_chunk_column_t* column) {
    free(column);
}

void nc_chunk_column_include_chunk(
    nc_chunk_column_t* column,
    const nc_chunk_t* chunk,
    const nc_block_registry_t* block_registry
) {
    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_CHUNK_SIZE; x++) {
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
            for (int y = NC_CHUNK_SIZE - 1; y >= 0; y--) {
                const uint16_t block = chunk->blocks[NC_CHUNK_COORDS_TO_INDEX(x, y, z)];
                if (nc_block_registry_get(block_registry, (nc_block_type_t)block)->flags
                        & NC_BLOCK_FLAG_BLOCKS_LIGHT) {
                    const int32_t world_y = nc_chunk_local_coord_to_block_coord(chunk->coords.y, y);
                    if (world_y > column->top_light_blocking_blocks[column_index]) {
                        column->top_light_blocking_blocks[column_index] = world_y;
                    }
                    break;
                }
            }
        }
    }
}

void nc_chunk_column_update_dirty(
    nc_chunk_column_t* column,
    const vkm_ivec2* column_coords,
    const nc_block_registry_t* block_registry,
    const nc_chunk_lookup_fn lookup_chunk,
    void* lookup_context
) {
    for (int z = 0; z < NC_CHUNK_SIZE; z++) {
        for (int x = 0; x < NC_CHUNK_SIZE; x++) {
            const int column_index = NC_CHUNK_COLUMN_COORDS_TO_INDEX(x, z);
            if (!nc_chunk_column_dirty_bitset_get(&column->dirty_top_light_blocking_blocks, column_index)) {
                continue;
            }
            const int32_t top_chunk_y = nc_block_coord_to_chunk_coord(
                    column->top_light_blocking_blocks[column_index]);
            column->top_light_blocking_blocks[column_index] = INT32_MIN;
            for (int32_t chunk_y = top_chunk_y; chunk_y >= column->min_loaded_chunk_y; chunk_y--) {
                const vkm_ivec3 chunk_coords = { { column_coords->x, chunk_y, column_coords->y } };
                const nc_chunk_t* chunk = lookup_chunk(lookup_context, &chunk_coords);
                if (!chunk) {
                    continue;
                }

                for (int y = NC_CHUNK_SIZE - 1; y >= 0; y--) {
                    const uint16_t block = chunk->blocks[NC_CHUNK_COORDS_TO_INDEX(x, y, z)];
                    if (nc_block_registry_get(block_registry, (nc_block_type_t)block)->flags
                            & NC_BLOCK_FLAG_BLOCKS_LIGHT) {
                        column->top_light_blocking_blocks[column_index] =
                                nc_chunk_local_coord_to_block_coord(chunk_y, y);
                        goto found_top_light_blocking_block;
                    }
                }
            }

found_top_light_blocking_block:
            nc_chunk_column_dirty_bitset_clear(&column->dirty_top_light_blocking_blocks, column_index);
        }
    }
}
