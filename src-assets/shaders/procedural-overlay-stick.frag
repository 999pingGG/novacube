#version 450

layout(std140, set = 3, binding = 0) uniform procedural_overlay_uniforms {
    vec4 rings[2];
    vec4 sticks[2];
    vec4 crosshair;
} uniforms;

layout(location = 0) out vec4 out_color;

bool in_stick(vec2 position, vec4 stick) {
    if (stick.z <= 0.0) {
        return false;
    }

    return length(position - stick.xy) <= stick.z;
}

void main() {
    vec2 position = gl_FragCoord.xy;
    if (       !in_stick(position, uniforms.sticks[0])
            && !in_stick(position, uniforms.sticks[1])) {
        discard;
    }

    out_color = vec4(0.02, 0.02, 0.025, 1.0);
}
