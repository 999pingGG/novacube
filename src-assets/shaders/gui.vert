#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct gui_rectangle {
    vec4 rectangle;
    vec4 color;
    vec4 corner_radii;
    uvec4 border_widths;
    vec4 overlay_color;
    uint character;
};

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer gui_uniforms {
    mat4 transform;
    vec2 gui_to_ndc_scale;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer gui_rectangles {
    gui_rectangle values[];
};

layout(push_constant) uniform push_constants {
    gui_uniforms uniforms;
    gui_rectangles rectangles;
} pc;

layout(location = 0) out vec2 out_local_position;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out flat vec2 out_size;
layout(location = 3) out flat vec4 out_color;
layout(location = 4) out flat vec4 out_corner_radii;
layout(location = 5) out flat uvec4 out_border_widths;
layout(location = 6) out flat vec4 out_overlay_color;

const vec2 corners[4] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0));

void main() {
    gui_rectangle rectangle = pc.rectangles.values[gl_InstanceIndex];
    vec2 corner = corners[gl_VertexIndex];
    vec2 gui_position = rectangle.rectangle.xy + rectangle.rectangle.zw * corner;
    vec2 clip_position = gui_position * pc.uniforms.gui_to_ndc_scale - 1.0;
    gl_Position = pc.uniforms.transform * vec4(clip_position, 0.0, 1.0);

    out_local_position = rectangle.rectangle.zw * corner;
    out_size = rectangle.rectangle.zw;
    out_color = rectangle.color;
    out_corner_radii = rectangle.corner_radii;
    out_border_widths = rectangle.border_widths;
    out_overlay_color = rectangle.overlay_color;

    uint character = rectangle.character;
    if (character == 0) {
        out_uv = vec2(2.0);
    } else {
        if (character < 32 || character > 127) {
            character = 127;
        }
        character -= 32;
        out_uv = vec2(character % 12, character / 12) * vec2(5.0 / 64.0, 8.0 / 64.0) +
            corner * vec2(5.0 / 64.0, 8.0 / 64.0);
    }
}
