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

    if (vs_info.dlcx[1] != 0)
        gl_Position = vs_info.dir_lights[0].space[vs_info.dlcx[2]] * world_pos;
    else
        gl_Position = vs_info.proj * vp;

    fs_info.view_frag_pos = vp;
    fs_info.world_frag_pos = world_pos;
    dir_light_count = vs_info.dlcx.x;
}
