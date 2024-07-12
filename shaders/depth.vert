#version 460

#extension GL_GOOGLE_include_directive : require

#define VERT
#define VERTEX_INPUT
#include "../shader.h.glsl"

layout(push_constant) uniform pc {
    #if SPLIT_SHADOW_MVP
    mat4 m,v,p;
    #else
    mat4 mvp;
    #endif
};

void main() {
    vec4 pos = vec4(in_position, 1);
    mat4 skin = skin_calc();

    #if SPLIT_SHADOW_MVP
    gl_Position = p * v * m * skin * pos;
    #else
    gl_Position = mvp * skin * pos;
    #endif
}
