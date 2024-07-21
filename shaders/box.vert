#version 460
#extension GL_EXT_debug_printf : enable

layout(location = 0) in vec3 pos;

layout(push_constant) uniform pc {
    mat4 space;
    vec4 col;
};

layout(location = 0) out vec4 out_col;

void main() {
    // debugPrintfEXT("%f, %f, %f", pos.x, pos.y, pos.z);
    gl_Position = space * vec4(pos, 1);
    out_col = col;
}

