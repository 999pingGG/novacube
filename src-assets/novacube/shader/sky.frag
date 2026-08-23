#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer sky_uniforms {
    mat4 inverse_view_projection;
    vec4 camera_position;
    vec4 gradient_colors[4];
    vec4 gradient_stops;
};

layout(push_constant) uniform push_constants {
    layout(offset = 16) sky_uniforms uniforms;
} pc;

layout(location = 0) in vec3 in_world_direction;

layout(location = 0) out vec4 out_color;

void main() {
    float elevation = normalize(in_world_direction).y;
    vec4 stops = pc.uniforms.gradient_stops;
    vec4 color = mix(
            pc.uniforms.gradient_colors[0],
            pc.uniforms.gradient_colors[1],
            smoothstep(stops.x, stops.y, elevation));
    color = mix(color, pc.uniforms.gradient_colors[2], smoothstep(stops.y, stops.z, elevation));
    color = mix(color, pc.uniforms.gradient_colors[3], smoothstep(stops.z, stops.w, elevation));
    out_color = color;
}
