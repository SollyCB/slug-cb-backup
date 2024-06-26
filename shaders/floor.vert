#version 450

#extension GL_GOOGLE_include_directive : require

#define VERT
#include "../shader.h.glsl"

vec3 points[] = {
    vec3(-100, 0, -100),
    vec3(-100, 0,  100),
    vec3( 100, 0, -100),
    vec3( 100, 0, -100),
    vec3( 100, 0,  100),
    vec3(-100, 0,  100),
};

void main() {
    vec3 in_position = points[gl_VertexIndex];

    vec4 world_pos = vec4(in_position, 1);
    gl_Position = vs_info.proj * vs_info.view * world_pos;

    dir_light_count = vs_info.dxxx.x;
    for(uint i=0; i < dir_light_count; ++i)
        fs_info.dir_lights[0].ls_frag_pos = vs_info.dir_lights[i].space[0] * world_pos; // @TODO space selection
}
