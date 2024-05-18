#version 450

layout(location = 0) in vec2 coords;
layout(set = 0, binding = 0) uniform sampler2D textures[5];

layout(location = 0) out vec4 frag_color;

void main() {
    vec4 base_color = texture(textures[0], coords);
    frag_color = base_color;
}
