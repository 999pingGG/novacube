#pragma once
#ifndef NOVACUBE_MESHER_H_
#define NOVACUBE_MESHER_H_

#include <stdbool.h>
#include <stdint.h>

// TODO: Some of those constants and macros don't belong to the mesher.
#define NC_MESHER_CHUNK_SIZE 16
#define NC_MESHER_PADDED_CHUNK_SIZE (NC_MESHER_CHUNK_SIZE + 2)
#define NC_MESHER_BLOCKS_PER_CHUNK (NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE)
#define NC_MESHER_CHUNK_COORDS_TO_INDEX(x, y, z) \
        (((x) & 15) + ((y) * NC_MESHER_CHUNK_SIZE) + ((z) * NC_MESHER_CHUNK_SIZE * NC_MESHER_CHUNK_SIZE))

#define NC_MESHER_BLOCK_MODEL_LENGTH 8
#define NC_MESHER_BLOCK_MODEL
#define NC_MESHER_VOXELS_PER_BLOCK_MODEL \
        (NC_MESHER_BLOCK_MODEL_LENGTH * NC_MESHER_BLOCK_MODEL_LENGTH * NC_MESHER_BLOCK_MODEL_LENGTH)
#define NC_MESHER_BLOCK_MODEL_SIZE (NC_MESHER_VOXELS_PER_BLOCK_MODEL / 8)
#define NC_MESHER_INTS_PER_BLOCK_MODEL ((NC_MESHER_VOXELS_PER_BLOCK_MODEL + 15) / 16)
#define NC_MESHER_BLOCK_MODEL_COORDS_TO_INDEX(x, y, z) \
        ((x) + ((y) * NC_MESHER_BLOCK_MODEL_LENGTH) + ((z) * NC_MESHER_BLOCK_MODEL_LENGTH * NC_MESHER_BLOCK_MODEL_LENGTH))

typedef struct nc_greedy_quad_t {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
} nc_greedy_quad_t;

// 8 bytes, encoded in an SSBO as 2 uints. As used by chunk.vert
// Note: Currently, we can spare 6 bits in total to represent anything extra we want.
typedef struct nc_mesh_quad_t {
    nc_greedy_quad_t quad;      // 4 bytes: x, y, width, height.
    uint8_t plane;              // Position along the axis perpendicular to the face.
    uint8_t direction;          // One of the possible 6 directions to face: +x, -x, +y, -y, +z, -z.
                                // We can opt into only using the lower 3 bits to get 5 extra bits.
    uint16_t face_data_offset;  // Offset to the quad's data array.
                                // There will be, in the absolute worst case scenario (drawing 16 * 16 * 16 * 6 faces),
                                // a max of 24576 faces. Representable with 15 bits.
                                // So we can get an extra bit here if ever needed.
} nc_mesh_quad_t;

#define TDS_DECLARE
#define TDS_VALUE_T nc_mesh_quad_t
#define TDS_TYPE nc_mesh_quad_vec
#include <tds/vector.h>

// 4 bytes. Matches the SSBO representation used by chunk.frag
typedef struct nc_mesh_face_data_t {
    uint16_t texture_layer;     // Bits 0-10: texture array layer (values 0-2047)
                                // Max 2048 layers was picked to support at least most entry-level mobile GPUs
                                // per Vulkan limits.
    uint8_t ambient_occlusion;  // 2 bits for every face corner. 4 different values. A total of 8 bits per face.
    uint8_t unused;             // Reserved for future biome blending, lighting, or something.
} nc_mesh_face_data_t;

#define TDS_DECLARE
#define TDS_VALUE_T nc_mesh_face_data_t
#define TDS_TYPE nc_mesh_face_data_vec
#include <tds/vector.h>

typedef struct nc_block_model_t {
    nc_mesh_quad_vec quads;
    uint16_t voxel_model_id;
    int direction_offsets[6];           // The offsets into the vector at which the quads' direction change.
                                        // Will be useful in the future for quickly splitting the block into more than
                                        // one chunk mesh because different faces need different array textures (a
                                        // single chunk mesh needs to use a single array texture).
    bool solid;                         // As an optimization.
    bool full_faces[6];                 // For every direction, whether that face is full, for greedy mesher
                                        // optimization purposes... but currently unused.
} nc_block_model_t;

typedef struct nc_mesher_t nc_mesher_t;
typedef struct nc_block_registry_t nc_block_registry_t;

nc_mesher_t* nc_mesher_init(const nc_block_registry_t* block_registry);
void nc_mesher_compute_chunk(
        nc_mesher_t* mesher,
        const uint16_t* chunk_and_neighbors[3][3][3],
        nc_mesh_quad_vec* quads_result,
        nc_mesh_face_data_vec* face_data_result);
void nc_mesher_compute_block_model(
        const uint16_t voxel_data[NC_MESHER_INTS_PER_BLOCK_MODEL],
        nc_mesh_quad_vec* quads,
        bool full_faces[6],
        int direction_offsets[6]);
// LoD currently not implemented.
// Result will be null-terminated, the array size is just the max size.
// Data will be nuked! Better pass a copy if you need it unmodified.
void nc_mesher_mesh_binary_plane(
        uint16_t data[NC_MESHER_CHUNK_SIZE],
        int lod_size,
        nc_greedy_quad_t result[128],
        int* count);
bool nc_mesher_export_obj(const char* filename, const nc_mesh_quad_vec* quads);
void nc_mesher_fini(nc_mesher_t* mesher);

#endif
