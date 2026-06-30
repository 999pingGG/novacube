#include <stdint.h>
#include <stdlib.h>

#include <novacube/cvkm.h>
#include <novacube/macros.h>
#include <novacube/terrain.h>

#ifdef ANDROID
#define NC__TERRAIN_ASSETS_BASE_PATH ""
#define NC__TERRAIN_TEXTURE_EXTENSION ".astc"
#else
#define NC__TERRAIN_ASSETS_BASE_PATH "assets/"
#define NC__TERRAIN_TEXTURE_EXTENSION ".png"
#endif

#define NC__CHUNK_LENGTH 256
#define NC__CHUNK_COUNT (NC__CHUNK_LENGTH * NC__CHUNK_LENGTH * NC__CHUNK_LENGTH)

typedef struct nc__terrain_block_t {
    /* The renderer reads the block position and block type as one packed ubyte4. */
    vkm_ubvec3 position;
    nc_block_type_t type;
} nc__terrain_block_t;

#define TDS_VALUE_T nc__terrain_block_t
#define TDS_TYPE nc__terrain_block_dense_pool_t
#define TDS_INITIAL_CAPACITY NC__CHUNK_COUNT
#include <tds/dense-pool.h>

typedef struct nc_terrain_t {
    nc__terrain_block_dense_pool_t chunk;
    nc_renderer_buffer_t* instance_buffer;
    nc_renderer_texture_t* texture_array;
    bool render_dirty;
} nc_terrain_t;

static const char* nc__terrain_texture_paths[] = {
    NC__TERRAIN_ASSETS_BASE_PATH "textures/stone" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/dirt" NC__TERRAIN_TEXTURE_EXTENSION,
    NC__TERRAIN_ASSETS_BASE_PATH "textures/grass" NC__TERRAIN_TEXTURE_EXTENSION,
};

nc_terrain_t* nc_terrain_init(nc_renderer_t* renderer) {
    nc_terrain_t* result = calloc(1, sizeof(*result));

    for (int z = 126; z < 129; z++) {
        for (int y = 126; y < 129; y++) {
            for (int x = 126; x < 129; x++) {
                nc__terrain_block_dense_pool_t_append(&result->chunk, (nc__terrain_block_t){
                    .position = { { (uint8_t)x, (uint8_t)y, (uint8_t)z } },
                    .type = y == 126 ? NC_BLOCK_TYPE_STONE : y == 127 ? NC_BLOCK_TYPE_DIRT : NC_BLOCK_TYPE_GRASS,
                });
            }
        }
    }

    if (!((result->texture_array = nc_renderer_create_texture_array_from_files(
            renderer,
            nc__terrain_texture_paths,
            NC_COUNTOF(nc__terrain_texture_paths))))) {
        goto error;
    }

    if (!((result->instance_buffer = nc_renderer_create_buffer(
            renderer,
            NC_RENDERER_BUFFER_USAGE_VERTEX,
            NC__CHUNK_COUNT * sizeof(nc__terrain_block_t))))) {
        goto error;
    }

    result->render_dirty = true;
    return result;

error:
    nc_terrain_fini(result, renderer);
    return NULL;
}

bool nc_terrain_prepare_render(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    if (!terrain->render_dirty) {
        return true;
    }

    if (terrain->chunk.count == 0) {
        // No blocks in the chunk.
        terrain->render_dirty = false;
        return true;
    }

    const bool result = nc_renderer_queue_buffer_upload(
            renderer,
            terrain->instance_buffer,
            terrain->chunk.array,
            terrain->chunk.count * sizeof(*terrain->chunk.array));
    if (result) {
        terrain->render_dirty = false;
    }

    return result;
}

void nc_terrain_get_opaque_draw(
    const nc_terrain_t* terrain,
    const vkm_mat4* view_projection,
    nc_renderer_opaque_draw_t* draw
) {
    *draw = (nc_renderer_opaque_draw_t){
        .instance_buffer = terrain->instance_buffer,
        .instance_count = terrain->chunk.count,
        .texture = terrain->texture_array,
        .view_projection = view_projection,
    };
}

void nc_terrain_modify_block(
    nc_terrain_t* terrain,
    const vkm_vec3* camera_position,
    const float camera_yaw,
    const float camera_pitch,
    const nc_block_type_t new_block
) {
    // https://tavianator.com/2011/ray_box.html
    // https://tavianator.com/cgit/dimension.git/tree/libdimension/bvh/bvh.c#n178
    // struct dmnsn_optimized_ray and dmnsn_ray_box_intersection()
    const float pitch_cosine = vkm_cos(camera_pitch);
    vkm_vec3 inverse_ray_direction = { {
        1.0f / (pitch_cosine * vkm_sin(camera_yaw)),
        1.0f / vkm_sin(camera_pitch),
        1.0f / (pitch_cosine * vkm_cos(camera_yaw)),
    } };

    float closest_distance = INFINITY;
    uint32_t closest_block_id = UINT32_MAX;
    vkm_bvec3 normal = { 0 };
    for (uint32_t i = 0; i < terrain->chunk.count; i++) {
        const nc__terrain_block_t block = terrain->chunk.array[i];
        const vkm_vec3 box_min = { { block.position.x, block.position.y, block.position.z } };
        const vkm_vec3 box_max = { { box_min.x + 1.0f, box_min.y + 1.0f, box_min.z + 1.0f } };

        vkm_vec3 t0;
        vkm_sub(&box_min, camera_position, &t0);
        vkm_mul(&t0, &inverse_ray_direction, &t0);

        vkm_vec3 t1;
        vkm_sub(&box_max, camera_position, &t1);
        vkm_mul(&t1, &inverse_ray_direction, &t1);

        vkm_vec3 enter_distances;
        vkm_min(&t0, &t1, &enter_distances);

        vkm_vec3 exit_distances;
        vkm_max(&t0, &t1, &exit_distances);

        const float enter_distance = vkm_scalar_max(&enter_distances);
        const float exit_distance = vkm_scalar_min(&exit_distances);

        if (exit_distance >= vkm_max(enter_distance, 0.0f)) {
            const float hit_distance = vkm_max(enter_distance, 0.0f);
            if (hit_distance < closest_distance) {
                closest_distance = hit_distance;
                closest_block_id = terrain->chunk.dense[i];
                normal = (vkm_bvec3){ {
                    (int8_t)((enter_distance == enter_distances.x) * (inverse_ray_direction.x < 0.0f ? 1 : -1)),
                    (int8_t)((enter_distance == enter_distances.y) * (inverse_ray_direction.y < 0.0f ? 1 : -1)),
                    (int8_t)((enter_distance == enter_distances.z) * (inverse_ray_direction.z < 0.0f ? 1 : -1)),
                } };
            }
        }
    }

    if (closest_distance == INFINITY) {
        return;
    }

    if (new_block == NC_BLOCK_TYPE_AIR) {
        nc__terrain_block_dense_pool_t_remove(&terrain->chunk, closest_block_id);
    } else if (closest_distance > 1.0f) {
        const vkm_ubvec3 block_position = nc__terrain_block_dense_pool_t_get(&terrain->chunk, closest_block_id).position;
        nc__terrain_block_dense_pool_t_append(&terrain->chunk, (nc__terrain_block_t){
            .position = { {
                block_position.x + normal.x,
                block_position.y + normal.y,
                block_position.z + normal.z,
            } },
            .type = new_block,
        });
    }

    terrain->render_dirty = true;
}

void nc_terrain_fini(nc_terrain_t* terrain, nc_renderer_t* renderer) {
    if (!terrain) {
        return;
    }

    nc_renderer_destroy_buffer(renderer, terrain->instance_buffer);
    nc_renderer_destroy_texture(renderer, terrain->texture_array);
    nc__terrain_block_dense_pool_t_fini(&terrain->chunk);

    free(terrain);
}
