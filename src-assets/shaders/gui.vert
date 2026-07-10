#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer gui_uniforms {
    mat4 transform;
    vec2 scale;
    vec2 translate;
};

layout(push_constant) uniform push_constants {
    gui_uniforms uniforms;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

void main() {
    gl_Position = pc.uniforms.transform * vec4(in_position * pc.uniforms.scale + pc.uniforms.translate, 0.0, 1.0);
    out_uv = in_uv;
    out_color = in_color;
}
