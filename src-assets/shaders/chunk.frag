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
    uvec2 faces[];
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

uint unpack_corner_light(uint light, int corner) {
    return light >> (corner * 4) & 0xfu;
}

void main() {
    // Interpolation and rasterization precision can put fragments on the far edge a hair beyond the quad. Clamp the
    // discrete lookup so those fragments cannot read past the face-data array through the device address.
    uvec2 face_data_coord = min(uvec2(floor(face_data_uv)), face_data_size - uvec2(1));
    uint face_index = face_data_coord.y * face_data_size.x + face_data_coord.x;
    uvec2 packed_data = pc.face_data.faces[face_data_offset + face_index];

    // unpack data
    uint texture_array_layer = packed_data.x & 0x7ffu;
    uint ambient_occlusion = packed_data.x >> 16 & 0xffu;
    uint block_light = packed_data.y & 0xffffu;
    uint sky_light = packed_data.y >> 16;

    uint corner0_ao = ambient_occlusion >> 0 & 0x3u;
    uint corner1_ao = ambient_occlusion >> 2 & 0x3u;
    uint corner2_ao = ambient_occlusion >> 4 & 0x3u;
    uint corner3_ao = ambient_occlusion >> 6 & 0x3u;

    vec2 face_cell_uv = fract(face_data_uv);
    float corner0_illumination = light_level_intensity(max(
            unpack_corner_light(block_light, 0),
            unpack_corner_light(sky_light, 0)))
            * ambient_occlusion_intensity(corner0_ao);
    float corner1_illumination = light_level_intensity(max(
            unpack_corner_light(block_light, 1),
            unpack_corner_light(sky_light, 1)))
            * ambient_occlusion_intensity(corner1_ao);
    float corner2_illumination = light_level_intensity(max(
            unpack_corner_light(block_light, 2),
            unpack_corner_light(sky_light, 2)))
            * ambient_occlusion_intensity(corner2_ao);
    float corner3_illumination = light_level_intensity(max(
            unpack_corner_light(block_light, 3),
            unpack_corner_light(sky_light, 3)))
            * ambient_occlusion_intensity(corner3_ao);
    float illumination_bottom = mix(corner0_illumination, corner1_illumination, face_cell_uv.x);
    float illumination_top = mix(corner3_illumination, corner2_illumination, face_cell_uv.x);
    float illumination = mix(illumination_bottom, illumination_top, face_cell_uv.y);

    out_color = texture(terrain_textures, vec3(face_uv, texture_array_layer)) * vec4(vec3(illumination), 1.0);
}
