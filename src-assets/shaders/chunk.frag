#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
layout(early_fragment_tests) in;

layout(location = 0) flat in uint face_data_offset;
layout(location = 1) in vec2 face_uv;
layout(location = 2) in vec2 face_data_uv;
layout(location = 3) flat in uvec2 face_data_size;

struct nc_mesh_face_data_t {
    uint textures;
};

layout(set = 0, binding = 0) uniform sampler2DArray terrain_textures;

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer face_data_array {
    nc_mesh_face_data_t faces[];
};

layout(push_constant) uniform push_constants {
    layout(offset = 16) face_data_array face_data;
} pc;

layout(location = 0) out vec4 out_color;

void main() {
    uvec2 face_data_coord = uvec2(floor(face_data_uv));
    uint face_index = face_data_coord.y * face_data_size.x + face_data_coord.x;
    nc_mesh_face_data_t face = pc.face_data.faces[face_data_offset + face_index];

    // unpack data
    uint texture_array_layer = face.textures & 0x7ff;
    //uint second_texture_array_layer = face.textures >> 16 & 0x7ff;

    out_color = texture(terrain_textures, vec3(face_uv, texture_array_layer));
}
