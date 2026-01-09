// based on https://www.shadertoy.com/view/WtdSDs
// it has antialiasing by smoothstep, but we will be doing MSAA instead
#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) in flat vec4 in_color;
layout(location = 1) in flat vec4 in_rectangle;
layout(location = 2) in flat vec4 in_border_radiuses;
layout(location = 3) in flat uvec4 in_border_thicknesses;
layout(location = 4) in mediump vec2 in_uv;
layout(location = 5) in flat mediump ivec4 in_scissor;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D font_texture;

float rounded_box_sdf(vec2 center, vec2 size, float radius) {
    return length(max(abs(center) - size + radius, 0.0)) - radius;
}

void main() {
    // perform scissor test
    if (
        in_scissor.x > 0
        || in_scissor.y > 0
        || in_scissor.z > 0
        || in_scissor.w > 0
    ) {
        vec2 max_position = in_scissor.xy + in_scissor.zw;
        if (
            gl_FragCoord.x < in_scissor.x
            || gl_FragCoord.x > max_position.x
            || gl_FragCoord.y < in_scissor.y
            || gl_FragCoord.y > max_position.y
        ) {
            discard;
        }
    }

    // position inside rectangle
    vec2 local_position = gl_FragCoord.xy - in_rectangle.xy;
    vec2 half_size = in_rectangle.zw;

    float top_left = float(local_position.x < half_size.x && local_position.y < half_size.y);
    float top_right = float(local_position.x > half_size.x && local_position.y < half_size.y);
    float bottom_left = float(local_position.x < half_size.x && local_position.y > half_size.y);
    float bottom_right = float(local_position.x > half_size.x && local_position.y > half_size.y);

    float border_radius =
        in_border_radiuses.x * top_left
        + in_border_radiuses.y * top_right
        + in_border_radiuses.z * bottom_left
        + in_border_radiuses.w * bottom_right;

    // cut out round borders
    if (border_radius > 0.0) {
        float distance = rounded_box_sdf(local_position - half_size, half_size, border_radius);
        if (distance > 0.0) {
            discard;
        }
    }

    // cut out the inside area beyond defined border thicknesses
    if (
        in_border_thicknesses.x > 0
        || in_border_thicknesses.y > 0
        || in_border_thicknesses.z > 0
        || in_border_thicknesses.w > 0
    ) {
        local_position -= vec2(
            in_border_thicknesses.x * float(local_position.x < half_size.x)
            - in_border_thicknesses.y * float(local_position.x > half_size.x),
            in_border_thicknesses.z * float(local_position.y < half_size.y)
            - in_border_thicknesses.w * float(local_position.y > half_size.y));

        float distance = rounded_box_sdf(local_position - half_size, half_size, border_radius);
        if (distance <= 0.0) {
            discard;
        }
    }

    if (in_uv.x > 1.1) {
        // not rendering a character
        out_color = in_color;
    } else {
        out_color = texture(font_texture, in_uv) * in_color;
    }
}
