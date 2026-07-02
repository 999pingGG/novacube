#version 450
layout(early_fragment_tests) in;

layout(std140, set = 3, binding = 0) uniform block_highlight_fragment_uniforms {
    vec4 color;
    float time;
} uniforms;

layout(location = 0) in vec2 in_face_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 warped_uv = abs(in_face_uv - vec2(0.5));

    float closest_to_border = max(warped_uv.x, warped_uv.y);
    out_color = uniforms.color;
    out_color.a *= float(closest_to_border > 0.48);

    float something = closest_to_border == warped_uv.x ? in_face_uv.y : in_face_uv.x;
    out_color.a *= float(mod(something + uniforms.time * 0.5, 0.2) < 0.1);
}
