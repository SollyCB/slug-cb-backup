#version 450

#extension GL_EXT_debug_printf : enable

layout(set = 1, binding = 0) uniform sampler2D shadow_maps[1];

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
    vec3 ls_frag_pos;
};

layout(location = 0) flat in uint dir_light_count;

layout(location = 1) in struct Fragment_Info {
    vec2 texcoord;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_view_pos;
    vec3 ambient;

    Directional_Light dir_lights[2];
} fs_info;

float in_shadow(uint i) {
    vec2 pc = fs_info.dir_lights[i].ls_frag_pos.xy;
    pc = pc * 0.5 + 0.5;

    return (fs_info.dir_lights[i].ls_frag_pos.z > texture(shadow_maps[i], pc).r) ? 1 : 0;
}

layout(location = 0) out vec4 col;

void main() {
    col = vec4(1, 1, 1, 0) * (1 - in_shadow(0));
    col.a = 1;
}
