#version 450

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage  : require

layout(location = 0)  in vec3 position;
layout(location = 1)  in vec3 normal;
layout(location = 2)  in vec4 tangent;
layout(location = 3)  in vec2 tex_coord_0;
layout(location = 4)  in uvec4 joints_0;
layout(location = 5)  in vec4 weights_0;
layout(location = 6)  in vec3 color_0;
layout(location = 7)  in vec4 color_alpha_0;
layout(location = 8)  in vec2 tex_coord_1;
layout(location = 9)  in uvec4 joints_1;
layout(location = 10) in vec4 weights_1;
layout(location = 11) in vec3 color_1;
layout(location = 12) in vec4 color_alpha_1;

layout(location = 1)  in vec3 position;
layout(location = 2)  in vec3 normal;
layout(location = 3)  in vec4 tangent;
layout(location = 4)  in vec2 tex_coord_0;
layout(location = 5)  in uint tex_coord_0;
layout(location = 6)  in uvec2 joints_0;
layout(location = 7)  in uint joints_0;
layout(location = 8)  in vec4 weights_0;
layout(location = 9)  in uvec2 weights_0;
layout(location = 10) in uint weights_0;
layout(location = 11) in vec3 color_0;
layout(location = 12) in vec4 color_alpha_0;
layout(location = 13) in uvec2 color_0;
layout(location = 14) in uint color_0;

layout(location = 16) in vec3 morph_0;
layout(location = 17) in vec3 morph_1;
layout(location = 18) in vec3 morph_2;
layout(location = 19) in vec3 morph_3;
layout(location = 20) in vec3 morph_4;
layout(location = 21) in vec3 morph_5;
layout(location = 22) in vec3 morph_6;
layout(location = 23) in vec3 morph_7;

layout(location = 24) in vec2 morph_tex;
layout(location = 25) in vec3 morph_color;
layout(location = 26) in vec4 morph_color_alpha;

struct morph_reference {
    uint8_t index;
    uint8_t type;
};

const int GLTF_SUPPORTED_MORPH_ATTRIBUTE_COUNT = 8;

struct morph_pc {
    morph_reference morphs[GLTF_SUPPORTED_MORPH_ATTRIBUTE_COUNT + 3]; // 22 bytes
};

const uint INPUT_TYPE_FLOAT                     = 0x10;
const uint INPUT_TYPE_UNSIGNED_BYTE_NORMALIZED  = 0x20;
const uint INPUT_TYPE_UNSIGNED_SHORT_NORMALIZED = 0x40;
const uint INPUT_COLOR_VEC4                     = 0x80;

struct base_pc {
    uint8_t infos[8]; // index corresponds to input location; high bits are input type, low bits are locations active.
};

// @Todo Need to send the morphed colors etc to the fragment shader.
layout(push_constant) uniform push_constants {
    morph_pc morph;
    base_pc base;
} pc;

layout(location = 0) out vec2 out_tex_coord_0;
layout(location = 1) out vec2 out_tex_coord_1;

void main() {
    gl_Position = vec4(position, 1);
    out_tex_coord_0 = tex_coord_0;
    out_tex_coord_1 = tex_coord_1;
}
