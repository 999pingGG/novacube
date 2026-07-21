#include <novacube/block.h>
#include <novacube/cvkm.h>
#include <novacube/terrain_generation.h>

void nc_terrain_generator_generate_chunk(
    const nc_terrain_generator_t* generator,
    const vkm_ivec3* chunk_coords,
    uint16_t blocks[NC_BLOCKS_PER_CHUNK]
) {
    (void)generator->seed;
    for (int index = 0; index < NC_BLOCKS_PER_CHUNK; index++) {
        vkm_ivec3 local_coords;
        nc_chunk_index_to_local_coords((uint16_t)index, &local_coords);
        const int32_t x = nc_chunk_local_coord_to_block_coord(chunk_coords->x, local_coords.x);
        const int32_t y = nc_chunk_local_coord_to_block_coord(chunk_coords->y, local_coords.y);
        const int32_t z = nc_chunk_local_coord_to_block_coord(chunk_coords->z, local_coords.z);
        const int32_t height = (int32_t)(
            (15.0f + vkm_sin((float)z / 3.0f) * 3.0f + (15.0f + vkm_cos((float)x / 3.0f) * 3.0f)) / 2.0f);
        blocks[index] = y >= height
                ? NC_BLOCK_TYPE_AIR
                : y == height - 1
                        ? NC_BLOCK_TYPE_GRASS
                        : y > height - 5 ? NC_BLOCK_TYPE_DIRT : NC_BLOCK_TYPE_STONE;
    }
}
