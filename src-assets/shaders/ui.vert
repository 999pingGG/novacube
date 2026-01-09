#version 450

#define FONT_TEXTURE_WIDTH 64.0
#define FONT_TEXTURE_HEIGHT 64.0
#define FONT_CHARACTER_WIDTH 5.0
#define FONT_CHARACTER_HEIGHT 8.0
#define CHARACTERS_PER_ROW uint(FONT_TEXTURE_WIDTH / FONT_CHARACTER_WIDTH)
#define CHARACTERS_PER_COLUMN uint(FONT_TEXTURE_HEIGHT / FONT_CHARACTER_HEIGHT)
#define CHARACTER_UV_WIDTH (FONT_CHARACTER_WIDTH / FONT_TEXTURE_WIDTH)
#define CHARACTER_UV_HEIGHT (FONT_CHARACTER_HEIGHT / FONT_TEXTURE_HEIGHT)
#define DEPTH_FACTOR (2.0 / 65535.0)

layout(location = 0) in ivec4 in_rectangle;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_border_radiuses;
layout(location = 3) in uvec4 in_border_thicknesses;
layout(location = 4) in uvec2 in_character;
layout(location = 5) in uvec2 in_draw_order;
layout(location = 6) in ivec4 in_scissor;

layout(std140, set = 1, binding = 0) uniform uniforms {
    // screen_size_factor = 2 / screen_size;
    vec2 screen_size_factor;
} ubo;

// mediump is bugged in PowerVR
layout(location = 0) out flat mediump vec4 out_color;
layout(location = 1) out flat mediump vec4 out_rectangle;
layout(location = 2) out flat mediump vec4 out_border_radiuses;
layout(location = 3) out flat mediump uvec4 out_border_thicknesses;
layout(location = 4) out mediump vec2 out_uv;
layout(location = 5) out flat mediump ivec4 out_scissor;

// triangle strip quad
const vec2 scales[] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0));

void main() {
    vec2 pos = in_rectangle.xy + in_rectangle.zw * scales[gl_VertexIndex & 3];
    gl_Position = vec4(
        (pos * ubo.screen_size_factor - vec2(1.0)) * vec2(1.0, -1.0),
        ((in_draw_order.y << 8) | in_draw_order.x) * DEPTH_FACTOR,
        1.0);

    out_color = in_color;
    out_rectangle = in_rectangle;
    // we only need half the size in the fragment shader
    out_rectangle.zw /= 2;
    //out_rectangle.zw = max(out_rectangle.zw / 2, ivec2(1));
    out_border_radiuses = max(in_border_radiuses, vec4(0.000001));
    out_border_thicknesses = in_border_thicknesses;

    uint character_index = in_character.x;
    if (character_index == 0) {
        // signal we're not rendering a character
        out_uv = vec2(2.0);
    } else {
        if (character_index < 32 || character_index > 127) {
            // render unknown characters as a diamond
            character_index = 127;
        }
        character_index -= 32;

        out_uv =
            vec2(character_index % CHARACTERS_PER_ROW, character_index / CHARACTERS_PER_ROW) * vec2(CHARACTER_UV_WIDTH, CHARACTER_UV_HEIGHT)
            + vec2(CHARACTER_UV_WIDTH, CHARACTER_UV_HEIGHT) * scales[gl_VertexIndex & 3];
    }

    out_scissor = in_scissor;
}
