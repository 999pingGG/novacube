#version 450
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

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
    vec2 warped_uv = abs(in_face_uv - vec2(0.5));

    float closest_to_border = max(warped_uv.x, warped_uv.y);
    out_color = uniforms.color;
    out_color.a *= float(closest_to_border > 0.48);

    float something = closest_to_border == warped_uv.x ? in_face_uv.y : in_face_uv.x;
    out_color.a *= float(mod(something + uniforms.time * 0.5, 0.2) < 0.1);
}
