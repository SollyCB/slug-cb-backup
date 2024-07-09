#version 460

#extension GL_GOOGLE_include_directive : require

#define FRAG
#include "../shader.h.glsl"

void main() {

    #if 1
    uint ci;

    if (fs_info.view_frag_pos.z > vs_info.cascade_boundaries[0])
        ci = 0;
    else if (fs_info.view_frag_pos.z > vs_info.cascade_boundaries[1])
        ci = 1;
    else if (fs_info.view_frag_pos.z > vs_info.cascade_boundaries[2])
        ci = 2;
    else if (fs_info.view_frag_pos.z > vs_info.cascade_boundaries[3])
        ci = 3;
    else
        ci = 4;

    vec3 cols[] = {
        vec3(1, 1, 1) * 1,
        vec3(1, 1, 1) * 0.5,
        vec3(1, 1, 1) * 0.25,
        vec3(1, 1, 1) * 0.1,
        vec3(1, 1, 1) * 0,
    };

    vec3 col = cols[ci]; // mix(cols[uint(ci.x)], cols[uint(ci.y)], ci.z);

    for(uint i=0; i < dir_light_count; ++i)
        fc = vec4(col * in_shadow(i), 1);
    #else

    fc = vec4(0);

    vec3 col = vec3(1, 1, 1);
    for(uint i=0; i < dir_light_count; ++i)
        fc += vec4(col * in_shadow(i), 1);
    #endif
}
