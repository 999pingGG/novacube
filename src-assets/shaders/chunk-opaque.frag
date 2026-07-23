#version 450

#include "chunk-common.inc.frag"

layout(location = 0) out vec4 out_color;

void main () {
    out_color = compute_color();
}
