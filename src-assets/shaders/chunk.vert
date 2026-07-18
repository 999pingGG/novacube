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
    // Used by the fragment shader.
    float sunlight_intensity;
    // XY expands from pixels to NDC; ZW is its CPU-precomputed reciprocal.
    vec4 quad_expansion;
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

const vec2 quad_corners[] = vec2[](
    vec2(0, 0), // min x, min y
    vec2(1, 0), // max x, min y
    vec2(1, 1), // max x, max y
    vec2(0, 1)  // min x, max y
);

const uint right_handed_strip_corner_order[] = uint[](
    3, 0, 2, 1
);

const uint left_handed_strip_corner_order[] = uint[](
    1, 0, 2, 3
);

vec3 face_to_world_coords(
    uint direction,
    float plane,
    vec2 position
) {
    switch (direction) {
        case 0:
            // down
            return vec3(position.x, plane,       position.y);
        case 1:
            // up
            return vec3(position.x, plane + 1.0, position.y);
        case 2:
            // left
            return vec3(plane,       position.y, position.x);
        case 3:
            // right
            return vec3(plane + 1.0, position.y, position.x);
        case 4:
            // back (-z)
            return vec3(position.x, position.y, plane      );
        case 5:
            // front (+z)
            return vec3(position.x, position.y, plane + 1.0);
    }

    // Should be unreachable. Wouldn't it be cool if we could raise a GPU panic here?
    return vec3(0.0);
}

bool uses_left_handed_quad_basis(uint direction) {
    // Directions 0, 2 and 5 have a local x/y basis whose cross product points inward.
    // Use the opposite diagonal for them so every generated triangle is counter-clockwise when viewed from outside.
    return direction == 0 || direction == 2 || direction == 5;
}

vec2 texture_uv_from_face_position(uint direction, vec2 face_position) {
    vec2 result = face_position * INVERSE_MODEL_SIZE;

    if (direction == 0) {
        // Down-facing quads use +z as their local V axis; flip it so the bottom matches the top face.
        result.y = -result.y;
    } else if (direction == 2 || direction == 5) {
        // Left and front quads are the opposite side of their axis pair, so mirror U to match right/back.
        result.x = -result.x;
    }

    if (direction >= 2) {
        // PNG row zero is the top, while side-face positions grow upward from the bottom.
        result.y = -result.y;
    }

    return result;
}

void main() {
    uvec2 data = pc.quads.data[gl_InstanceIndex];

    // Unpack data
    uint greedy_quad = data.x;
    uvec2 quad_position = uvec2(greedy_quad & 255u, greedy_quad >> 8 & 255u);
    uvec2 quad_size_in_model_voxels = uvec2(greedy_quad >> 16 & 255u, greedy_quad >> 24);
    uint plane_direction_and_face_data_offset = data.y;
    uint plane = plane_direction_and_face_data_offset & 255u;
    uint direction = plane_direction_and_face_data_offset >> 8 & 255u;
    face_data_offset = plane_direction_and_face_data_offset >> 16;

    uint corner_index = uses_left_handed_quad_basis(direction)
            ? left_handed_strip_corner_order[gl_VertexIndex]
            : right_handed_strip_corner_order[gl_VertexIndex];
    vec2 vertex_offset = quad_corners[corner_index];
    vec2 quad_size = vec2(quad_size_in_model_voxels);
    vec2 vertex_position = vec2(quad_position) + quad_size * vertex_offset;

    face_data_size = (quad_size_in_model_voxels + uvec2(7)) / uvec2(8);
    face_uv = texture_uv_from_face_position(direction, vertex_position);
    face_data_uv = quad_size * INVERSE_MODEL_SIZE * vertex_offset;

    vec3 translation = face_to_world_coords(direction, float(plane), vertex_position);
    vec4 clip_position = pc.uniforms.view_projection * vec4(translation * INVERSE_MODEL_SIZE + pc.uniforms.position, 1.0);

    // The chunk of code below is an AI-generated fix for the terrain getting plagued by lots of unrasterized pixels,
    // especially on mobiles.
    // See the section "Pixel-sized holes all over the terrain" in `ADDENDUM.md` for further explanation.

    // A voxel face's two axes are always world X/Y/Z, so their clip-space directions are matrix columns. Project both
    // directions at this vertex without another matrix multiply; the common perspective divisor is unnecessary.
    vec3 clip_u = direction >= 2 && direction < 4
            ? pc.uniforms.view_projection[2].xyw
            : pc.uniforms.view_projection[0].xyw;
    vec3 clip_v = direction < 2
            ? pc.uniforms.view_projection[2].xyw
            : pc.uniforms.view_projection[1].xyw;
    vec2 tangent_u = clip_u.xy * clip_position.w - clip_position.xy * clip_u.z;
    vec2 tangent_v = clip_v.xy * clip_position.w - clip_position.xy * clip_v.z;

    // Offset both edges incident to this corner against an axis-aligned screen-space rectangle. Solving in the quad's
    // projected U/V basis guarantees that every edge moves outward; simply moving away from the center does not.
    float projected_area = abs(tangent_u.x * tangent_v.y - tangent_u.y * tangent_v.x);
    float u_edge_expansion = dot(abs(tangent_v.yx), pc.uniforms.quad_expansion.xy);
    float v_edge_expansion = dot(abs(tangent_u.yx), pc.uniforms.quad_expansion.xy);
    vec2 corner_direction = vertex_offset * 2.0 - 1.0;
    vec2 raw_offset =
              tangent_u * (corner_direction.x * u_edge_expansion)
            + tangent_v * (corner_direction.y * v_edge_expansion);

    // Bound the miter by scaling both components together. Independent component clamping can rotate the displacement
    // inward on skewed distant quads. Folding the scale into the area denominator keeps this to one reciprocal.
    float bounded_area = max(
            projected_area,
            max(
                    abs(raw_offset.x) * pc.uniforms.quad_expansion.z,
                    abs(raw_offset.y) * pc.uniforms.quad_expansion.w));
    float valid_area = step(1e-12, projected_area);
    float inverse_bounded_area = valid_area / max(bounded_area, 1e-12);
    clip_position.xy += raw_offset * (clip_position.w * inverse_bounded_area);
    gl_Position = clip_position;
}
