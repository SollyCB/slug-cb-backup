#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 norm;

layout(push_constant) uniform constants {
    uint flags;
} c;

void main() {
    if ((c.flags & 0x01) != 0)
        gl_Position = vec4(norm, 1);
    else
        gl_Position = vec4(pos, 1);
}
