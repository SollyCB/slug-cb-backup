#version 460
#extension GL_EXT_debug_printf : enable

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput hdr_texture;

layout(push_constant) uniform pc {
    vec4 exxx; // exposure, nil, nil, nil
};

layout(location = 0) out vec4 fc;

void main() {
    vec3 hdr = subpassLoad(hdr_texture).rgb;
    fc = vec4(vec3(1) - exp(-hdr * exxx.x), 1);
}
