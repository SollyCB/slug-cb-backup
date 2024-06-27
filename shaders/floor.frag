#version 450

#extension GL_GOOGLE_include_directive : require

#define FRAG
#include "../shader.h.glsl"

void main() {
    uint ci = cascade_i();
    for(uint i=0; i < dir_light_count; ++i)
        fc = vec4(vec3(1) * in_shadow(i, ci), 0);
}
