#version 450

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage  : require

layout(location = 0) in vec2 tex_coord_0;
layout(location = 1) in vec2 tex_coord_1;

layout(set = 0, binding = 0) uniform sampler2D base_color;
layout(set = 0, binding = 1) uniform sampler2D metallic_roughness;
layout(set = 0, binding = 2) uniform sampler2D normal;
layout(set = 0, binding = 3) uniform sampler2D occlusion;
layout(set = 0, binding = 4) uniform sampler2D emissive;

layout(set = 1, binding = 0) uniform material_uniforms {
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3];
    float alpha_cutoff;
} mat_ubo;

struct morph_reference {
    uint8_t index;
    uint8_t type;
};

const int GLTF_SUPPORTED_MORPH_ATTRIBUTE_COUNT = 8;

struct material_pc {
    uint8_t base_color_texcoord;
    uint8_t metallic_roughness_texcoord;
    uint8_t normal_texcoord;
    uint8_t occlusion_texcoord;
    uint8_t emissive_texcoord;
    uint16_t flags; // gltf_material_flag_bits
};

layout(push_constant) uniform push_constants {
    material_pc mat;
} pc;

layout(location = 0) out vec4 frag_color;

void main() {
    vec2 base_tex_coord;
    if (uint(pc.mat.base_color_texcoord) == 0)
        base_tex_coord = tex_coord_0;
    else if (uint(pc.mat.base_color_texcoord) == 1)
        base_tex_coord = tex_coord_1;
    else
        base_tex_coord = vec2(0,0);
    frag_color = texture(base_color, base_tex_coord);
}
