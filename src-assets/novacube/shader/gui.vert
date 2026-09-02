#version 450
#extension GL_EXT_scalar_block_layout : require

struct gui_rectangle {
    vec4 rectangle;
    vec4 color;
    vec4 corner_radii;
    uvec4 border_widths;
    vec4 overlay_color;
    uint character;
};

struct gui_uniforms {
    mat4 transform;
    vec2 gui_to_ndc_scale;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer gui_uniform_buffer {
    gui_uniforms values[];
} frame_data;

layout(set = 2, binding = 0, scalar) restrict readonly buffer gui_rectangle_buffer {
    gui_rectangle values[];
} rectangle_data;

layout(push_constant) uniform push_constants {
    uint uniforms;
    uint rectangles;
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
    gui_uniforms uniforms = frame_data.values[pc.uniforms];
    gui_rectangle rectangle = rectangle_data.values[pc.rectangles + gl_InstanceIndex];
    vec2 corner = corners[gl_VertexIndex];
    vec2 gui_position = rectangle.rectangle.xy + rectangle.rectangle.zw * corner;
    vec2 clip_position = gui_position * uniforms.gui_to_ndc_scale - 1.0;
    gl_Position = uniforms.transform * vec4(clip_position, 0.0, 1.0);

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
        out_uv = vec2(character % 12, character / 12) * vec2(5.0 / 64.0, 8.0 / 64.0)
                + corner * vec2(5.0 / 64.0, 8.0 / 64.0);
    }
}
