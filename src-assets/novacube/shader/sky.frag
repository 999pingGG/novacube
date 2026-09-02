#version 450
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

struct sky_uniforms {
    mat4 inverse_view_projection;
    vec4 camera_position;
    vec4 gradient_colors[4];
    vec4 gradient_stops;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer sky_uniform_buffer {
    sky_uniforms values[];
} frame_data;

layout(push_constant) uniform push_constants {
    uint uniforms;
} pc;

layout(location = 0) in vec3 in_world_direction;

layout(location = 0) out vec4 out_color;

void main() {
    sky_uniforms uniforms = frame_data.values[pc.uniforms];
    float elevation = normalize(in_world_direction).y;
    vec4 stops = uniforms.gradient_stops;
    vec4 color = mix(
            uniforms.gradient_colors[0],
            uniforms.gradient_colors[1],
            smoothstep(stops.x, stops.y, elevation));
    color = mix(color, uniforms.gradient_colors[2], smoothstep(stops.y, stops.z, elevation));
    color = mix(color, uniforms.gradient_colors[3], smoothstep(stops.z, stops.w, elevation));
    out_color = color;
}
