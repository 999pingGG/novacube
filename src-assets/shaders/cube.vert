#version 450

layout(location = 0) in uvec4 in_position_and_block_type;

layout(std140, set = 1, binding = 0) uniform uniforms {
    mat4 view_projection;
    float time;
} ubo;

// Note: mediump is bugged with PowerVR Rogue
layout(location = 0) out vec3 out_uv;

const vec3 cube_vertices[] = vec3[](
    // right face
    vec3(1.0, 1.0, 0.0),
    vec3(1.0, 0.0, 0.0),
    vec3(1.0, 0.0, 1.0),

    vec3(1.0, 0.0, 1.0),
    vec3(1.0, 1.0, 1.0),
    vec3(1.0, 1.0, 0.0),

    // left face
    vec3(0.0, 1.0, 1.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, 0.0),

    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 1.0, 1.0),

    // up face
    vec3(0.0, 1.0, 1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(1.0, 1.0, 0.0),

    vec3(1.0, 1.0, 0.0),
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, 1.0, 1.0),

    // down face
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 1.0),

    vec3(1.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0),

    // back face
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0),

    vec3(1.0, 0.0, 0.0),
    vec3(1.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0),

    // front face
    vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.0, 1.0),
    vec3(0.0, 0.0, 1.0),

    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 1.0, 1.0),
    vec3(1.0, 1.0, 1.0));

const vec2 cube_uvs[] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 0.0));

void main() {

//        gl_Position = ubo.view_projection * vec4(in_position_and_block_type.xyz + cube_vertices[0], 1.0);
    int i = gl_VertexIndex % 3;
    if (i == 0) {
        gl_Position = vec4(-0.5, (-0.9 + (float(gl_InstanceIndex) / 10.0)) * (sin(ubo.time) * 5.0), 0.5, 1.0);
        out_uv = vec3(0.0, 0.0, 0.0);
    } else if (i == 1) {
        gl_Position = vec4(-0.5,  (-0.8 + (float(gl_InstanceIndex) / 10.0)) * (sin(ubo.time) * 5.0), 0.5, 1.0);
        out_uv = vec3(0.0, 1.0, 0.0);
    } else {
        gl_Position = vec4(0.5,   (-0.8  + (float(gl_InstanceIndex) / 10.0)) * (sin(ubo.time) * 5.0), 0.5, 1.0);
        out_uv = vec3(1.0, 1.0, 0.0);
    }
    //gl_Position = ubo.view_projection * vec4(in_position_and_block_type.xyz + cube_vertices[gl_VertexIndex], 1.0);

    out_uv = vec3(cube_uvs[gl_VertexIndex % 6], in_position_and_block_type.w - 1);
}
