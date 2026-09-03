#include <stdbool.h>
#include <stdint.h>

#include <novacube/intrinsics.h>
#include <novacube/macros.h>
#include <novacube/mesher.h>
#include <novacube/standard_functions.h>

static void nc__block_model_add_voxel_to_axis_columns(
    const int x,
    const int y,
    const int z,
    uint16_t axis_columns[3][NC_MESHER_BLOCK_MODEL_LENGTH][NC_MESHER_BLOCK_MODEL_LENGTH]
) {
    // x,z - y axis
    axis_columns[0][z][x] |= (uint16_t)(1 << y);
    // z,y - x axis
    axis_columns[1][y][z] |= (uint16_t)(1 << x);
    // x,y - z axis
    axis_columns[2][y][x] |= (uint16_t)(1 << z);
}

static bool nc__block_model_get_voxel(
    const uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL],
    const int index
) {
    return (voxel_data[index / NC_CHUNK_SIZE] >> index % NC_CHUNK_SIZE & 1) != 0;
}

// Perform greedy meshing on a binary plane.
static void nc__mesh_binary_plane(
    uint16_t data[NC_MESHER_BLOCK_MODEL_LENGTH],
    const int lod_size,
    nc_greedy_quad_t result[128],
    int* count
) {
    int quad_count = 0;

    for (int row = 0; row < NC_MESHER_BLOCK_MODEL_LENGTH; row++) {
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
                const int next_row_height = data[row + width] >> column & height_as_mask;
                if (next_row_height != height_as_mask) {
                    // can no longer expand horizontally
                    break;
                }

                // nuke the bits we expanded into
                data[row + width] &= (uint16_t)~mask;
                width++;
            }

	    if (quad_count >= 128) {
		    printf("QUAD COUNT!!! %d\n", quad_count);
	    }
            NC_ASSERT(quad_count < 128);
            result[quad_count++] = (nc_greedy_quad_t){
                .x = (uint8_t)row,
                .y = (uint8_t)column,
                .width = (uint8_t)width,
                .height = (uint8_t)height,
            };

            column += height;
        }
    }

    *count = quad_count;
}

// Main function to mesh the block model.
void nc_mesher_compute_block_model(
    const uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL],
    nc_mesh_quad_vec* quads,
    bool full_faces[6],
    int direction_offsets[6]
) {
    // solid binary for each x,y,z axis (3)
    uint16_t axis_columns[3][NC_MESHER_BLOCK_MODEL_LENGTH][NC_MESHER_BLOCK_MODEL_LENGTH] = { 0 };
    // the cull mask to perform greedy slicing, based on solids on previous axis_cols
    uint16_t face_masks[6][NC_MESHER_BLOCK_MODEL_LENGTH][NC_MESHER_BLOCK_MODEL_LENGTH] = { 0 };

    // process voxels and build axis columns
    int index = 0;
    for (int z = 0; z < NC_MESHER_BLOCK_MODEL_LENGTH; z++) {
        for (int y = 0; y < NC_MESHER_BLOCK_MODEL_LENGTH; y++) {
            for (int x = 0; x < NC_MESHER_BLOCK_MODEL_LENGTH; x++) {
                if (nc__block_model_get_voxel(voxel_data, index)) {
                    nc__block_model_add_voxel_to_axis_columns(x, y, z, axis_columns);
                }

                index++;
            }
        }
    }

    // face culling
    for (int axis = 0; axis < 3; axis++) {
        for (int z = 0; z < NC_MESHER_BLOCK_MODEL_LENGTH; z++) {
            for (int x = 0; x < NC_MESHER_BLOCK_MODEL_LENGTH; x++) {
                // set if current is solid, and next is air
                const uint16_t column = axis_columns[axis][z][x];

                // sample descending axis, and set true when air meets solid
                face_masks[2 * axis + 0][z][x] = (uint16_t)(column & ~(column << 1));
                // sample ascending axis, and set true when air meets solid
                face_masks[2 * axis + 1][z][x] = (uint16_t)(column & ~(column >> 1));
            }
        }
    }

    uint16_t planes[6][NC_MESHER_BLOCK_MODEL_LENGTH][NC_MESHER_BLOCK_MODEL_LENGTH] = { 0 };

    // build binary planes for each face direction
    for (int direction = 0; direction < 6; direction++) {
        for (int z = 0; z < NC_MESHER_BLOCK_MODEL_LENGTH; z++) {
            for (int x = 0; x < NC_MESHER_BLOCK_MODEL_LENGTH; x++) {
                uint16_t column = face_masks[direction][z][x];

                while (column != 0) {
                    const int y = nc__u16_trailing_zeroes(column);
                    // clear least significant set bit
                    column &= (uint16_t)(column - 1);

                    planes[direction][y][x] |= (uint16_t)(1 << z);
                }
            }
        }
    }

    int offset = 0;
    *quads = (nc_mesh_quad_vec){ 0 };
    memset(full_faces, false, sizeof(full_faces[0]) * 6);

    // perform greedy meshing
    for (uint8_t direction = 0; direction < 6; direction++) {
        for (uint8_t plane = 0; plane < NC_MESHER_BLOCK_MODEL_LENGTH; plane++) {
            nc_greedy_quad_t greedy_quads[128] = { 0 };
            int count;
            nc__mesh_binary_plane(planes[direction][plane], NC_MESHER_BLOCK_MODEL_LENGTH, greedy_quads, &count);
            NC_ASSERT(count <= (int)NC_COUNTOF(greedy_quads));

            if ((plane == 0 || plane == NC_MESHER_BLOCK_MODEL_LENGTH - 1) && count == 1) {
                full_faces[direction] = true;
            }

            for (int i = 0; i < count; i++, offset++) {
                const nc_greedy_quad_t quad = greedy_quads[i];
                const nc_mesh_quad_t result_quad = {
                    .quad = {
                        .x = quad.x,
                        .y = quad.y,
                        .width = quad.width,
                        .height = quad.height,
                    },
                    .plane = plane,
                    .direction = direction,
                };
                nc_mesh_quad_vec_append(quads, result_quad);
            }
        }

        direction_offsets[direction] = offset;
    }

    nc_mesh_quad_vec_reclaim(quads);
}
