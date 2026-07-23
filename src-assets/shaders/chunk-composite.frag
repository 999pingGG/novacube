#version 450

const float EPSILON = 0.00001f;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput in_accumulation;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput in_reveal;

layout (location = 0) out vec4 out_color;

float max_rgb_channel(vec3 color) {
    return max(max(abs(color.r), abs(color.g)), abs(color.b));
}

void main() {
    float reveal = subpassLoad(in_reveal).r;

    if (reveal >= 1.0) {
        // Nothing to do here.
        discard;
    }

    // color
    vec4 accumulation = subpassLoad(in_accumulation);

    // prevent overflow
    if (isinf(max_rgb_channel(abs(accumulation.rgb)))) {
        accumulation.rgb = vec3(accumulation.a);
    }

    // prevent floating point precision bug
    vec3 average_color = accumulation.rgb / max(accumulation.a, EPSILON);

    // blend pixels
    out_color = vec4(average_color, 1.0f - reveal);
}
