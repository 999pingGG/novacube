#version 450
layout(early_fragment_tests) in;
#extension GL_EXT_scalar_block_layout : require

struct block_highlight_uniforms {
    mat4 view_projection;
    vec4 block_position_and_scale;
    vec4 color;
    float time;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer block_highlight_uniform_buffer {
    block_highlight_uniforms values[];
} frame_data;

layout(push_constant) uniform push_constants {
    uint uniforms;
} pc;

layout(location = 0) in vec2 in_face_uv;

layout(location = 0) out vec4 out_color;

void main() {
    block_highlight_uniforms uniforms = frame_data.values[pc.uniforms];
    vec2 uv = in_face_uv;
    uv *= 1.0 - uv.xy;
    float alpha = sqrt(sqrt(uv.x * uv.y * 15.0)) * -1.0 + 1.0;
    alpha += abs(sin(uniforms.time * 3.0)) * 0.5;

    out_color = uniforms.color;
    out_color.a *= alpha;
}
