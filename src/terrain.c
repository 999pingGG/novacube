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

#define NC__TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE 5.0f
#define NC__TERRAIN_MIN_CHUNK_COORD (INT32_MIN / NC_MESHER_CHUNK_SIZE)
#define NC__TERRAIN_MAX_CHUNK_COORD (INT32_MAX / NC_MESHER_CHUNK_SIZE)

typedef struct nc__terrain_chunk_t {
    vkm_ivec3 coords;
    uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK];
    // SSBO containing an array of nc_mesh_quad_t.
    nc_renderer_buffer_t* quad_buffer;
    // SSBO containing an array of nc_mesh_face_data_t.
    nc_renderer_buffer_t* face_data_buffer;
    uint32_t vertex_count;
    bool dirty;
} nc__terrain_chunk_t;

typedef struct nc__terrain_raycast_hit_t {
    vkm_ivec3 block_position;
    vkm_bvec3 normal;
    float distance;
} nc__terrain_raycast_hit_t;

static uint64_t nc__terrain_hash_chunk_coords(const vkm_ivec3* coords) {
    return rapidhashNano(coords, sizeof(*coords));
}

// TODO: Define the finish function for the value and use it instead.
#define TDS_TYPE nc__terrain_chunk_map
#define TDS_KEY_T vkm_ivec3
#define TDS_VALUE_T nc__terrain_chunk_t*
#define TDS_HASH_KEY(key) nc__terrain_hash_chunk_coords(&(key))
#define TDS_KEY_EQUALS(a, b) ((a).x == (b).x && (a).y == (b).y && (a).z == (b).z)
#include <tds/hashmap.h>

typedef struct nc_terrain_t {
    nc__terrain_chunk_map chunks;
    nc_renderer_texture_t* texture_array;
    nc_mesher_t* mesher;
    uint16_t block_type_model_ids[NC_BLOCK_TYPE_COUNT + 1];
} nc_terrain_t;

static const char* nc__terrain_texture_paths[] = {
    NC__TERRAIN_ASSETS_BASE_PATH "textures/stone" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/dirt" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/grass" NC__TERRAIN_TEXTURE_EXTENSION,
};

// Make sure the chunk doesn't contain blocks whose coords overflow int32_t.
static bool nc__terrain_chunk_coords_are_valid(const vkm_ivec3* coords) {
    return coords->x >= NC__TERRAIN_MIN_CHUNK_COORD && coords->x <= NC__TERRAIN_MAX_CHUNK_COORD
        && coords->y >= NC__TERRAIN_MIN_CHUNK_COORD && coords->y <= NC__TERRAIN_MAX_CHUNK_COORD
        && coords->z >= NC__TERRAIN_MIN_CHUNK_COORD && coords->z <= NC__TERRAIN_MAX_CHUNK_COORD;
}

// Returns NULL if the chunk isn't loaded.
static nc__terrain_chunk_t* nc__terrain_get_chunk(const nc_terrain_t* terrain, const vkm_ivec3* coords) {
    nc__terrain_chunk_t** chunk = nc__terrain_chunk_map_get(&terrain->chunks, *coords);
    return chunk ? *chunk : NULL;
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
                    neighbor->dirty = true;
                }
            }
        }
    }
}

static int32_t nc__terrain_floor_divide_by_chunk_size(const int32_t value) {
    int32_t result = value / NC_MESHER_CHUNK_SIZE;
    if (value < 0 && value % NC_MESHER_CHUNK_SIZE != 0) {
        result--;
    }
    return result;
}

static void nc__terrain_block_to_chunk_coords(const vkm_ivec3 block_coords, vkm_ivec3* result) {
    *result = (vkm_ivec3){ {
        nc__terrain_floor_divide_by_chunk_size(block_coords.x),
        nc__terrain_floor_divide_by_chunk_size(block_coords.y),
        nc__terrain_floor_divide_by_chunk_size(block_coords.z),
    } };
}

static vkm_ivec3 nc__terrain_block_to_chunk_local_coords(
    const vkm_ivec3 block_coords,
    const vkm_ivec3 chunk_coords
) {
    const vkm_ivec3 result = { {
        (int32_t)((int64_t)block_coords.x - (int64_t)chunk_coords.x * NC_MESHER_CHUNK_SIZE),
        (int32_t)((int64_t)block_coords.y - (int64_t)chunk_coords.y * NC_MESHER_CHUNK_SIZE),
        (int32_t)((int64_t)block_coords.z - (int64_t)chunk_coords.z * NC_MESHER_CHUNK_SIZE),
    } };

    NC_ASSERT(result.x >= 0 && result.x < NC_MESHER_CHUNK_SIZE);
    NC_ASSERT(result.y >= 0 && result.y < NC_MESHER_CHUNK_SIZE);
    NC_ASSERT(result.z >= 0 && result.z < NC_MESHER_CHUNK_SIZE);
    return result;
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

bool nc_terrain_get_block(const nc_terrain_t* terrain, const vkm_ivec3 block_coords, uint16_t* block) {
    vkm_ivec3 chunk_coords;
    nc__terrain_block_to_chunk_coords(block_coords, &chunk_coords);
    const nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    const vkm_ivec3 local_coords = nc__terrain_block_to_chunk_local_coords(block_coords, chunk_coords);

    if (block) {
        *block = chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z)];
    }
    return true;
}

static bool nc__terrain_set_world_block(
    const nc_terrain_t* terrain,
    const vkm_ivec3 block_coords,
    const uint16_t block
) {
    vkm_ivec3 chunk_coords;
    nc__terrain_block_to_chunk_coords(block_coords, &chunk_coords);
    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, &chunk_coords);
    if (!chunk) {
        return false;
    }

    const vkm_ivec3 local_coords = nc__terrain_block_to_chunk_local_coords(block_coords, chunk_coords);

    uint16_t* slot = &chunk->blocks[NC_MESHER_CHUNK_COORDS_TO_INDEX(local_coords.x, local_coords.y, local_coords.z)];
    if (*slot != block) {
        *slot = block;
        nc__terrain_mark_chunk_and_neighbors_dirty(terrain, &chunk_coords);
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
bool nc_terrain_load_or_replace_chunk(
    nc_terrain_t* terrain,
    nc_renderer_t* renderer,
    const vkm_ivec3* coords,
    const uint16_t blocks[NC_MESHER_BLOCKS_PER_CHUNK]
) {
    NC_ASSERT(nc__terrain_chunk_coords_are_valid(coords));

    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (chunk) {
        if (blocks) {
            memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
        } else {
            memset(chunk->blocks, 0, sizeof(chunk->blocks));
        }
        nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
        return true;
    }

    chunk = malloc(sizeof(*chunk));
    chunk->coords = *coords;
    if (blocks) {
        memcpy(chunk->blocks, blocks, sizeof(chunk->blocks));
    } else {
        memset(chunk->blocks, 0, sizeof(chunk->blocks));
    }

    // Start at the minimum size SDL mandates (4) just to create valid buffers.
    // Buffer uploads grow these to the meshed chunk size.
    chunk->quad_buffer = nc_renderer_create_buffer(renderer, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 4);
    if (!chunk->quad_buffer) {
        goto error;
    }

    chunk->face_data_buffer = nc_renderer_create_buffer(renderer, NC_RENDERER_BUFFER_USAGE_GRAPHICS_STORAGE_READ, 4);
    if (!chunk->face_data_buffer) {
        goto error;
    }

    nc__terrain_chunk_map_set(&terrain->chunks, *coords, chunk);
    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
    return true;

error:
    nc__terrain_chunk_fini(renderer, chunk);
    return false;
}

void nc_terrain_unload_chunk(nc_terrain_t* terrain, nc_renderer_t* renderer, const vkm_ivec3* coords) {
    NC_ASSERT(nc__terrain_chunk_coords_are_valid(coords));

    nc__terrain_chunk_t* chunk = nc__terrain_get_chunk(terrain, coords);
    if (!chunk) {
        return;
    }

    nc__terrain_chunk_map_remove(&terrain->chunks, *coords);
    nc__terrain_mark_chunk_and_neighbors_dirty(terrain, coords);
    nc__terrain_chunk_fini(renderer, chunk);
}

uint32_t nc_terrain_get_loaded_chunk_count(const nc_terrain_t* terrain) {
    return nc__terrain_chunk_map_count(&terrain->chunks);
}

static void nc__terrain_set_model_voxel(
    uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL],
    const int x,
    const int y,
    const int z
) {
    const int index = NC_MESHER_BLOCK_MODEL_COORDS_TO_INDEX(x, y, z);
    voxel_data[index >> 4] |= (uint16_t)(1 << (index & 15));
}

static void nc__terrain_initialize_test_block_models(nc_terrain_t* terrain) {
    uint16_t full_cube_data[NC_MESHER_INTS_PER_BLOCK_MODEL];
    memset(full_cube_data, 0xff, sizeof(full_cube_data));

    const uint16_t stone_textures[6] = { 0, 0, 0, 0, 0, 0 };
    const uint16_t dirt_textures[6] = { 1, 1, 1, 1, 1, 1 };
    const uint16_t grass_textures[6] = { 2, 2, 2, 2, 2, 2 };

    terrain->block_type_model_ids[NC_BLOCK_TYPE_AIR] = 0;
    terrain->block_type_model_ids[NC_BLOCK_TYPE_STONE] =
            nc_mesher_register_block_model(terrain->mesher, full_cube_data, stone_textures);

    uint16_t half_cube_data[NC_MESHER_INTS_PER_BLOCK_MODEL] = { 0 };
    for (int z = 0; z < NC_MESHER_BLOCK_MODEL_LENGTH; z++) {
        for (int y = 0; y < NC_MESHER_BLOCK_MODEL_LENGTH / 2; y++) {
            for (int x = 0; x < NC_MESHER_BLOCK_MODEL_LENGTH; x++) {
                nc__terrain_set_model_voxel(half_cube_data, x, y, z);
            }
        }
    }
    terrain->block_type_model_ids[NC_BLOCK_TYPE_DIRT] =
            nc_mesher_register_block_model(terrain->mesher, full_cube_data, dirt_textures);

    uint16_t ramp_data[NC_MESHER_INTS_PER_BLOCK_MODEL] = { 0 };
    for (int z = 0; z < NC_MESHER_BLOCK_MODEL_LENGTH; z++) {
        for (int y = 0; y < NC_MESHER_BLOCK_MODEL_LENGTH; y++) {
            for (int x = 0; x < NC_MESHER_BLOCK_MODEL_LENGTH; x++) {
                if (y <= x) {
                    nc__terrain_set_model_voxel(ramp_data, x, y, z);
                }
            }
        }
    }
    terrain->block_type_model_ids[NC_BLOCK_TYPE_GRASS] =
            nc_mesher_register_block_model(terrain->mesher, full_cube_data, grass_textures);
}

static bool nc__terrain_initialize_test_chunks(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    for (int z = -10; z < 10; z++) {
        for (int y = 0; y < 3; y++) {
            for (int x = -10; x < 10; x++) {
                if (!nc_terrain_load_or_replace_chunk(terrain, renderer, &(vkm_ivec3){ { x, y, z } }, NULL)) {
                    return false;
                }
            }
        }
    }

    return true;
}

static void nc__terrain_set_test_block(const nc_terrain_t* terrain, const vkm_ivec3 coords, const uint16_t block) {
    const bool result = nc__terrain_set_world_block(terrain, coords, block);
    NC_ASSERT(result);
    (void)result;
}

static void nc__terrain_initialize_test_blocks(const nc_terrain_t* terrain) {
    for (int z = -10 * NC_MESHER_CHUNK_SIZE; z < 9 * NC_MESHER_CHUNK_SIZE; z++) {
        for (int x = -10 * NC_MESHER_CHUNK_SIZE; x < 9 * NC_MESHER_CHUNK_SIZE; x++) {
            const int height = (int)(((15.0f + vkm_sin((float)z / 3.0f) * 3.0f) + (15.0f + vkm_cos((float)x / 3.0f) * 3.0f)) / 2.0f);

            for (int y = 0; y < height; y++) {
                int type;
                if (y == height - 1) {
                    type = NC_BLOCK_TYPE_GRASS;
                } else if (y > height - 5) {
                    type = NC_BLOCK_TYPE_DIRT;
                } else {
                    type = NC_BLOCK_TYPE_STONE;
                }

                nc__terrain_set_test_block(
                        terrain,
                        (vkm_ivec3){ { x, y, z } },
                        terrain->block_type_model_ids[type]);
            }
        }
    }
}

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer) {
    nc_terrain_t* result = calloc(1, sizeof(*result));

    if (!((result->texture_array = nc_renderer_create_texture_array_from_files(
            renderer,
            nc__terrain_texture_paths,
            NC_COUNTOF(nc__terrain_texture_paths))))) {
        goto error;
    }

    if (!((result->mesher = nc_mesher_init()))) {
        goto error;
    }

    nc__terrain_initialize_test_block_models(result);

    if (!nc__terrain_initialize_test_chunks(result, renderer)) {
        goto error;
    }

    nc__terrain_initialize_test_blocks(result);
    return result;

error:
    nc_terrain_fini(result, renderer);
    return NULL;
}

static void nc__terrain_load_chunk_and_neighbors(
    const nc_terrain_t* terrain,
    const vkm_ivec3* coords,
    const uint16_t* chunk_and_neighbors[3][3][3]
) {
    memset(chunk_and_neighbors, 0, sizeof(const uint16_t*) * 3 * 3 * 3);

    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vkm_ivec3 neighbor_coords;
                if (!nc__terrain_offset_chunk_coords(coords, x, y, z, &neighbor_coords)) {
                    continue;
                }

                const nc__terrain_chunk_t* neighbor = nc__terrain_get_chunk(terrain, &neighbor_coords);
                if (neighbor) {
                    chunk_and_neighbors[x + 1][y + 1][z + 1] = neighbor->blocks;
                }
            }
        }
    }
}

static bool nc__terrain_prepare_chunk_render(
    const nc_terrain_t* terrain,
    nc_renderer_t* renderer,
    nc__terrain_chunk_t* chunk
) {
    if (!chunk->dirty) {
        return true;
    }

    const uint16_t* chunk_and_neighbors[3][3][3];
    nc__terrain_load_chunk_and_neighbors(terrain, &chunk->coords, chunk_and_neighbors);

    nc_mesh_quad_vec quads;
    nc_mesh_face_data_vec face_data;
    nc_mesher_compute_chunk(terrain->mesher, chunk_and_neighbors, &quads, &face_data);

    bool result = true;
    chunk->vertex_count = quads.count * 6;
    if (chunk->vertex_count == 0) {
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
            sizeof(*face_data.array) * face_data.count);

done:
    if (result) {
        chunk->dirty = false;
    }

    nc_mesh_face_data_vec_fini(&face_data);
    nc_mesh_quad_vec_fini(&quads);
    return result;
}

bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer) {
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
    const vkm_mat4* view_projection,
    nc_renderer_chunk_opaque_draw_vec* draws
) {
    *draws = (nc_renderer_chunk_opaque_draw_vec){ 0 };

    nc__terrain_chunk_map_iter_t it = nc__terrain_chunk_map_iter(&terrain->chunks);
    while (nc__terrain_chunk_map_next(&it)) {
        const nc__terrain_chunk_t* chunk = *it.value;
        if (chunk->vertex_count == 0) {
            continue;
        }

        nc_renderer_chunk_opaque_draw_vec_append(draws, (nc_renderer_chunk_opaque_draw_t){
            .chunk_buffer = chunk->quad_buffer,
            .vertex_count = chunk->vertex_count,
            .face_data_buffer = chunk->face_data_buffer,
            .texture = terrain->texture_array,
            .view_projection = view_projection,
            .position = { {
                .x = (float)(chunk->coords.x * NC_MESHER_CHUNK_SIZE),
                .y = (float)(chunk->coords.y * NC_MESHER_CHUNK_SIZE),
                .z = (float)(chunk->coords.z * NC_MESHER_CHUNK_SIZE),
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
static bool nc__terrain_get_target_block(
    const nc_terrain_t* terrain,
    const nc_camera_t* camera,
    nc__terrain_raycast_hit_t* hit
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
    while (distance <= NC__TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE) {
        uint16_t block;
        if (nc_terrain_get_block(terrain, block_position, &block) && block != 0) {
            *hit = (nc__terrain_raycast_hit_t){
                .block_position = block_position,
                .normal = normal,
                .distance = distance,
            };
            return true;
        }

        if (t_max.x <= t_max.y && t_max.x <= t_max.z) {
            if (t_max.x > NC__TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE) {
                break;
            }
            if (!nc__terrain_step_raycast_axis(&block_position.x, step.x)) {
                break;
            }
            distance = t_max.x;
            t_max.x += t_delta.x;
            normal = (vkm_bvec3){ { (int8_t)-step.x, 0, 0 } };
        } else if (t_max.y <= t_max.z) {
            if (t_max.y > NC__TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE) {
                break;
            }
            if (!nc__terrain_step_raycast_axis(&block_position.y, step.y)) {
                break;
            }
            distance = t_max.y;
            t_max.y += t_delta.y;
            normal = (vkm_bvec3){ { 0, (int8_t)-step.y, 0 } };
        } else {
            if (t_max.z > NC__TERRAIN_MAX_BLOCK_MODIFICATION_DISTANCE) {
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
    const vkm_mat4* view_projection,
    const float time,
    const nc_camera_t* camera,
    nc_renderer_block_highlight_draw_t* draw
) {
    nc__terrain_raycast_hit_t hit;
    if (!nc__terrain_get_target_block(terrain, camera, &hit)) {
        *draw = (nc_renderer_block_highlight_draw_t){
            .shown = false,
        };
        return;
    }

    *draw = (nc_renderer_block_highlight_draw_t){
        .view_projection = view_projection,
        .position = { { (float)hit.block_position.x, (float)hit.block_position.y, (float)hit.block_position.z } },
        .normal = { { hit.normal.x, hit.normal.y, hit.normal.z } },
        .time = time,
        .shown = true,
    };
}

void nc_terrain_modify_block(nc_terrain_t* terrain, const nc_camera_t* camera, const nc_block_type_t new_block) {
    nc__terrain_raycast_hit_t hit;
    if (!nc__terrain_get_target_block(terrain, camera, &hit)) {
        return;
    }

    if (new_block == NC_BLOCK_TYPE_AIR) {
        nc__terrain_set_world_block(terrain, hit.block_position, 0);
    } else if (new_block <= NC_BLOCK_TYPE_COUNT && !nc__terrain_normal_is_zero(hit.normal)) {
        vkm_ivec3 place_position;
        if (nc__terrain_offset_block_coords(hit.block_position, hit.normal, &place_position)) {
            nc__terrain_set_world_block(terrain, place_position, terrain->block_type_model_ids[new_block]);
        }
    }
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

    nc_mesher_fini(terrain->mesher);
    nc_renderer_destroy_texture(renderer, terrain->texture_array);

    free(terrain);
}
