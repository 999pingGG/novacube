#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

layout(location = 0) flat in uint face_data_offset;
layout(location = 1) in vec2 face_uv;
layout(location = 2) in vec2 face_data_uv;
layout(location = 3) flat in uvec2 face_data_size;

layout(set = 0, binding = 0) uniform sampler2DArray terrain_textures;

struct chunk_uniforms {
    mat4 view_projection;
    vec3 position;
    // Only this value is used by the fragment shader.
    float sunlight_intensity;
    vec4 quad_expansion;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer chunk_uniform_buffer {
    chunk_uniforms values[];
} frame_data;

layout(set = 2, binding = 0, scalar) restrict readonly buffer chunk_mesh_buffer {
    // This should be an array of nc_mesh_face_data_t, but we're working around driver bugs here.
    uvec2 values[];
} mesh_data;

layout(push_constant) uniform push_constants {
    uint uniforms;
    uint quads;
    uint face_data;
} pc;

const float ambient_intensity = 0.01;
const float ambient_occlusion_curve[4] = float[4](1.0, 0.7, 0.45, 0.2);
// pow(float(level) / 15.0, 2.2)
const float direct_light_curve[16] = float[16](
        0,
        0.00258582559623417,
        0.0118813344348137,
        0.0289911865471078,
        0.0545922772817603,
        0.0891935068622478,
        0.1332085131843,
        0.186988508758844,
        0.2508402364364,
        0.325036962521076,
        0.409825738436323,
        0.505432468828216,
        0.612065599865624,
        0.729918893352071,
        0.859173569658532,
        1);

float ambient_occlusion_intensity(uint occlusion) {
    return ambient_occlusion_curve[occlusion];
}

float direct_light_level_intensity(uint level) {
    return direct_light_curve[level];
}

float combined_light_intensity(uint block_level, uint sky_level) {
    float block_intensity = direct_light_level_intensity(block_level);
    float sky_intensity = direct_light_level_intensity(sky_level)
            * frame_data.values[pc.uniforms].sunlight_intensity;
    return mix(ambient_intensity, 1.0, max(block_intensity, sky_intensity));
}

uint unpack_corner_light(uint light, int corner) {
    return light >> (corner * 4) & 0xfu;
}

vec4 compute_color() {
    // Interpolation and rasterization precision can put fragments on the far edge a hair beyond the quad. Clamp the
    // discrete lookup so those fragments cannot read past the face-data array through the device address.
    uvec2 face_data_coord = min(uvec2(floor(face_data_uv)), face_data_size - uvec2(1));
    uint face_index = face_data_coord.y * face_data_size.x + face_data_coord.x;
    uvec2 packed_data = mesh_data.values[pc.face_data + face_data_offset + face_index];

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
    float corner0_illumination = combined_light_intensity(
            unpack_corner_light(block_light, 0),
            unpack_corner_light(sky_light, 0))
            * ambient_occlusion_intensity(corner0_ao);
    float corner1_illumination = combined_light_intensity(
            unpack_corner_light(block_light, 1),
            unpack_corner_light(sky_light, 1))
            * ambient_occlusion_intensity(corner1_ao);
    float corner2_illumination = combined_light_intensity(
            unpack_corner_light(block_light, 2),
            unpack_corner_light(sky_light, 2))
            * ambient_occlusion_intensity(corner2_ao);
    float corner3_illumination = combined_light_intensity(
            unpack_corner_light(block_light, 3),
            unpack_corner_light(sky_light, 3))
            * ambient_occlusion_intensity(corner3_ao);
    float illumination_bottom = mix(corner0_illumination, corner1_illumination, face_cell_uv.x);
    float illumination_top = mix(corner3_illumination, corner2_illumination, face_cell_uv.x);
    float illumination = mix(illumination_bottom, illumination_top, face_cell_uv.y);

    return texture(terrain_textures, vec3(face_uv, texture_array_layer)) * vec4(vec3(illumination), 1.0);
}
