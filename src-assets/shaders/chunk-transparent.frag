#version 450

#include "chunk-common.inc.frag"

layout (location = 0) out vec4 out_accumulation;
layout (location = 1) out float out_reveal;

void main () {
    vec4 color = compute_color();
    // Favor opaque and nearby voxel surfaces without the huge weights that quickly overflow RGBA16F accumulation.
    // The bounded factors keep the total weight in [0.01, 10], while still giving nearby layers useful precedence.
    const float alpha_weight = clamp(color.a * 8.0 + 0.01, 0.01, 1.0);
    const float depth_weight = clamp(1.0 / (0.1 + 8.0 * pow(gl_FragCoord.z, 4.0)), 1.0, 10.0);
    const float weight = alpha_weight * depth_weight;

    out_accumulation = vec4(color.rgb * color.a, color.a) * weight;
    out_reveal = color.a;
}
