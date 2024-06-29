#version 450

#extension GL_GOOGLE_include_directive : require

#define FRAG
#include "../shader.h.glsl"

void main() {
    vec3 ci = cascade_i();

    vec3 cols[] = {
        vec3(1, 0, 1),
        vec3(0, 1, 0),
        vec3(0, 0, 1),
    };

    vec3 col = mix(cols[uint(ci.x)], cols[uint(ci.y)], ci.z);

    for(uint i=0; i < dir_light_count; ++i)
        fc = vec4(col * in_shadow(i), 1);
}
