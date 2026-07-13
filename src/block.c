#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rapidhash.h>

#include <novacube/block.h>
#include <novacube/macros.h>
#include <novacube/standard_functions.h>

typedef struct nc__voxel_model_t {
    uint16_t data[NC_MESHER_INTS_PER_BLOCK_MODEL];
} nc__voxel_model_t;

static uint64_t nc__block_hash_voxel_model(const nc__voxel_model_t* model) {
    return rapidhashMicro(model, sizeof(*model));
}

#define TDS_TYPE nc__voxel_model_id_map
#define TDS_KEY_T nc__voxel_model_t
#define TDS_VALUE_T uint16_t
#define TDS_HASH_KEY(key) nc__block_hash_voxel_model(&(key))
#define TDS_KEY_EQUALS(a, b) (memcmp(&(a), &(b), sizeof(a)) == 0)
#include <tds/hashmap.h>

#define TDS_SIZE_T uint16_t
#define TDS_TYPE nc__voxel_model_vec
#define TDS_VALUE_T nc__voxel_model_t
#include <tds/vector.h>

#define TDS_SIZE_T uint16_t
#define TDS_TYPE nc__block_model_vec
#define TDS_VALUE_T nc_block_model_t
#include <tds/vector.h>

typedef struct nc_block_registry_t {
    nc_block_t blocks[NC_BLOCK_TYPE_COUNT + 1];
    nc__voxel_model_id_map voxel_model_ids;
    nc__voxel_model_vec voxel_models;
    nc__block_model_vec block_models;
} nc_block_registry_t;

static bool nc__block_voxel_model_is_solid(const nc__voxel_model_t* model) {
    for (int i = 0; i < NC_MESHER_INTS_PER_BLOCK_MODEL; i++) {
        if (model->data[i] != UINT16_MAX) {
            return false;
        }
    }
    return true;
}

uint16_t nc_block_registry_register_voxel_model(
    nc_block_registry_t* registry,
    const uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL]
) {
    nc__voxel_model_t model = { 0 };
    memcpy(model.data, voxel_data, sizeof(model.data));

    uint16_t* existing = nc__voxel_model_id_map_get(&registry->voxel_model_ids, model);
    if (existing) {
        return *existing;
    }

    NC_ASSERT(registry->block_models.count < UINT16_MAX);
    NC_ASSERT(registry->voxel_models.count == registry->block_models.count);
    const uint16_t id = registry->block_models.count;
    nc_block_model_t greedy_model = { .voxel_model_id = id };
    if (nc__block_voxel_model_is_solid(&model)) {
        greedy_model.solid = true;
        memset(greedy_model.full_faces, true, sizeof(greedy_model.full_faces));
    } else {
        nc_mesher_compute_block_model(
                model.data,
                &greedy_model.quads,
                greedy_model.full_faces,
                greedy_model.direction_offsets);
    }

    nc__voxel_model_vec_append(&registry->voxel_models, model);
    nc__block_model_vec_append(&registry->block_models, greedy_model);
    (void)nc__voxel_model_id_map_set(&registry->voxel_model_ids, model, id);
    return id;
}

static void nc__block_registry_register_blocks(nc_block_registry_t* registry) {
    const uint16_t empty[NC_MESHER_INTS_PER_BLOCK_MODEL] = { 0 };
    uint16_t cube[NC_MESHER_INTS_PER_BLOCK_MODEL];
    memset(cube, 0xff, sizeof(cube));

    const uint16_t empty_id = nc_block_registry_register_voxel_model(registry, empty);
    const uint16_t cube_id = nc_block_registry_register_voxel_model(registry, cube);
    registry->blocks[NC_BLOCK_TYPE_AIR] = (nc_block_t){ .voxel_model_id = empty_id };
    registry->blocks[NC_BLOCK_TYPE_STONE] = (nc_block_t){
        .texture_array_layers = { 0, 0, 0, 0, 0, 0 },
        .voxel_model_id = cube_id,
        .fully_solid = true,
    };
    registry->blocks[NC_BLOCK_TYPE_DIRT] = (nc_block_t){
        .texture_array_layers = { 1, 1, 1, 1, 1, 1 },
        .voxel_model_id = cube_id,
        .fully_solid = true,
    };
    registry->blocks[NC_BLOCK_TYPE_GRASS] = (nc_block_t){
        .texture_array_layers = { 1, 2, 1, 1, 1, 1 },
        .voxel_model_id = cube_id,
        .fully_solid = true,
    };
}

nc_block_registry_t* nc_block_registry_init(void) {
    nc_block_registry_t* registry = calloc(1, sizeof(*registry));
    nc__block_registry_register_blocks(registry);
    return registry;
}

const nc_block_t* nc_block_registry_get(const nc_block_registry_t* registry, const nc_block_type_t type) {
    NC_ASSERT(type <= NC_BLOCK_TYPE_COUNT);
    return &registry->blocks[type];
}

const nc_block_model_t* nc_block_registry_get_model(
    const nc_block_registry_t* registry,
    const nc_block_type_t block_type
) {
    const nc_block_t* block = nc_block_registry_get(registry, block_type);
    NC_ASSERT(block->voxel_model_id < registry->block_models.count);
    return &registry->block_models.array[block->voxel_model_id];
}

void nc_block_registry_fini(nc_block_registry_t* registry) {
    if (!registry) {
        return;
    }
    for (uint16_t i = 0; i < registry->block_models.count; i++) {
        nc_mesh_quad_vec_fini(&registry->block_models.array[i].quads);
    }
    nc__voxel_model_id_map_fini(&registry->voxel_model_ids);
    nc__voxel_model_vec_fini(&registry->voxel_models);
    nc__block_model_vec_fini(&registry->block_models);
    free(registry);
}
