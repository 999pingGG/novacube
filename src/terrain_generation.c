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

                const int elevation = (int)(vkm_pow(
                        fnlGetNoise2D(
                                &generator->noise_state,
                                (FNLfloat)block_x + 0.5,
                                (FNLfloat)block_z + 0.5),
                        5.0f)
                        * 50.0f);
                blocks[index] = block_y > elevation ? (block_y <= 0 ? NC_BLOCK_TYPE_WATER : NC_BLOCK_TYPE_AIR) : NC_BLOCK_TYPE_STONE;
            }
        }
    }
}
