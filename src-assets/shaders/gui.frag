#version 450

layout(location = 0) in vec2 in_local_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in flat vec2 in_size;
layout(location = 3) in flat vec4 in_color;
layout(location = 4) in flat vec4 in_corner_radii;
layout(location = 5) in flat uvec4 in_border_widths;
layout(location = 6) in flat vec4 in_overlay_color;

layout(set = 0, binding = 0) uniform sampler2D gui_texture;

layout(location = 0) out vec4 out_color;

float rounded_box_distance(vec2 position, vec2 size, vec4 radii) {
    bool right = position.x >= size.x * 0.5;
    bool bottom = position.y >= size.y * 0.5;
    float radius = bottom
        ? (right ? radii.w : radii.z)
        : (right ? radii.y : radii.x);
    radius = min(radius, min(size.x, size.y) * 0.5);
    vec2 center = position - size * 0.5;
    return length(max(abs(center) - size * 0.5 + radius, 0.0)) - radius;
}

void main() {
    if (rounded_box_distance(in_local_position, in_size, in_corner_radii) > 0.0) {
        discard;
    }

    if (any(greaterThan(in_border_widths, uvec4(0)))) {
        vec2 inset = vec2(in_border_widths.x, in_border_widths.z);
        vec2 inner_size = in_size - inset - vec2(in_border_widths.y, in_border_widths.w);
        if (all(greaterThan(inner_size, vec2(0.0)))) {
            vec4 inner_radii = max(
                in_corner_radii - vec4(
                    max(in_border_widths.x, in_border_widths.z),
                    max(in_border_widths.y, in_border_widths.z),
                    max(in_border_widths.x, in_border_widths.w),
                    max(in_border_widths.y, in_border_widths.w)),
                vec4(0.0));
            if (rounded_box_distance(in_local_position - inset, inner_size, inner_radii) <= 0.0) {
                discard;
            }
        }
    }

    vec4 color = in_uv.x > 1.0 ? in_color : texture(gui_texture, in_uv) * in_color;
    color.rgb = mix(color.rgb, in_overlay_color.rgb, in_overlay_color.a);
    out_color = color;
}
