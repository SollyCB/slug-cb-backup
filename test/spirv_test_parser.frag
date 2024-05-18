#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 7) in vec3 tangent;
layout(location = 10) in vec2 tex_coord_0;

layout(set = 0, binding = 1) uniform sampler2D tex_sampler[2];

layout(set = 3, binding = 1) uniform texture2D sampled_image[4];

layout(set = 2, binding = 3) uniform textureBuffer texel_buffer[4];

layout(set = 1, binding = 1) uniform UBO {
    mat4 model;
    mat4 proj;
    mat4 view;
} ubo[4];

layout(set = 4, binding = 0, r32f) uniform image2D storage_image;

layout(set = 1, binding = 0) buffer SBO {
    mat4 model;
    mat4 proj;
    mat4 view;
} sbo;

layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(texture(tex_sampler[0], tex_coord_0));
}
