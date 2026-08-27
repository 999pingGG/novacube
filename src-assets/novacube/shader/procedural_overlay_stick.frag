#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer procedural_overlay_uniforms {
    vec4 rings[2];
    vec4 sticks[2];
    vec4 crosshair;
};

layout(push_constant) uniform push_constants {
    layout(offset = 8) uint element;
    layout(offset = 16) procedural_overlay_uniforms uniforms;
} pc;

layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 srgb) {
    bvec3 use_linear_segment = lessThanEqual(srgb, vec3(0.04045));
    vec3 linear_segment = srgb * (1.0 / 12.92);
    vec3 exponential_segment = pow((srgb + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
    return mix(exponential_segment, linear_segment, use_linear_segment);
}

bool in_stick(vec2 position, vec4 stick) {
    if (stick.z <= 0.0) {
        return false;
    }

    return length(position - stick.xy) <= stick.z;
}

void main() {
    vec2 position = gl_FragCoord.xy;
    if (!in_stick(position, pc.uniforms.sticks[pc.element])) {
        discard;
    }

    out_color = vec4(srgb_to_linear(vec3(0.02, 0.02, 0.025)), 1.0);
}
