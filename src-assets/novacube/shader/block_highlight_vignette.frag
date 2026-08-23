#version 450
layout(early_fragment_tests) in;
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer block_highlight_fragment_uniforms {
    vec4 color;
    float time;
};

layout(push_constant) uniform push_constants {
    layout(offset = 16) block_highlight_fragment_uniforms uniforms;
} pc;

layout(location = 0) in vec2 in_face_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = in_face_uv;
    uv *= 1.0 - uv.xy;
    float alpha = sqrt(sqrt(uv.x * uv.y * 15.0)) * -1.0 + 1.0;
    alpha += abs(sin(pc.uniforms.time * 3.0)) * 0.5;

    out_color = pc.uniforms.color;
    out_color.a *= alpha;
}
