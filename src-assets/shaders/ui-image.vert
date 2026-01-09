#version 450

#define DEPTH_FACTOR (2.0 / 65535.0)

layout(std140, set = 1, binding = 0) uniform uniforms {
    // screen_size_factor = 2 / screen_size;
    vec2 screen_size_factor;
} ubo;

struct pd_ui_image_uniforms_t {
    vec4 rectangle;
    vec4 color;
    vec4 border_radiuses;
    vec4 scissor;
    uint draw_order;
};

layout(std140, set = 1, binding = 1) uniform image_uniforms {
    pd_ui_image_uniforms_t uniforms;
} image_ubo;

// mediump is bugged in PowerVR
layout(location = 0) out flat mediump vec4 out_color;
layout(location = 1) out flat mediump vec4 out_rectangle;
layout(location = 2) out flat mediump vec4 out_border_radiuses;
layout(location = 3) out mediump vec2 out_uv;
layout(location = 4) out mediump vec4 out_scissor;

// triangle strip quad
const vec2 uvs[] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0));

void main() {
    vec2 pos = image_ubo.uniforms.rectangle.xy + image_ubo.uniforms.rectangle.zw * uvs[gl_VertexIndex & 3];
    gl_Position = vec4(
        (pos * ubo.screen_size_factor - vec2(1.0)) * vec2(1.0, -1.0),
        float(image_ubo.uniforms.draw_order) * DEPTH_FACTOR,
        1.0);

    // A color of (0, 0, 0, 0) means "untinted" image, so make the tint white to show it unchanged.
    out_color = image_ubo.uniforms.color.r + image_ubo.uniforms.color.g + image_ubo.uniforms.color.b + image_ubo.uniforms.color.a > 0.0 ? image_ubo.uniforms.color : vec4(1.0);
    out_rectangle = image_ubo.uniforms.rectangle;
    // we only need half the size in the fragment shader
    out_rectangle.zw *= 0.5;
    out_border_radiuses = max(image_ubo.uniforms.border_radiuses, vec4(0.000001));

    out_uv = uvs[gl_VertexIndex & 3];
    out_scissor = image_ubo.uniforms.scissor;
}
