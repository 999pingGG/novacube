#version 450
#extension GL_EXT_scalar_block_layout : require

const vec2 positions[] = vec2[](
        vec2(-1.0, -1.0),
        vec2(-1.0, 3.0),
        vec2(3.0, -1.0));

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

layout(location = 0) out vec3 out_world_direction;

void main() {
    sky_uniforms uniforms = frame_data.values[pc.uniforms];
    vec2 position = positions[gl_VertexIndex];
    vec4 world_position = uniforms.inverse_view_projection * vec4(position, 1.0, 1.0);
    out_world_direction = world_position.xyz / world_position.w - uniforms.camera_position.xyz;
    gl_Position = vec4(position, 1.0, 1.0);
}
