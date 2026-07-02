#version 450
layout(early_fragment_tests) in;

layout(std140, set = 3, binding = 0) uniform block_highlight_fragment_uniforms {
    vec4 color;
    float time;
} uniforms;

layout(location = 0) in vec2 in_face_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = in_face_uv;
    uv *= 1.0 - uv.xy;
    float alpha = sqrt(sqrt(uv.x * uv.y * 15.0)) * -1.0 + 1.0;
    alpha += abs(sin(uniforms.time * 3.0)) * 0.5;

    out_color = uniforms.color;
    out_color.a *= alpha;
}
