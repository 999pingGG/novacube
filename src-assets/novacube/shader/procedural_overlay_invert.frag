#version 450
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform sampler2D crosshair_texture;

struct procedural_overlay_uniforms {
    vec4 rings[2];
    vec4 sticks[2];
    vec4 crosshair;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer procedural_overlay_uniform_buffer {
    procedural_overlay_uniforms values[];
} frame_data;

layout(push_constant) uniform push_constants {
    uint uniforms;
    uint element;
} pc;

layout(location = 0) out vec4 out_color;

bool in_ring(vec2 position, vec4 ring) {
    if (ring.z <= 0.0 || ring.w <= 0.0) {
        return false;
    }

    return abs(length(position - ring.xy) - ring.z) <= ring.w * 0.5;
}

bool in_crosshair(vec2 position) {
    procedural_overlay_uniforms uniforms = frame_data.values[pc.uniforms];
    if (uniforms.crosshair.z <= 0.0 || uniforms.crosshair.w <= 0.0) {
        return false;
    }

    const float epsilon = 0.0001;
    vec2 uv = (position - uniforms.crosshair.xy) / uniforms.crosshair.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
        return false;
    }

    return texture(crosshair_texture, clamp(uv, vec2(epsilon), vec2(1.0 - epsilon))).a > 0.5;
}

void main() {
    procedural_overlay_uniforms uniforms = frame_data.values[pc.uniforms];
    vec2 position = gl_FragCoord.xy;
    if (pc.element == 2) {
        if (!in_crosshair(position)) {
            discard;
        }
    } else {
        if (!in_ring(position, uniforms.rings[pc.element])) {
            discard;
        }

        // Separate scissored draws must still invert the union of the shapes only once where they overlap.
        if (in_crosshair(position) || (pc.element == 1 && in_ring(position, uniforms.rings[0]))) {
            discard;
        }
    }

    out_color = vec4(1.0);
}
