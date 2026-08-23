#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const vec2 positions[] = vec2[](
        vec2(-1.0, -1.0),
        vec2(-1.0, 3.0),
        vec2(3.0, -1.0));

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer sky_uniforms {
    mat4 inverse_view_projection;
    vec4 camera_position;
    vec4 gradient_colors[4];
    vec4 gradient_stops;
};

layout(push_constant) uniform push_constants {
    sky_uniforms uniforms;
} pc;

layout(location = 0) out vec3 out_world_direction;

void main() {
    vec2 position = positions[gl_VertexIndex];
    vec4 world_position = pc.uniforms.inverse_view_projection * vec4(position, 1.0, 1.0);
    out_world_direction = world_position.xyz / world_position.w - pc.uniforms.camera_position.xyz;
    gl_Position = vec4(position, 1.0, 1.0);
}
