#pragma once
#ifndef NOVACUBE_BLOCK_H_
#define NOVACUBE_BLOCK_H_

#include <stdbool.h>
#include <stdint.h>

#include <novacube/mesher.h>

typedef uint8_t nc_block_type_t;
enum {
    NC_BLOCK_TYPE_AIR = 0,
    NC_BLOCK_TYPE_STONE,
    NC_BLOCK_TYPE_DIRT,
    NC_BLOCK_TYPE_GRASS,
    NC_BLOCK_TYPE_TORCH,
    NC_BLOCK_TYPE_TEST,

    NC_BLOCK_TYPE_COUNT = NC_BLOCK_TYPE_TEST,
};

typedef uint8_t nc_block_flags_t;
enum {
    NC_BLOCK_FLAG_FULLY_SOLID = 1 << 0,
    NC_BLOCK_FLAG_BLOCKS_LIGHT = 1 << 1,
};

typedef struct nc_block_t {
    uint16_t texture_array_layers[6];
    uint16_t voxel_model_id;
    uint8_t light_emission;             // Value goes from 0-15, we have 4 spare bits here.
    nc_block_flags_t flags;
} nc_block_t;

typedef struct nc_block_registry_t nc_block_registry_t;

nc_block_registry_t* nc_block_registry_init(void);
const nc_block_t* nc_block_registry_get(const nc_block_registry_t* registry, nc_block_type_t type);
const nc_block_model_t* nc_block_registry_get_model(
        const nc_block_registry_t* registry,
        nc_block_type_t block_type);
uint16_t nc_block_registry_register_voxel_model(
        nc_block_registry_t* registry,
        const uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL]);
void nc_block_registry_fini(nc_block_registry_t* registry);

#endif
