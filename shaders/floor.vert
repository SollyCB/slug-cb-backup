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
    vec4 vp = vs_info.view * world_pos;
    gl_Position = vs_info.proj * vp;

    fs_info.cascade_boundaries = vs_info.cascade_boundaries;

    // fs_info.view_frag_pos = vec3(vp);
    fs_info.view_frag_pos = vec3(world_pos);

    dir_light_count = vs_info.dxxx.x;

    for(uint i=0; i < dir_light_count; ++i)
        for(uint j=0; j < SHADOW_CASCADE_COUNT; ++j)
            fs_info.dir_lights[i].ls_frag_pos[j] = vec3(vs_info.dir_lights[i].space[j] * world_pos);
}
