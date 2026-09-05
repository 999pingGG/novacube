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

#define NC__MESHER_BLOCK_LIGHT_SHIFT 0
#define NC__MESHER_SKY_LIGHT_SHIFT 4

static_assert(sizeof(nc_mesh_quad_t) == 8, "Packed chunk allocation requires 8-byte quads");
static_assert(sizeof(nc_mesh_face_data_t) == 8, "Packed chunk allocation requires 8-byte face data");

static bool nc__chunk_block_is_full_for_pass(
    const nc_block_registry_t* block_registry,
    const uint16_t block_type,
    const bool transparent
) {
    const nc_block_t* block = nc_block_registry_get(block_registry, (nc_block_type_t)block_type);
    if (transparent != !!(block->flags & NC_BLOCK_FLAG_TRANSPARENT)) {
        return false;
    }

    const nc_block_model_t* model = nc_block_registry_get_model(block_registry, (nc_block_type_t)block_type);
    return (model->flags & NC_BLOCK_MODEL_FLAGS_FULL_BIT) != 0;
}

static void nc__chunk_add_voxel_to_axis_columns(
    const int x,
    const int y,
    const int z,
    uint32_t axis_columns[3][NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE]
) {
    axis_columns[0][z][x] |= 1 << y;
    axis_columns[1][y][z] |= 1 << x;
    axis_columns[2][y][x] |= 1 << z;
}

static uint16_t nc__chunk_get_block(const uint16_t* chunk_and_neighbors[3][3][3], int x, int y, int z) {
    x += NC_CHUNK_SIZE;
    y += NC_CHUNK_SIZE;
    z += NC_CHUNK_SIZE;

    const int x_chunk = x / NC_CHUNK_SIZE;
    const int y_chunk = y / NC_CHUNK_SIZE;
    const int z_chunk = z / NC_CHUNK_SIZE;

    x %= NC_CHUNK_SIZE;
    y %= NC_CHUNK_SIZE;
    z %= NC_CHUNK_SIZE;

    const uint16_t* chunk_data = chunk_and_neighbors[x_chunk][y_chunk][z_chunk];
    if (!chunk_data) {
        return 0;
    }

    return chunk_data[NC_CHUNK_COORDS_TO_INDEX(x, y, z)];
}

static uint8_t nc__chunk_get_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    int x,
    int y,
    int z,
    const uint8_t fallback,
    const int shift
) {
    x += NC_CHUNK_SIZE;
    y += NC_CHUNK_SIZE;
    z += NC_CHUNK_SIZE;

    const int x_chunk = x / NC_CHUNK_SIZE;
    const int y_chunk = y / NC_CHUNK_SIZE;
    const int z_chunk = z / NC_CHUNK_SIZE;
    x %= NC_CHUNK_SIZE;
    y %= NC_CHUNK_SIZE;
    z %= NC_CHUNK_SIZE;

    const uint8_t* chunk_data = light_levels_and_neighbors[x_chunk][y_chunk][z_chunk];
    if (!chunk_data) {
        return fallback;
    }

    return chunk_data[NC_CHUNK_COORDS_TO_INDEX(x, y, z)] >> shift & 0xf;
}

static uint8_t nc__chunk_get_face_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const int direction,
    const int x,
    const int y,
    const int z,
    const int shift
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
            0,
            shift);
}

static uint8_t nc__chunk_get_face_plane_light(
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const int direction,
    const int x,
    const int y,
    const int z,
    const int face_x,
    const int face_y,
    const uint8_t fallback,
    const int shift
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

    return nc__chunk_get_light(light_levels_and_neighbors, sample_x, sample_y, sample_z, fallback, shift);
}

static bool nc__chunk_is_solid_from_axis_columns(
    const uint32_t axis_columns[3][NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE],
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
    const uint32_t axis_columns[3][NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE],
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

    int light_sum = center_light;
    int sample_count = 1;
    if (!side_0) {
        light_sum += light_samples[side_0_bit];
        sample_count++;
    }
    if (!side_1) {
        light_sum += light_samples[side_1_bit];
        sample_count++;
    }
    if (!corner && !(side_0 && side_1)) {
        light_sum += light_samples[corner_bit];
        sample_count++;
    }

    return (uint8_t)((light_sum + sample_count / 2) / sample_count);
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
    const int z,
    const int shift
) {
    NC_ASSERT(shift == NC__MESHER_BLOCK_LIGHT_SHIFT || shift == NC__MESHER_SKY_LIGHT_SHIFT);
    const uint8_t center_light = nc__chunk_get_face_light(light_levels_and_neighbors, direction, x, y, z, shift);
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
                center_light,
                shift);
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

// TODO: Can we just omit this function and instead just compute both opaque and transparent meshes at once?
void nc_mesher_compute_workspace(
    const nc_block_registry_t* block_registry,
    const uint16_t* chunk_and_neighbors[3][3][3],
    nc_mesher_workspace_t* workspace
) {
    *workspace = (nc_mesher_workspace_t){ 0 };
    for (int z = 0; z < NC_PADDED_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_PADDED_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_PADDED_CHUNK_SIZE; x++) {
                const uint16_t block_type = nc__chunk_get_block(
                        chunk_and_neighbors,
                        x - 1,
                        y - 1,
                        z - 1);
                const nc_block_t* block = nc_block_registry_get(
                        block_registry,
                        (nc_block_type_t)block_type);
                const bool transparent = (block->flags & NC_BLOCK_FLAG_TRANSPARENT) != 0;
                if (nc__chunk_block_is_full_for_pass(block_registry, block_type, transparent)) {
                    nc__chunk_add_voxel_to_axis_columns(
                            x,
                            y,
                            z,
                            transparent ? workspace->transparent_axis_columns : workspace->opaque_axis_columns);
                }
            }
        }
    }
}

void nc_mesher_compute_chunk(
    const nc_block_registry_t* block_registry,
    const uint16_t* chunk_and_neighbors[3][3][3],
    const uint8_t* light_levels_and_neighbors[3][3][3],
    const nc_mesher_workspace_t* workspace,
    nc_mesh_quad_vec* quads_result,
    nc_mesh_face_data_vec* face_data_result,
    const bool transparent
) {
    const uint32_t (*const axis_columns)[NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE] = transparent
            ? workspace->transparent_axis_columns
            : workspace->opaque_axis_columns;
    const uint32_t (*const opaque_axis_columns)[NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE] =
            workspace->opaque_axis_columns;
    *quads_result = (nc_mesh_quad_vec){ 0 };
    *face_data_result = (nc_mesh_face_data_vec){ 0 };
    // Some capacity that can help reduce the allocations done without wasting too much memory, hopefully.
    nc_mesh_quad_vec_reserve(quads_result, 512);
    nc_mesh_face_data_vec_reserve(face_data_result, 512);

    const uint16_t* chunk = chunk_and_neighbors[1][1][1];
    NC_ASSERT(chunk);

    for (int z = 0, block_index = 0; z < NC_CHUNK_SIZE; z++) {
        for (int y = 0; y < NC_CHUNK_SIZE; y++) {
            for (int x = 0; x < NC_CHUNK_SIZE; x++, block_index++) {
                const nc_block_t* block = nc_block_registry_get(block_registry, (nc_block_type_t)chunk[block_index]);
                if (transparent != !!(block->flags & NC_BLOCK_FLAG_TRANSPARENT)) {
                    continue;
                }

                const nc_block_model_t* model = nc_block_registry_get_model(
                        block_registry,
                        (nc_block_type_t)chunk[block_index]);
                if ((model->flags & NC_BLOCK_MODEL_FLAGS_FULL_BIT) || !model->quads.count) {
                    continue;
                }

                // Non-full block models are directly copied into the result and not greedy-meshed.
                // This is an optimization opportunity to cull hidden faces.
                nc_mesh_quad_t* new_quad = memcpy(
                        nc_mesh_quad_vec_grow(quads_result, model->quads.count),
                        model->quads.array,
                        (size_t)model->quads.count * sizeof(nc_mesh_quad_t));

                for (int direction = 1; direction < 6; direction++) {
                    NC_ASSERT(model->direction_offsets[direction] >= model->direction_offsets[direction - 1]);
                }
                NC_ASSERT(model->direction_offsets[5] >= 0);
                NC_ASSERT((uint32_t)model->direction_offsets[5] == model->quads.count);

                nc_mesh_face_data_t* face_data = nc_mesh_face_data_vec_grow(face_data_result, 6);
                int face_data_offset = (int)(face_data - face_data_result->array);
                int offset = 0;
                for (int direction = 0; direction < 6; direction++, face_data_offset++) {
                    face_data[direction] = (nc_mesh_face_data_t){
                        .texture_layer = block->texture_array_layers[direction],
                    };

                    const int quads_in_direction = model->direction_offsets[direction] - offset;
                    offset = model->direction_offsets[direction];
                    for (int i = 0; i < quads_in_direction; i++) {
                        new_quad->face_data_offset = (uint16_t)face_data_offset;

                        const vkm_ubvec3 position_offset = nc__quad_position_to_block_coords(
                                new_quad->direction,
                                z,
                                x,
                                y);
                        new_quad->quad.x += (uint8_t)(position_offset.x << 3);
                        new_quad->quad.y += (uint8_t)(position_offset.y << 3);
                        new_quad->plane += (uint8_t)(position_offset.z << 3);
                        new_quad++;
                    }
                }
            }
        }
    }

    const uint32_t non_solid_face_data_count = face_data_result->count;

    if (non_solid_face_data_count != 0) {
        nc_mesh_face_data_t* face_data = face_data_result->array;
        uint32_t face_data_index = 0;

        for (int z = 0, block_index = 0; z < NC_CHUNK_SIZE; z++) {
            for (int y = 0; y < NC_CHUNK_SIZE; y++) {
                for (int x = 0; x < NC_CHUNK_SIZE; x++, block_index++) {
                    const nc_block_t* block = nc_block_registry_get(
                            block_registry,
                            (nc_block_type_t)chunk[block_index]);
                    if (transparent != !!(block->flags & NC_BLOCK_FLAG_TRANSPARENT)) {
                        continue;
                    }

                    const nc_block_model_t* model = nc_block_registry_get_model(
                            block_registry,
                            (nc_block_type_t)chunk[block_index]);
                    if (!(model->flags & NC_BLOCK_MODEL_FLAGS_FULL_BIT) && model->quads.count) {
                        const uint8_t light_emission = block->light_emission;
                        for (int direction = 0; direction < 6; direction++) {
                            const uint16_t ao_mask = nc__chunk_build_face_ao_mask(
                                    opaque_axis_columns,
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
                                            z,
                                            NC__MESHER_BLOCK_LIGHT_SHIFT);
                            direction_data->sky_light = nc__chunk_compute_face_light(
                                    light_levels_and_neighbors,
                                    ao_mask,
                                    direction,
                                    x,
                                    y,
                                    z,
                                    NC__MESHER_SKY_LIGHT_SHIFT);
                        }

                        face_data_index += 6;
                    }
                }
            }
        }

        NC_ASSERT(face_data_index == non_solid_face_data_count);
    }

    // face culling
    // the cull mask to perform greedy slicing, based on solids on previous axis_cols
    uint32_t face_masks[6][NC_PADDED_CHUNK_SIZE][NC_PADDED_CHUNK_SIZE] = { 0 };
    for (int axis = 0; axis < 3; axis++) {
        for (int z = 0; z < NC_PADDED_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_PADDED_CHUNK_SIZE; x++) {
                const uint32_t column = axis_columns[axis][z][x];

                face_masks[2 * axis + 0][z][x] = column & ~(column << 1);
                face_masks[2 * axis + 1][z][x] = column & ~(column >> 1);
                if (transparent) {
                    const uint32_t opaque_column = opaque_axis_columns[axis][z][x];
                    // Faces against opaque full blocks can never contribute. Removing them makes the remaining
                    // transparent boundaries safe to render two-sided without coplanar transparent/opaque z-fighting.
                    face_masks[2 * axis + 0][z][x] &= ~(opaque_column << 1);
                    face_masks[2 * axis + 1][z][x] &= ~(opaque_column >> 1);
                }
            }
        }
    }

    uint16_t planes[6][NC_CHUNK_SIZE][NC_CHUNK_SIZE] = { 0 };

    // find faces and build binary planes based on the voxel block+ao etc...
    for (int direction = 0; direction < 6; direction++) {
        for (int z = 0; z < NC_CHUNK_SIZE; z++) {
            for (int x = 0; x < NC_CHUNK_SIZE; x++) {
                // skip padded by adding 1(for x padding) and (z+1) for (z padding)
                uint32_t column = face_masks[direction][z + 1][x + 1];

                // removes the right most padding value, because it's invalid
                column >>= 1;
                // removes the left most padding value, because it's invalid
                column &= ~(1 << NC_CHUNK_SIZE);

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
        for (int plane = 0; plane < NC_CHUNK_SIZE; plane++) {
            nc_greedy_quad_t quads[128] = { 0 };
            int count;
            nc_mesher_mesh_binary_plane(planes[direction][plane], NC_CHUNK_SIZE, quads, &count);
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
                        const int block_index = NC_CHUNK_COORDS_TO_INDEX(
                                block_coords.x,
                                block_coords.y,
                                block_coords.z);
                        const nc_block_t* block = nc_block_registry_get(
                                block_registry,
                                (nc_block_type_t)chunk[block_index]);

                        const uint16_t ao_mask = nc__chunk_build_face_ao_mask(
                                opaque_axis_columns,
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
                                            block_coords.z,
                                            NC__MESHER_BLOCK_LIGHT_SHIFT),
                            .sky_light = nc__chunk_compute_face_light(
                                    light_levels_and_neighbors,
                                    ao_mask,
                                    direction,
                                    block_coords.x,
                                    block_coords.y,
                                    block_coords.z,
                                    NC__MESHER_SKY_LIGHT_SHIFT),
                        };
                    }
                }
            }
        }
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
