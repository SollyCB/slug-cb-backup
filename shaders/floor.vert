#version 450

#extension GL_EXT_debug_printf : enable

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

vec3 points[] = {
    vec3(-100, -3, -100),
    vec3(-100, -3,  100),
    vec3( 100, -3, -100),
    vec3( 100, -3, -100),
    vec3( 100, -3,  100),
    vec3(-100, -3, -100),
};

void main() {
    gl_Position = vs_info.proj * vs_info.view * vec4(points[gl_VertexIndex], 1);
}
