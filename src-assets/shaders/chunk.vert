#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer quad_buffer {
    // This data should be nc_mesh_quad_t instead of uvec2,
    // but this works around a driver bug I've encountered in Adreno 610.
    uvec2 data[];
};

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer chunk_uniforms {
    mat4 view_projection;
    vec3 position;
};

layout(push_constant) uniform push_constants {
    quad_buffer quads;
    chunk_uniforms uniforms;
} pc;

// Do not use `mediump` here, it causes severe rendering corruption in PowerVR Rogue GPUs.
layout(location = 0) flat out uint face_data_offset;
layout(location = 1) out vec2 face_uv;
layout(location = 2) out vec2 face_data_uv;
layout(location = 3) flat out uvec2 face_data_size;

#define MODEL_SIZE 8.0
#define INVERSE_MODEL_SIZE (1.0 / MODEL_SIZE)

const uvec2 quad_corners[] = uvec2[](
    uvec2(0, 0), // min x, min y
    uvec2(1, 0), // max x, min y
    uvec2(1, 1), // max x, max y
    uvec2(0, 1)  // min x, max y
);

const uint right_handed_corner_order[] = uint[](
    0, 2, 3,
    0, 1, 2
);

const uint left_handed_corner_order[] = uint[](
    0, 2, 1,
    0, 3, 2
);

uvec3 world_to_sample_coords(
    uint direction,
    uint plane,
    uvec2 position
) {
    switch (direction) {
        case 0:
            // down
            return uvec3(position.x, plane,      position.y);
        case 1:
            // up
            return uvec3(position.x, plane + 1,  position.y);
        case 2:
            // left
            return uvec3(plane,      position.y, position.x);
        case 3:
            // right
            return uvec3(plane + 1,  position.y, position.x);
        case 4:
            // back (-z)
            return uvec3(position.x, position.y, plane     );
        case 5:
            // front (+z)
            return uvec3(position.x, position.y, plane + 1 );
    }

    // Should be unreachable. Wouldn't it be cool if we could raise a GPU panic here?
    return uvec3(0);
}

bool uses_left_handed_quad_basis(uint direction) {
    // Directions 0, 2 and 5 have a local x/y basis whose cross product points inward.
    // Use the opposite diagonal for them so every generated triangle is counter-clockwise when viewed from outside.
    return direction == 0 || direction == 2 || direction == 5;
}

vec2 texture_uv_from_face_position(uint direction, uvec2 face_position) {
    vec2 result = vec2(face_position) * INVERSE_MODEL_SIZE;

    if (direction == 0) {
        // Down-facing quads use +z as their local V axis; flip it so the bottom matches the top face.
        result.y = -result.y;
    } else if (direction == 2 || direction == 5) {
        // Left and front quads are the opposite side of their axis pair, so mirror U to match right/back.
        result.x = -result.x;
    }

    return result;
}

void main() {
    // Can't optimize those due to loss of precision at high vertex indices (about 80000+)...
    int quad_index = gl_VertexIndex / 6;
    int vertex_index_within_quad = gl_VertexIndex % 6;

    uvec2 data = pc.quads.data[quad_index];

    // Unpack data
    uint greedy_quad = data.x;
    uvec2 quad_position = uvec2(greedy_quad & 255u, greedy_quad >> 8 & 255u);
    uvec2 quad_size_in_model_voxels = uvec2(greedy_quad >> 16 & 255u, greedy_quad >> 24);
    uint plane_direction_and_face_data_offset = data.y;
    uint plane = plane_direction_and_face_data_offset & 255u;
    uint direction = plane_direction_and_face_data_offset >> 8 & 255u;
    face_data_offset = plane_direction_and_face_data_offset >> 16;

    uint corner_index = uses_left_handed_quad_basis(direction)
            ? left_handed_corner_order[vertex_index_within_quad]
            : right_handed_corner_order[vertex_index_within_quad];
    uvec2 vertex_offset = quad_corners[corner_index];
    uvec2 vertex_position = quad_position + quad_size_in_model_voxels * vertex_offset;

    face_data_size = (quad_size_in_model_voxels + uvec2(7)) / uvec2(8);
    face_uv = texture_uv_from_face_position(direction, vertex_position);
    face_data_uv = vec2(quad_size_in_model_voxels) * INVERSE_MODEL_SIZE * vec2(vertex_offset);

    uvec3 translation = world_to_sample_coords(direction, plane, vertex_position);

    gl_Position = pc.uniforms.view_projection
            * vec4(vec3(translation) * INVERSE_MODEL_SIZE + pc.uniforms.position, 1.0);
}
