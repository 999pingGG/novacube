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

vec3 srgb_to_linear(vec3 srgb) {
    bvec3 use_linear_segment = lessThanEqual(srgb, vec3(0.04045));
    vec3 linear_segment = srgb * (1.0 / 12.92);
    vec3 exponential_segment = pow((srgb + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
    return mix(exponential_segment, linear_segment, use_linear_segment);
}

void main() {
    gl_Position = pc.uniforms.transform * vec4(in_position * pc.uniforms.scale + pc.uniforms.translate, 0.0, 1.0);
    out_uv = in_uv;
    out_color = vec4(srgb_to_linear(in_color.rgb), in_color.a);
}
