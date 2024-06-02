#version 450

#extension GL_EXT_debug_printf : enable

void pv3(vec3 v) {
    debugPrintfEXT("[%f, %f, %f]\n", v.x, v.y, v.z);
}

struct In_Directional_Light {
    vec4 position;
    vec4 direction;
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

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
    vec3 ls_frag_pos;
};

layout(location = 0) out uint dir_light_count;

layout(location = 1) out struct Fragment_Info {
    vec2 texcoord;
    vec3 frag_pos;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_view_pos;
    vec3 ambient;

    Directional_Light dir_lights[2];
} fs_info;

vec3 points[] = {
    vec3(-10, 0, -10),
    vec3(-10, 0,  10),
    vec3( 10, 0, -10),
    vec3( 10, 0, -10),
    vec3( 10, 0,  10),
    vec3(-10, 0,  10),
};

void main() {
    vec3 in_position = points[gl_VertexIndex];

    vec4 world_pos = vec4(in_position, 1);
    gl_Position = vs_info.proj * vs_info.view * world_pos;

    fs_info.dir_lights[0].ls_frag_pos = vec3(vs_info.dir_lights[0].space * world_pos);
    // pv3(fs_info.dir_lights[0].ls_frag_pos);
}
