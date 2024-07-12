#version 460

#extension GL_GOOGLE_include_directive : require
#include "../shader.h.glsl"

layout(location = 0) in vec3 pos;

layout(push_constant) uniform pc {
    #if SPLIT_SHADOW_MVP
    mat4 m,v,p;
    #else
    mat4 mvp;
    #endif
};

void main() {
    #if SPLIT_SHADOW_MVP
    gl_Position = p * v * m * vec4(pos, 1);
    #else
    gl_Position = mvp * vec4(pos, 1);
    #endif
}
