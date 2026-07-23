#include <stdbool.h>
#include <stdint.h>

#include <novacube/macros.h>
NC_IGNORE_ALL_WARNINGS_BEGIN
#include <FastNoiseLite.h>
NC_IGNORE_ALL_WARNINGS_END

#include <novacube/block.h>
#include <novacube/cvkm.h>
#include <novacube/terrain_generation.h>

void nc_terrain_generator_generate_chunk(
    const nc_terrain_generator_t* generator,
    const vkm_ivec3* chunk_coords,
    uint16_t blocks[NC_BLOCKS_PER_CHUNK]
) {
    for (int z = 0, index = 0; z < NC_CHUNK_SIZE; z++) {
        const int block_z = nc_chunk_local_coord_to_block_coord(chunk_coords->z, z);
        for (int y = 0; y < NC_CHUNK_SIZE; y++) {
            const int block_y = nc_chunk_local_coord_to_block_coord(chunk_coords->y, y);
            for (int x = 0; x < NC_CHUNK_SIZE; x++, index++) {
                const int block_x = nc_chunk_local_coord_to_block_coord(chunk_coords->x, x);

                float sample = fnlGetNoise2D(
                        &generator->noise_state,
                        (FNLfloat)block_x + 0.5,
                        (FNLfloat)block_z + 0.5);
                const bool negative = sample < 0.0f;
                sample = vkm_pow(sample, 2.0f) * 40.0f;

                if (negative) {
                    sample = -sample;
                }

                if (block_y < 0.0f) {
                    sample *= 0.4f;
                }

                const int elevation = (int)sample;
                const int difference = elevation - block_y;

                nc_block_type_t block_type;
                if (difference < 0) {
                    // above heightmap
                    block_type = block_y <= 0 ? NC_BLOCK_TYPE_WATER : NC_BLOCK_TYPE_AIR;
                } else if (difference <= 2) {
                    // at or below heightmap, max 2 difference
                    if (block_y <= 0) {
                        block_type = NC_BLOCK_TYPE_SAND;
                    } else if (difference == 0) {
                        block_type = NC_BLOCK_TYPE_GRASS;
                    } else {
                        block_type = NC_BLOCK_TYPE_DIRT;
                    }
                } else {
                    // below heightmap, 3+ difference
                    block_type = NC_BLOCK_TYPE_STONE;
                }

                blocks[index] = block_type;
            }
        }
    }
}
