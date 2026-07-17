// Based on https://github.com/TanTanDev/binary_greedy_mesher_demo

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <novacube/block.h>
#include <novacube/cvkm.h>
#include <novacube/intrinsics.h>
#include <novacube/macros.h>
#include <novacube/mesher.h>
#include <novacube/standard_functions.h>

#define TDS_IMPLEMENT
#define TDS_VALUE_T nc_mesh_quad_t
#define TDS_TYPE nc_mesh_quad_vec
#include <tds/vector.h>

#define TDS_IMPLEMENT
#define TDS_VALUE_T nc_mesh_face_data_t
#define TDS_TYPE nc_mesh_face_data_vec
#include <tds/vector.h>

typedef struct nc_mesher_t {
    const nc_block_registry_t* block_registry;
} nc_mesher_t;

nc_mesher_t* nc_mesher_init(const nc_block_registry_t* block_registry) {
    nc_mesher_t* result = malloc(sizeof(*result));
    result->block_registry = block_registry;
    return result;
}

static bool nc__is_solid(const nc_mesher_t* mesher, const uint16_t block_type) {
    return nc_block_registry_get(mesher->block_registry, (nc_block_type_t)block_type)->fully_solid;
}

static const nc_block_model_t* nc__get_block_model(const nc_mesher_t* mesher, const uint16_t block_type) {
    return nc_block_registry_get_model(mesher->block_registry, (nc_block_type_t)block_type);
}

static void nc__chunk_add_voxel_to_axis_columns(
    const int x,
    const int y,
    const int z,
    uint32_t axis_columns[3][NC_MESHER_PADDED_CHUNK_SIZE][NC_MESHER_PADDED_CHUNK_SIZE]
) {
    axis_columns[0][z][x] |= 1 << y;
    axis_columns[1][y][z] |= 1 << x;
    axis_columns[2][y][x] |= 1 << z;
}

static uint16_t nc__chunk_get_block(const uint16_t* chunk_and_neighbors[3][3][3], int x, int y, int z) {
    x += NC_MESHER_CHUNK_SIZE;
    y += NC_MESHER_CHUNK_SIZE;
    z += NC_MESHER_CHUNK_SIZE;

    const int x_chunk = x / NC_MESHER_CHUNK_SIZE;
    const int y_chunk = y / NC_MESHER_CHUNK_SIZE;
    const int z_chunk = z / NC_MESHER_CHUNK_SIZE;

    x %= NC_MESHER_CHUNK_SIZE;
    y %= NC_MESHER_CHUNK_SIZE;
    z %= NC_MESHER_CHUNK_SIZE;

    const uint16_t* chunk_data = chunk_and_neighbors[x_chunk][y_chunk][z_chunk];
    if (!chunk_data) {
        return 0;
    }

    return chunk_data[NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z)];
}

static uint8_t nc__chunk_get_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    int x,
    int y,
    int z,
    const uint8_t fallback
) {
    x += NC_MESHER_CHUNK_SIZE;
    y += NC_MESHER_CHUNK_SIZE;
    z += NC_MESHER_CHUNK_SIZE;

    const int x_chunk = x / NC_MESHER_CHUNK_SIZE;
    const int y_chunk = y / NC_MESHER_CHUNK_SIZE;
    const int z_chunk = z / NC_MESHER_CHUNK_SIZE;
    x %= NC_MESHER_CHUNK_SIZE;
    y %= NC_MESHER_CHUNK_SIZE;
    z %= NC_MESHER_CHUNK_SIZE;

    const uint8_t* chunk_data = light_levels_and_neighbors[x_chunk][y_chunk][z_chunk];
    if (!chunk_data) {
        return fallback;
    }

    return chunk_data[NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z)];
}

static uint8_t nc__chunk_get_face_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const int direction,
    const int x,
    const int y,
    const int z
) {
    static const int offsets[6][3] = {
        {  0, -1,  0 },
        {  0,  1,  0 },
        { -1,  0,  0 },
        {  1,  0,  0 },
        {  0,  0, -1 },
        {  0,  0,  1 },
    };
    NC_ASSERT(direction >= 0 && direction < 6);

    return nc__chunk_get_light(
            light_levels_and_neighbors,
            x + offsets[direction][0],
            y + offsets[direction][1],
            z + offsets[direction][2],
            0);
}

static uint8_t nc__chunk_get_face_plane_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const int direction,
    const int x,
    const int y,
    const int z,
    const int face_x,
    const int face_y,
    const uint8_t fallback
) {
    int sample_x;
    int sample_y;
    int sample_z;

    switch (direction) {
        case 0:
            sample_x = x + face_x;
            sample_y = y - 1;
            sample_z = z + face_y;
            break;
        case 1:
            sample_x = x + face_x;
            sample_y = y + 1;
            sample_z = z + face_y;
            break;
        case 2:
            sample_x = x - 1;
            sample_y = y + face_y;
            sample_z = z + face_x;
            break;
        case 3:
            sample_x = x + 1;
            sample_y = y + face_y;
            sample_z = z + face_x;
            break;
        case 4:
            sample_x = x + face_x;
            sample_y = y + face_y;
            sample_z = z - 1;
            break;
        case 5:
            sample_x = x + face_x;
            sample_y = y + face_y;
            sample_z = z + 1;
            break;
        default:
            NC_ASSERT(false);
            return fallback;
    }

    return nc__chunk_get_light(light_levels_and_neighbors, sample_x, sample_y, sample_z, fallback);
}

static bool nc__chunk_is_solid_from_axis_columns(
    uint32_t axis_columns[3][NC_MESHER_PADDED_CHUNK_SIZE][NC_MESHER_PADDED_CHUNK_SIZE],
    const int x,
    const int y,
    const int z
) {
    return (axis_columns[0][z][x] >> y & 1) != 0;
}

static const int8_t nc__chunk_face_sample_offsets[9][2] = {
    { -1, -1 }, { -1,  0 }, { -1,  1 },
    {  0, -1 }, {  0,  0 }, {  0,  1 },
    {  1, -1 }, {  1,  0 }, {  1,  1 },
};

static uint16_t nc__chunk_build_face_ao_mask(
    uint32_t axis_columns[3][NC_MESHER_PADDED_CHUNK_SIZE][NC_MESHER_PADDED_CHUNK_SIZE],
    const int direction,
    int x,
    int y,
    int z
) {
    uint16_t result = 0;

    // Shift into the padded chunk coordinates so chunk-edge AO can hit neighbour chunks too.
    x++;
    y++;
    z++;

    for (int i = 0; i < (int)NC_COUNTOF(nc__chunk_face_sample_offsets); i++) {
        const int8_t offset_x = nc__chunk_face_sample_offsets[i][0];
        const int8_t offset_y = nc__chunk_face_sample_offsets[i][1];
        int sample_x;
        int sample_y;
        int sample_z;

        switch (direction) {
            case 0:
                // down
                sample_x = x + offset_x;
                sample_y = y - 1;
                sample_z = z + offset_y;
                break;
            case 1:
                // up
                sample_x = x + offset_x;
                sample_y = y + 1;
                sample_z = z + offset_y;
                break;
            case 2:
                // left
                sample_x = x - 1;
                sample_y = y + offset_y;
                sample_z = z + offset_x;
                break;
            case 3:
                // right
                sample_x = x + 1;
                sample_y = y + offset_y;
                sample_z = z + offset_x;
                break;
            case 4:
                // back (-z)
                sample_x = x + offset_x;
                sample_y = y + offset_y;
                sample_z = z - 1;
                break;
            case 5:
                // front (+z)
                sample_x = x + offset_x;
                sample_y = y + offset_y;
                sample_z = z + 1;
                break;
            default:
                NC_ASSERT(false);
                return 0;
        }

        if (nc__chunk_is_solid_from_axis_columns(axis_columns, sample_x, sample_y, sample_z)) {
            result |= (uint16_t)(1 << i);
        }
    }

    return result;
}

static uint8_t nc__chunk_corner_ambient_occlusion(
    const uint16_t ao_mask,
    const int side_0_bit,
    const int corner_bit,
    const int side_1_bit
) {
    const bool side_0 = (ao_mask >> side_0_bit & 1) != 0;
    const bool corner = (ao_mask >> corner_bit & 1) != 0;
    const bool side_1 = (ao_mask >> side_1_bit & 1) != 0;

    // Once both blocks sharing the vertex are solid, the diagonal cannot make that corner any more exposed.
    return side_0 && side_1 ? 3 : (uint8_t)(side_0 + corner + side_1);
}

static uint8_t nc__chunk_pack_face_ambient_occlusion(const uint16_t ao_mask) {
    const uint8_t corner_0 = nc__chunk_corner_ambient_occlusion(ao_mask, 1, 0, 3);
    const uint8_t corner_1 = nc__chunk_corner_ambient_occlusion(ao_mask, 3, 6, 7);
    const uint8_t corner_2 = nc__chunk_corner_ambient_occlusion(ao_mask, 7, 8, 5);
    const uint8_t corner_3 = nc__chunk_corner_ambient_occlusion(ao_mask, 5, 2, 1);

    return (uint8_t)(corner_0 | corner_1 << 2 | corner_2 << 4 | corner_3 << 6);
}

static uint8_t nc__chunk_corner_light(
    const uint8_t light_samples[9],
    const uint16_t ao_mask,
    const int side_0_bit,
    const int corner_bit,
    const int side_1_bit
) {
    const bool side_0 = (ao_mask >> side_0_bit & 1) != 0;
    const bool corner = (ao_mask >> corner_bit & 1) != 0;
    const bool side_1 = (ao_mask >> side_1_bit & 1) != 0;
    const uint8_t center_light = light_samples[4];

    const int light_sum =
            center_light
            + (side_0 ? center_light : light_samples[side_0_bit])
            + (side_1 ? center_light : light_samples[side_1_bit])
            + (corner || (side_0 && side_1) ? center_light : light_samples[corner_bit]);
    return (uint8_t)((light_sum + 2) / 4);
}

static uint16_t nc__chunk_uniform_face_light(const uint8_t light) {
    NC_ASSERT(light <= 15);
    return (uint16_t)(light * 0x1111);
}

static uint16_t nc__chunk_compute_face_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const uint16_t ao_mask,
    const int direction,
    const int x,
    const int y,
    const int z
) {
    const uint8_t center_light = nc__chunk_get_face_light(light_levels_and_neighbors, direction, x, y, z);
    uint8_t light_samples[9];
    for (int i = 0; i < (int)NC_COUNTOF(light_samples); i++) {
        light_samples[i] = nc__chunk_get_face_plane_light(
                light_levels_and_neighbors,
                direction,
                x,
                y,
                z,
                nc__chunk_face_sample_offsets[i][0],
                nc__chunk_face_sample_offsets[i][1],
                center_light);
    }

    const uint8_t corner_0 = nc__chunk_corner_light(light_samples, ao_mask, 1, 0, 3);
    const uint8_t corner_1 = nc__chunk_corner_light(light_samples, ao_mask, 3, 6, 7);
    const uint8_t corner_2 = nc__chunk_corner_light(light_samples, ao_mask, 7, 8, 5);
    const uint8_t corner_3 = nc__chunk_corner_light(light_samples, ao_mask, 5, 2, 1);

    return (uint16_t)(corner_0 | corner_1 << 4 | corner_2 << 8 | corner_3 << 12);
}

static vkm_ubvec3 nc__world_to_sample_coords(const int direction, const int plane, const int x, const int y) {
    switch (direction) {
        case 0:
            // down
            return (vkm_ubvec3){ { (uint8_t)x,           (uint8_t)plane,       (uint8_t)y           } };
        case 1:
            // up
            return (vkm_ubvec3){ { (uint8_t)x,           (uint8_t)(plane + 1), (uint8_t)y           } };
        case 2:
            // left
            return (vkm_ubvec3){ { (uint8_t)plane,       (uint8_t)y,           (uint8_t)x           } };
        case 3:
            // right
            return (vkm_ubvec3){ { (uint8_t)(plane + 1), (uint8_t)y,           (uint8_t)x           } };
        case 4:
            // back (-z)
            return (vkm_ubvec3){ { (uint8_t)x,           (uint8_t)y,           (uint8_t)plane       } };
        case 5:
            // front (+z)
            return (vkm_ubvec3){ { (uint8_t)x,           (uint8_t)y,           (uint8_t)(plane + 1) } };
        default:
            NC_ASSERT(false);
            return CVKM_UBVEC3_ZERO;
    }
}

static vkm_ubvec3 nc__quad_position_to_block_coords(const int direction, const int plane, const int x, const int y) {
    switch (direction) {
        case 0:
        case 1:
            return (vkm_ubvec3){ { (uint8_t)x,     (uint8_t)plane, (uint8_t)y     } };
        case 2:
        case 3:
            return (vkm_ubvec3){ { (uint8_t)plane, (uint8_t)y,     (uint8_t)x     } };
        case 4:
        case 5:
            return (vkm_ubvec3){ { (uint8_t)x,     (uint8_t)y,     (uint8_t)plane } };
        default:
            NC_ASSERT(false);
            return CVKM_UBVEC3_ZERO;
    }
}

void nc_mesher_compute_chunk(
    nc_mesher_t* mesher,
    const uint16_t* chunk_and_neighbors[3][3][3],
    const uint8_t* light_levels_and_neighbors[3][3][3],
    nc_mesh_quad_vec* quads_result,
    nc_mesh_face_data_vec* face_data_result
) {
    // solid binary for each x,y,z axis (3)
    uint32_t axis_columns[3][NC_MESHER_PADDED_CHUNK_SIZE][NC_MESHER_PADDED_CHUNK_SIZE] = { 0 };

    // the cull mask to perform greedy slicing, based on solids on previous axis_cols
    uint32_t face_masks[6][NC_MESHER_PADDED_CHUNK_SIZE][NC_MESHER_PADDED_CHUNK_SIZE] = { 0 };

    *quads_result = (nc_mesh_quad_vec){ 0 };
    // Something that can help reduce the allocations done without wasting too much memory.
    nc_mesh_quad_vec_reserve(quads_result, 512);
    *face_data_result = (nc_mesh_face_data_vec){ 0 };
    nc_mesh_face_data_vec_reserve(face_data_result, 512);

    // inner chunk voxels.
    const uint16_t* chunk = chunk_and_neighbors[1][1][1];
    if (!chunk) {
        return;
    }

    int block_index = 0;
    for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                const nc_block_model_t* model = nc__get_block_model(mesher, chunk[block_index]);

                if (model->solid) {
                    // Solid blocks are greedy-meshed.
                    nc__chunk_add_voxel_to_axis_columns(x + 1, y + 1, z + 1, axis_columns);
                } else if (model->quads.count) {
                    // Non-solid blocks' models are straight up copied into the result and not greedy-meshed.
                    // This is an optimization opportunity to cull hidden faces.

                    // Append the model's quads into the resulting quad list.
                    nc_mesh_quad_t* new_quad = memcpy(
                            nc_mesh_quad_vec_grow(quads_result, model->quads.count),
                            model->quads.array,
                            (size_t)model->quads.count * sizeof(nc_mesh_quad_t));

                    // Some sanity checks.
                    for (int direction = 1; direction < 6; direction++) {
                        NC_ASSERT(model->direction_offsets[direction] >= model->direction_offsets[direction - 1]);
                    }
                    NC_ASSERT(model->direction_offsets[5] >= 0);
                    NC_ASSERT((uint32_t)model->direction_offsets[5] == model->quads.count);

                    // Every "small quad" inside the block model need its own block face data struct instance.
                    // Also, offset all quad positions by the current block position.
                    nc_mesh_face_data_t* face_data = nc_mesh_face_data_vec_grow(face_data_result, 6);
                    int face_data_offset = (int)(face_data - face_data_result->array);
                    int offset = 0;

                    for (int direction = 0; direction < 6; direction++, face_data_offset++) {
                        face_data[direction] = (nc_mesh_face_data_t){
                            .texture_layer = nc_block_registry_get(
                                    mesher->block_registry,
                                    (nc_block_type_t)chunk[block_index])->texture_array_layers[direction],
                        };

                        const int quads_in_direction = model->direction_offsets[direction] - offset;
                        offset = model->direction_offsets[direction];

                        for (int i = 0; i < quads_in_direction; i++) {
                            new_quad->face_data_offset = (uint16_t)face_data_offset;

                            // Offset the quad's position based on the block's position in the chunk.
                            const vkm_ubvec3 position_offset =
                                nc__quad_position_to_block_coords(new_quad->direction, z, x, y);
                            new_quad->quad.x += (uint8_t)(position_offset.x << 3);
                            new_quad->quad.y += (uint8_t)(position_offset.y << 3);
                            new_quad->plane += (uint8_t)(position_offset.z << 3);

                            new_quad++;
                        }
                    }
                }

                block_index++;
            }
        }
    }

    const uint32_t non_solid_face_data_count = face_data_result->count;

    // neighbor chunk voxels.
    // note(leddoo): couldn't be bothered to optimize these.
    //  might be worth it though. together, they take
    //  almost as long as the entire "inner chunk" loop.
    for (int z = 0; z < NC_MESHER_PADDED_CHUNK_SIZE; z += NC_MESHER_CHUNK_SIZE + 1) {
        for (int y = 0; y < NC_MESHER_PADDED_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_PADDED_CHUNK_SIZE; x++) {
                if (nc__is_solid(mesher, nc__chunk_get_block(chunk_and_neighbors, x - 1, y - 1, z - 1))) {
                    nc__chunk_add_voxel_to_axis_columns(x, y, z, axis_columns);
                }
            }
        }
    }

    for (int z = 0; z < NC_MESHER_PADDED_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_PADDED_CHUNK_SIZE; y += NC_MESHER_CHUNK_SIZE + 1) {
            for (int x = 0; x < NC_MESHER_PADDED_CHUNK_SIZE; x++) {
                if (nc__is_solid(mesher, nc__chunk_get_block(chunk_and_neighbors, x - 1, y - 1, z - 1))) {
                    nc__chunk_add_voxel_to_axis_columns(x, y, z, axis_columns);
                }
            }
        }
    }

    for (int z = 0; z < NC_MESHER_PADDED_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_MESHER_PADDED_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_MESHER_PADDED_CHUNK_SIZE; x += NC_MESHER_CHUNK_SIZE + 1) {
                if (nc__is_solid(mesher, nc__chunk_get_block(chunk_and_neighbors, x - 1, y - 1, z - 1))) {
                    nc__chunk_add_voxel_to_axis_columns(x, y, z, axis_columns);
                }
            }
        }
    }

    if (non_solid_face_data_count != 0) {
        nc_mesh_face_data_t* face_data = face_data_result->array;
        uint32_t face_data_index = 0;

        block_index = 0;
        for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
            for (int y = 0; y < NC_MESHER_CHUNK_SIZE; y++) {
                for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                    const nc_block_model_t* model = nc__get_block_model(mesher, chunk[block_index]);
                    if (!model->solid && model->quads.count) {
                        const uint8_t light_emission = nc_block_registry_get(
                                mesher->block_registry,
                                (nc_block_type_t)chunk[block_index])->light_emission;
                        for (int direction = 0; direction < 6; direction++) {
                            const uint16_t ao_mask = nc__chunk_build_face_ao_mask(
                                    axis_columns,
                                    direction,
                                    x,
                                    y,
                                    z);
                            nc_mesh_face_data_t* direction_data = &face_data[face_data_index + direction];
                            direction_data->ambient_occlusion = light_emission
                                    ? 0
                                    : nc__chunk_pack_face_ambient_occlusion(ao_mask);
                            direction_data->block_light = light_emission
                                    ? nc__chunk_uniform_face_light(light_emission)
                                    : nc__chunk_compute_face_light(
                                            light_levels_and_neighbors,
                                            ao_mask,
                                            direction,
                                            x,
                                            y,
                                            z);
                        }

                        face_data_index += 6;
                    }

                    block_index++;
                }
            }
        }

        NC_ASSERT(face_data_index == non_solid_face_data_count);
    }

    // face culling
    for (int axis = 0; axis < 3; axis++) {
        for (int z = 0; z < NC_MESHER_PADDED_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_MESHER_PADDED_CHUNK_SIZE; x++) {
                const uint32_t column = axis_columns[axis][z][x];

                face_masks[2 * axis + 0][z][x] = column & ~(column << 1);
                face_masks[2 * axis + 1][z][x] = column & ~(column >> 1);
            }
        }
    }

    uint16_t planes[6][NC_MESHER_CHUNK_SIZE][NC_MESHER_CHUNK_SIZE] = { 0 };

    // find faces and build binary planes based on the voxel block+ao etc...
    for (int direction = 0; direction < 6; direction++) {
        for (int z = 0; z < NC_MESHER_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_MESHER_CHUNK_SIZE; x++) {
                // skip padded by adding 1(for x padding) and (z+1) for (z padding)
                uint32_t column = face_masks[direction][z + 1][x + 1];

                // removes the right most padding value, because it's invalid
                column >>= 1;
                // removes the left most padding value, because it's invalid
                column &= ~(1 << NC_MESHER_CHUNK_SIZE);

                while (column != 0) {
                    const unsigned y = nc__u32_trailing_zeroes(column);
                    // clear least significant set bit
                    column &= column - 1;

                    planes[direction][y][x] |= (uint16_t)(1 << z);
                }
            }
        }
    }

    for (int direction = 0; direction < 6; direction++) {
        for (int plane = 0; plane < NC_MESHER_CHUNK_SIZE; plane++) {
            nc_greedy_quad_t quads[128] = { 0 };
            int count;
            nc_mesher_mesh_binary_plane(planes[direction][plane], NC_MESHER_CHUNK_SIZE, quads, &count);
            NC_ASSERT(count <= (int)NC_COUNTOF(quads));

            for (int i = 0; i < count; i++) {
                const nc_greedy_quad_t quad = quads[i];
                nc_mesh_quad_t* mesh_quad = nc_mesh_quad_vec_grow(quads_result, 1);

                *mesh_quad = (nc_mesh_quad_t){
                    .quad = {
                        .x = (uint8_t)(quad.x * NC_MESHER_BLOCK_MODEL_LENGTH),
                        .y = (uint8_t)(quad.y * NC_MESHER_BLOCK_MODEL_LENGTH),
                        .width = (uint8_t)(quad.width * NC_MESHER_BLOCK_MODEL_LENGTH),
                        .height = (uint8_t)(quad.height * NC_MESHER_BLOCK_MODEL_LENGTH),
                    },
                    .plane = (uint8_t)(
                            plane * NC_MESHER_BLOCK_MODEL_LENGTH
                            + (NC_MESHER_BLOCK_MODEL_LENGTH - 1) * (direction & 1)),
                    .direction = (uint8_t)direction,
                    .face_data_offset = (uint16_t)face_data_result->count,
                };

                for (int y = 0; y < quad.height; y++) {
                    for (int x = 0; x < quad.width; x++) {
                        const vkm_ubvec3 block_coords =
                            nc__quad_position_to_block_coords(direction, plane, quad.x + x, quad.y + y);
                        block_index = NC_MESHER_CHUNK_COORDS_TO_INDEX(block_coords.x, block_coords.y, block_coords.z);
                        const nc_block_t* block = nc_block_registry_get(
                                mesher->block_registry,
                                (nc_block_type_t)chunk[block_index]);

                        const uint16_t ao_mask = nc__chunk_build_face_ao_mask(
                                axis_columns,
                                direction,
                                block_coords.x,
                                block_coords.y,
                                block_coords.z);
                        nc_mesh_face_data_t* face_data = nc_mesh_face_data_vec_grow(face_data_result, 1);
                        *face_data = (nc_mesh_face_data_t){
                            .texture_layer = block->texture_array_layers[direction],
                            .ambient_occlusion = block->light_emission
                                    ? 0
                                    : nc__chunk_pack_face_ambient_occlusion(ao_mask),
                            .block_light = block->light_emission
                                    ? nc__chunk_uniform_face_light(block->light_emission)
                                    : nc__chunk_compute_face_light(
                                            light_levels_and_neighbors,
                                            ao_mask,
                                            direction,
                                            block_coords.x,
                                            block_coords.y,
                                            block_coords.z),
                        };
                    }
                }
            }
        }
    }
}

void nc_mesher_mesh_binary_plane(
    uint16_t data[NC_MESHER_CHUNK_SIZE],
    const int lod_size,
    nc_greedy_quad_t result[128],
    int* count
) {
    int quad_count = 0;

    for (int row = 0; row < NC_MESHER_CHUNK_SIZE; row++) {
        int column = 0;
        while (column < lod_size) {
            // find first solid, "air/zero's" could be first so skip
            column += nc__u16_trailing_zeroes(data[row] >> column);
            if (column >= lod_size) {
                // reached top
                break;
            }

            const uint16_t height = nc__u16_trailing_ones(data[row] >> column);
            // convert height 'num' to positive bits repeated 'num' times aka:
            // 1 = 0b1, 2 = 0b11, 4 = 0b1111
            const int height_as_mask = (1 << height) - 1;
            const int mask = height_as_mask << column;

            // grow horizontally
            int width = 1;
            while (row + width < lod_size) {
                // fetch bits spanning height, in the next row
                const int next_row_height = (data[row + width] >> column) & height_as_mask;
                if (next_row_height != height_as_mask) {
                    // can no longer expand horizontally
                    break;
                }

                // nuke the bits we expanded into
                data[row + width] &= (uint16_t)~mask;
                width++;
            }

            NC_ASSERT(quad_count < 128);
            result[quad_count] = (nc_greedy_quad_t){
                .x = (uint8_t)row,
                .y = (uint8_t)column,
                .width = (uint8_t)width,
                .height = (uint8_t)height,
            };

            quad_count++;
            column += height;
        }
    }

    if (count) {
        *count = quad_count;
    }
}

bool nc_mesher_export_obj(const char* filename, const nc_mesh_quad_vec* quads) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        return false;
    }

    // Write vertices to the file.
    for (uint32_t i = 0; i < quads->count; i++) {
        static const float colors[4][3] = {
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
        };

        const nc_mesh_quad_t quad = ((const nc_mesh_quad_t*)quads->array)[i];

        vkm_ubvec3 position = nc__world_to_sample_coords(quad.direction, quad.plane, quad.quad.x, quad.quad.y);
        (void)fprintf(file, "v %u %u %u %f %f %f\n", position.x, position.y, position.z,
                colors[0][0], colors[0][1], colors[0][2]);

        position = nc__world_to_sample_coords(
                quad.direction,
                quad.plane,
                quad.quad.x + quad.quad.width,
                quad.quad.y);
        (void)fprintf(file, "v %u %u %u %f %f %f\n", position.x, position.y, position.z,
                colors[1][0], colors[1][1], colors[1][2]);

        position = nc__world_to_sample_coords(
                quad.direction,
                quad.plane,
                quad.quad.x + quad.quad.width,
                quad.quad.y + quad.quad.height);
        (void)fprintf(file, "v %u %u %u %f %f %f\n", position.x, position.y, position.z,
                colors[2][0], colors[2][1], colors[2][2]);

        position = nc__world_to_sample_coords(
                quad.direction,
                quad.plane,
                quad.quad.x,
                quad.quad.y + quad.quad.height);
        (void)fprintf(file, "v %u %u %u %f %f %f\n", position.x, position.y, position.z,
                colors[3][0], colors[3][1], colors[3][2]);
    }

    // Write faces to the file.
    // Note: OBJ file indices start at 1. The vertex list is always A, B, C, D, but the local quad basis is
    // left-handed for directions 0, 2 and 5, so the other directions need the reversed face order for outward normals.
    for (uint32_t i = 0; i < (uint32_t)quads->count; i++) {
        const nc_mesh_quad_t quad = ((const nc_mesh_quad_t*)quads->array)[i];
        if (quad.direction == 0 || quad.direction == 2 || quad.direction == 5) {
            (void)fprintf(file, "f %u %u %u %u\n", i * 4 + 1, i * 4 + 2, i * 4 + 3, i * 4 + 4);
        } else {
            (void)fprintf(file, "f %u %u %u %u\n", i * 4 + 1, i * 4 + 4, i * 4 + 3, i * 4 + 2);
        }
    }

    return fclose(file) == 0;
}

void nc_mesher_fini(nc_mesher_t* mesher) {
    if (!mesher) {
        return;
    }

    free(mesher);
}
