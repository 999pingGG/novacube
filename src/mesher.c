#include <stdint.h>

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

void nc_mesher_mesh_binary_plane(
    uint16_t data[NC_CHUNK_SIZE],
    const int lod_size,
    nc_greedy_quad_t result[128],
    int* count
) {
    int quad_count = 0;

    int debug = 0;
    int prev = 0;

    for (int row = 0; row < NC_CHUNK_SIZE; row++) {
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

	    if (prev == quad_count) {
		    debug++;
	    }
	    prev = quad_count;
            quad_count++;
            column += height;
        }
    }

    if (count) {
        *count = quad_count;
    }
}
