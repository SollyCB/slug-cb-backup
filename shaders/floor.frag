#version 450

#extension GL_GOOGLE_include_directive : require

#define FRAG
#include "../shader.h.glsl"

void main() {
    uint ci = cascade_i();
    vec3 col = vec3(1, 0, 1);

    if (ci == 0) col = vec3(1, 0, 0);
    if (ci == 1) col = vec3(0, 1, 0);
    if (ci == 2) col = vec3(0, 0, 1);

    for(uint i=0; i < dir_light_count; ++i)
        fc = vec4(col * in_shadow(i, ci), 1);
}
