#version 450
layout(early_fragment_tests) in;

layout(location = 0) in vec3 in_uv;

layout(set = 2, binding = 0) uniform sampler2DArray terrain_textures;

layout(location = 0) out vec4 out_color;

void main() {
    //out_color = texture(terrain_textures, in_uv);
    out_color = vec4(1.0);
}
