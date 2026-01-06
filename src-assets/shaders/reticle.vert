#version 450

const vec4 reticle_vertices[] = vec4[](
    vec4(-0.01,  0.01, 0.0, 1.0),
    vec4(-0.01, -0.01, 0.0, 1.0),
    vec4( 0.01, -0.01, 0.0, 1.0),

    vec4( 0.01, -0.01, 0.0, 1.0),
    vec4( 0.01,  0.01, 0.0, 1.0),
    vec4(-0.01,  0.01, 0.0, 1.0));

void main() {
    gl_Position = reticle_vertices[gl_VertexIndex];
}
