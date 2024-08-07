#extension GL_GOOGLE_include_directive : require

#define VERT
#define VERTEX_INPUT
#include "../shader.h.glsl"

layout(location = 25) out vec3 dbg_norm;
layout(location = 26) out vec4 dbg_tang;

void main() {

    vec4 pos = vec4(in_position, 1);

    #ifdef SKINNED
    mat4 ws = vs_info.model * skin_calc();
    #else
    mat4 ws = vs_info.model * transforms[gl_InstanceIndex].joints[0];
    #endif

    vec4 world_pos = ws * pos;
    vec4 view_pos = vs_info.view * world_pos;

    if (vs_info.dlcs[1] != 0)
        gl_Position = vs_info.dir_lights[0].space[vs_info.dlcs[2]] * world_pos;
    else
        gl_Position = vs_info.proj * view_pos;

    fs_info.texcoord = in_texcoord;

    dbg_norm = in_normal;
    dbg_tang = in_tangent;

    vec3 normal = vec3(ws * vec4(in_normal, 0));
    vec3 tangent = vec3(ws * vec4(in_tangent.xyz, 0));

    mat3 tbn = transpose(mat3(tangent, cross(normal, vec3(tangent)), normal));
    fs_info.tang_frag_pos = tbn * vec3(world_pos);
    fs_info.tang_eye_pos = tbn * vec3(vs_info.eye_pos);
    fs_info.tang_normal = normalize(tbn * normal);

    fs_info.view_frag_pos = view_pos;
    fs_info.world_frag_pos = world_pos;
    dir_light_count = vs_info.dlcs.x;

    for(uint i=0; i < vs_info.dlcs.x; ++i) {
        fs_info.dir_lights[i].color = vec3(vs_info.dir_lights[i].color);
        fs_info.dir_lights[i].ts_light_pos = tbn * vec3(vs_info.dir_lights[i].position);
    }
}
