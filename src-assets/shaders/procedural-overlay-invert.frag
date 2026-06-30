#version 450

layout(set = 2, binding = 0) uniform sampler2D crosshair_texture;

layout(std140, set = 3, binding = 0) uniform procedural_overlay_uniforms {
    vec4 rings[2];
    vec4 sticks[2];
    vec4 crosshair;
} uniforms;

layout(location = 0) out vec4 out_color;

bool in_ring(vec2 position, vec4 ring) {
    if (ring.z <= 0.0 || ring.w <= 0.0) {
        return false;
    }

    return abs(length(position - ring.xy) - ring.z) <= ring.w * 0.5;
}

bool in_crosshair(vec2 position) {
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
    vec2 position = gl_FragCoord.xy;
    if (       !in_ring(position, uniforms.rings[0])
            && !in_ring(position, uniforms.rings[1])
            && !in_crosshair(position)) {
        discard;
    }

    out_color = vec4(1.0);
}
