#version 450
#extension GL_EXT_debug_printf : enable

layout(location = 0) in vec2 pos;

void main() {
    gl_Position = vec4(pos, 0, 1);
}
