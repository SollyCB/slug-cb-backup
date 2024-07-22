#extension GL_GOOGLE_include_directive : require

#define VERT
#define VERTEX_INPUT
#include "../shader.h.glsl"

// @Test I now have the infrastructure in place to pass the vs_info uniform to
// the depth shader. Maybe this is more efficient than push constants.

layout(push_constant) uniform pc {
    #if SPLIT_SHADOW_MVP
    mat4 m,v,p;
    #else
    mat4 mvp;
    #endif
};

void main() {
    vec4 pos = vec4(in_position, 1);

    #if SPLIT_SHADOW_MVP
        #ifdef SKINNED
        gl_Position = p * v * m * skin_calc() * pos;
        #else
        gl_Position = p * v * m * transforms[gl_InstanceIndex].joints[0] * pos;
        #endif
    #else
        #ifdef SKINNED
        gl_Position = mvp * skin_calc() * pos;
        #else
        gl_Position = mvp * transforms[gl_InstanceIndex].joints[0] * pos;
        #endif
    #endif
}
