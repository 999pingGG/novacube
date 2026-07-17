#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

layout(location = 0) flat in uint face_data_offset;
layout(location = 1) in vec2 face_uv;
layout(location = 2) in vec2 face_data_uv;
layout(location = 3) flat in uvec2 face_data_size;

layout(set = 0, binding = 0) uniform sampler2DArray terrain_textures;

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer face_data_array {
    // This should be an array of nc_mesh_face_data_t, but we're working around driver bugs here.
    uint faces[];
};

layout(push_constant) uniform push_constants {
    layout(offset = 16) face_data_array face_data;
} pc;

layout(location = 0) out vec4 out_color;

const float ambient_intensity = 0.01;
const float ambient_occlusion_curve[4] = float[4](1.0, 0.7, 0.45, 0.2);

float ambient_occlusion_intensity(uint occlusion) {
    return ambient_occlusion_curve[occlusion];
}

float light_level_intensity(uint level) {
    float normalized_level = float(level) * (1.0 / 15.0);
    float direct_intensity = pow(normalized_level, 2.2);
    return mix(ambient_intensity, 1.0, direct_intensity);
}

void main() {
    // Interpolation and rasterization precision can put fragments on the far edge a hair beyond the quad. Clamp the
    // discrete lookup so those fragments cannot read past the face-data array through the device address.
    uvec2 face_data_coord = min(uvec2(floor(face_data_uv)), face_data_size - uvec2(1));
    uint face_index = face_data_coord.y * face_data_size.x + face_data_coord.x;
    uint packed_data = pc.face_data.faces[face_data_offset + face_index];

    // unpack data
    uint texture_array_layer = packed_data & 0x7ffu;
    uint ambient_occlusion = packed_data >> 16 & 0xffu;
    uint light = packed_data >> 24;

    uint corner0_ao = ambient_occlusion >> 0 & 0x3u;
    uint corner1_ao = ambient_occlusion >> 2 & 0x3u;
    uint corner2_ao = ambient_occlusion >> 4 & 0x3u;
    uint corner3_ao = ambient_occlusion >> 6 & 0x3u;

    vec2 face_cell_uv = fract(face_data_uv);
    float ao_bottom = mix(
            ambient_occlusion_intensity(corner0_ao),
            ambient_occlusion_intensity(corner1_ao),
            face_cell_uv.x);
    float ao_top = mix(
            ambient_occlusion_intensity(corner3_ao),
            ambient_occlusion_intensity(corner2_ao),
            face_cell_uv.x);
    float ao = mix(ao_bottom, ao_top, face_cell_uv.y);

    uint block_light = light & 0xfu;
    uint sky_light = light >> 4;
    float illumination = light_level_intensity(max(block_light, sky_light)) * ao;

    out_color = texture(terrain_textures, vec3(face_uv, texture_array_layer)) * vec4(vec3(illumination), 1.0);
}
