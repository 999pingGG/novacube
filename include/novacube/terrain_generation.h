#pragma once
#ifndef NOVACUBE_TERRAIN_GENERATION_H_
#define NOVACUBE_TERRAIN_GENERATION_H_

#include <stdint.h>

#include <novacube/chunk.h>

typedef struct nc_terrain_generator_t {
    uint64_t seed;
} nc_terrain_generator_t;

void nc_terrain_generator_generate_chunk(
        const nc_terrain_generator_t* generator,
        const vkm_ivec3* chunk_coords,
        uint16_t blocks[NC_BLOCKS_PER_CHUNK]);

#endif
