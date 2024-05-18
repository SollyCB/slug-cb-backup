#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 1) out vec3 out_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec2 in_texcoord_0;
layout(location = 3) out vec2 out_texcoord_0;


layout(set = 0, binding = 0) uniform Vertex_Info {
    vec4 ambient;
    mat4 view;
    mat4 proj;
} vs_info;

layout(set = 2, binding = 0) uniform Transforms_Ubo {
    mat4 node_trs;
    mat4 node_tbn;
} transforms_ubo[1];

layout(location = 4) flat out struct Fragment_Info {
    vec3 eye;
    vec3 frag_pos;
    vec3 ambient;
} fs_info;

void main() {
    vec3 position = in_position;
    vec4 world_pos = transforms_ubo[gl_InstanceIndex].node_trs * vec4(position, 1.0);
    vec4 view_pos = vs_info.view * world_pos;
    gl_Position = vs_info.proj * view_pos;
    gl_Position.y = -gl_Position.y;

    vec3 normal = in_normal;
    out_normal = mat3(transforms_ubo[gl_InstanceIndex].node_tbn) * normal;

    vec3 tangent = in_tangent;
    vec3 ts_tangent = mat3(transforms_ubo[gl_InstanceIndex].node_tbn) * tangent;

    out_texcoord_0 = in_texcoord_0;

    fs_info.eye = tbn * vec3(view_pos);
    fs_info.frag_pos = tbn * vec3(world_pos);
    fs_info.ambient = vs_info.ambient;

}
