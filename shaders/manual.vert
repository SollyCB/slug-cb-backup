#version 450

#extension GL_EXT_debug_printf : enable

void pmat(mat4 m) {
    debugPrintfEXT("[ %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f]\n",
    m[0][0], m[1][0], m[2][0], m[3][0],
    m[0][1], m[1][1], m[2][1], m[3][1],
    m[0][2], m[1][2], m[2][2], m[3][2],
    m[0][3], m[1][3], m[2][3], m[3][3]);
}

void pv3(vec3 v) {
    debugPrintfEXT("%f, %f, %f\n", v.x, v.y, v.z);
}

void pv4(vec4 v) {
    debugPrintfEXT("%f, %f, %f, %f\n", v.x, v.y, v.z, v.w);
}

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_texcoord;

struct In_Directional_Light {
    vec4 position;
    vec4 color;
    mat4 space;
};

layout(set = 0, binding = 0) uniform Vertex_Info {
    vec4 view_pos;
    vec4 ambient;
    mat4 model;
    mat4 view;
    mat4 proj;

    In_Directional_Light dir_lights[2];

    uvec4 dxxx; // dir light count, null, null, null
} vs_info;

layout(set = 2, binding = 0) uniform Transforms_Ubo {
    mat4 node_trs;
    mat4 node_tbn;
} transforms;

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
    vec3 ls_frag_pos;
};

layout(location = 0) out uint dir_light_count;

layout(location = 1) out struct Fragment_Info {
    vec2 texcoord;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_view_pos;
    vec3 ambient;

    Directional_Light dir_lights[2];
} fs_info;

void main() {
    vec4 world_pos = vs_info.model * transforms.node_trs * vec4(in_position, 1);
    gl_Position = vs_info.proj * vs_info.view * world_pos;

    // pv4(vs_info.view * vs_info.model * transforms.node_trs * vec4(in_position, 1));
    // pmat(vs_info.view * vs_info.model);
    // pv4(vs_info.view * vs_info.model * transforms.node_trs * vec4(in_position, 1));

    fs_info.texcoord = in_texcoord;

    vec3 normal = vec3(vs_info.model * transforms.node_trs * vec4(in_normal, 1));
    vec3 tangent = vec3(vs_info.model * transforms.node_trs * in_tangent);

    mat3 tbn = transpose(mat3(tangent, cross(normal, vec3(tangent)), normal));
    fs_info.tang_frag_pos = tbn * vec3(world_pos);
    fs_info.tang_view_pos = tbn * vec3(vs_info.view_pos);
    fs_info.tang_normal = tbn * normal;

    fs_info.ambient = vec3(vs_info.ambient);
    dir_light_count = vs_info.dxxx.x;

    for(uint i=0; i < vs_info.dxxx.x; ++i) {
        fs_info.dir_lights[i].color = vec3(vs_info.dir_lights[i].color);
        fs_info.dir_lights[i].ts_light_pos = tbn * vec3(vs_info.dir_lights[i].position);
        fs_info.dir_lights[i].ls_frag_pos = vec3(vs_info.dir_lights[i].space * world_pos);
        // pmat(vs_info.dir_lights[i].space);
        // debugPrintfEXT("%f, %f, %f, %f | %f, %f, %f\n", world_pos.x, world_pos.y, world_pos.z, world_pos.w,
        //                fs_info.dir_lights[i].ls_frag_pos.x,
        //                fs_info.dir_lights[i].ls_frag_pos.y,
        //                fs_info.dir_lights[i].ls_frag_pos.z);
    }
}
