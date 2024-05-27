#version 450
#extension GL_EXT_debug_printf : enable

layout(location = 0) in vec3 pos;

layout(push_constant) uniform pc {
    mat4 mvp;
};

void main() {
    gl_Position = mvp * vec4(pos, 1);
}
