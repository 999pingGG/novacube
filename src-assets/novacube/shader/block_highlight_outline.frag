#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

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
    vec2 warped_uv = abs(in_face_uv - vec2(0.5));

    float closest_to_border = max(warped_uv.x, warped_uv.y);
    out_color = pc.uniforms.color;
    out_color.a *= float(closest_to_border > 0.48);

    float something = closest_to_border == warped_uv.x ? in_face_uv.y : in_face_uv.x;
    out_color.a *= float(mod(something + pc.uniforms.time * 0.5, 0.2) < 0.1);
}
