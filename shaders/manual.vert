#version 450

#extension GL_GOOGLE_include_directive : require

#define VERT
#define VERTEX_INPUT
#include "../shader.h.glsl"

layout(location = 3) in vec2 in_texcoord;

void main() {
    
    vec4 world_pos = vs_info.model * transforms.node_trs * vec4(in_position, 1);
    gl_Position = vs_info.proj * vs_info.view * world_pos;

    fs_info.texcoord = in_texcoord;

    vec3 normal = vec3(vs_info.model * transforms.node_trs * vec4(in_normal, 0));
    vec3 tangent = vec3(vs_info.model * transforms.node_trs * vec4(in_tangent.xyz, 0));

    mat3 tbn = transpose(mat3(tangent, cross(normal, vec3(tangent)), normal));
    fs_info.tang_frag_pos = tbn * vec3(world_pos);
    fs_info.tang_view_pos = tbn * vec3(vs_info.view_pos);
    fs_info.tang_normal = normalize(tbn * normal);

    fs_info.ambient = vec3(vs_info.ambient);
    dir_light_count = vs_info.dxxx.x;

    for(uint i=0; i < vs_info.dxxx.x; ++i) {
        fs_info.dir_lights[i].color = vec3(vs_info.dir_lights[i].color);
        fs_info.dir_lights[i].ts_light_pos = tbn * vec3(vs_info.dir_lights[i].position);
        fs_info.dir_lights[i].ls_frag_pos = vs_info.dir_lights[i].space[0] * world_pos; // @TODO CSM - find correct space
    }
}
