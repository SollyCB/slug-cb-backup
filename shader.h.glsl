#ifndef SHADER_H_GLSL_
#define SHADER_H_GLSL_

#ifndef GL_core_profile
#include "gltf_limits.h"
#endif

#define DIR_LIGHT_COUNT 1
#define CSM_COUNT 4
#define CSM_BLEND_BAND (2)
#define MORPH_WEIGHT_COUNT 1
#define SPLIT_SHADOW_MVP 0
#define SHADER_MAX_MESH_INSTANCE_COUNT 2

#ifdef GL_core_profile
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#endif

#ifndef GL_core_profile // C code invisible to glsl

typedef struct In_Directional_Light In_Directional_Light;
typedef struct Vertex_Info          Vertex_Info;
typedef struct Vertex_Transforms    Vertex_Transforms;
typedef struct Material_Uniforms    Material_Uniforms;

#endif // ifndef GL_core_profile

#if 0 // @Unused for now...
typedef struct point_light {
    vector position;
    vector color;
    matrix space;
    float linear;
    float quadratic;
} In_Point_Light;
#endif

struct In_Directional_Light {
    vec4 position;
    vec4 color;
    mat4 space[CSM_COUNT];
};

struct Vertex_Info {
    vec4 eye_pos;
    vec4 ambient;
    vec4 cascade_boundaries;
    mat4 model;
    mat4 view;
    mat4 proj;

    In_Directional_Light dir_lights[DIR_LIGHT_COUNT];

    uvec4 dlcs; // dir light count, use light view, light view cascade index, shadow map dimensions
};

struct Vertex_Transforms {
    mat4 joints[GLTF_JOINT_COUNT];
    vec4 weights[(MORPH_WEIGHT_COUNT / 4) + 1];
};

// matched to gltf_material_uniforms but defined for shader alignment
struct Material_Uniforms {
    vec4 bbbb; // float base_color[4];
    vec4 mrno; // float metallic_factor; float roughness_factor; float normal_scale; float occlusion_strength;
    vec4 eeea; // float emissive_factor[3]; float alpha_cutoff;
};

#ifndef GL_core_profile

static inline uint vt_ubo_sz(void)
{
    return sizeof(Vertex_Transforms);
}

static inline uint vt_ubo_ofs(bool weights)
{
    return offsetof(Vertex_Transforms, weights) & maxif(weights);
}

#endif

#ifdef GL_core_profile // glsl code invisible to C

#extension GL_EXT_debug_printf : enable

#define PI 3.1415926
#define feq(x, y) (abs(x - y) < 0.000001)

float sq(float x) { return x * x; }
float heaviside(float x) { return x > 0 ? 1 : 0; }

void pmat(mat4 m) {
    debugPrintfEXT("[%f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f]\n",
    m[0][0], m[1][0], m[2][0], m[3][0],
    m[0][1], m[1][1], m[2][1], m[3][1],
    m[0][2], m[1][2], m[2][2], m[3][2],
    m[0][3], m[1][3], m[2][3], m[3][3]);
}

void pv3(vec3 v) {
    debugPrintfEXT("%f, %f, %f\n", v.x, v.y, v.z);
}

void puv4(uvec4 v) {
    debugPrintfEXT("%u, %u, %u, %u\n", v.x, v.y, v.z, v.w);
}

void pv4(vec4 v) {
    debugPrintfEXT("%f, %f, %f, %f\n", v.x, v.y, v.z, v.w);
}

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
};

struct Fragment_Info {
    vec2 texcoord;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_eye_pos;
    vec4 world_frag_pos;
    vec4 view_frag_pos;

    Directional_Light dir_lights[DIR_LIGHT_COUNT];
};

layout(set = 0, binding = 0) uniform UBO_Vertex_Info { Vertex_Info vs_info; };

#ifdef VERT // vertex shader only

#ifdef VERTEX_INPUT
    // @Note @TODO I think that I am creating descriptors for an
    // array of uniform buffers as I was not previously making the
    // member arrayed. So think that if the mesh instance count is
    // > 1 then this will break.
    #ifdef DEPTH
    layout(set = 0, binding = 0) uniform UBO_Transforms { Vertex_Transforms transforms[SHADER_MAX_MESH_INSTANCE_COUNT]; };
    #else
    layout(set = 2, binding = 0) uniform UBO_Transforms { Vertex_Transforms transforms[SHADER_MAX_MESH_INSTANCE_COUNT]; };
    #endif

    #ifdef SKINNED
    layout(location = 0) in vec3  in_position;
    layout(location = 1) in uvec4 in_joints;
    layout(location = 2) in vec4  in_weights;

        #ifndef DEPTH
        layout(location = 3) in vec3 in_normal;
        layout(location = 4) in vec4 in_tangent;
        layout(location = 5) in vec2 in_texcoord;
        #endif

    mat4 skin_calc() {
       return in_weights.x * transforms[gl_InstanceIndex].joints[in_joints.x] +
              in_weights.y * transforms[gl_InstanceIndex].joints[in_joints.y] +
              in_weights.z * transforms[gl_InstanceIndex].joints[in_joints.z] +
              in_weights.w * transforms[gl_InstanceIndex].joints[in_joints.w];
    }
    #else
    layout(location = 0) in vec3 in_position;
        #ifndef DEPTH
        layout(location = 1) in vec3 in_normal;
        layout(location = 2) in vec4 in_tangent;
        layout(location = 3) in vec2 in_texcoord;
        #endif
    #endif
#endif

layout(location = 0) out uint dir_light_count;
layout(location = 1) out Fragment_Info fs_info;

#endif

#ifdef FRAG // fragment shader only

layout(set = 1, binding = 0) uniform sampler2DShadow shadow_maps[DIR_LIGHT_COUNT * CSM_COUNT];
layout(set = 3, binding = 0) uniform UBO_Material_Uniforms { Material_Uniforms material_ubo; };
layout(set = 4, binding = 0) uniform sampler2D material_textures[GLTF_MAX_MATERIAL_TEXTURE_COUNT];

layout(location = 0) out vec4 fc;

layout(location = 0) flat in uint dir_light_count;
layout(location = 1) in Fragment_Info fs_info;

vec3 cascade_i() {
    float fz = fs_info.view_frag_pos.z;
    vec4  d  = vs_info.cascade_boundaries;

    #if 0 // My implementation is cooler
    int  j = 4 - int(dot(vec4(fz > d.x, fz > d.y, fz > d.z, fz > d.w), vec4(1,1,1,1)));
    int  i = max(j - 1, 0);
    vec4 b = vec4(fz-d.x,fz-d.y,fz-d.z,fz-d.w); // positive == before far plane

    float d0 = abs(b[i]);
    float d1 = abs(b[j]);
    uint sdi = i + 1 * int(d1 < d0); // cascade pivot index
    float sd = b[sdi];

    float bf = (sd / CSM_BLEND_BAND) * 0.5 + 0.5;

    return vec3(sdi + 1 * int(bf < 0), min(sdi + 1 * int(bf < 1), CSM_COUNT-1), clamp(1 - bf, 0, 1));
    #else
    int c0 = 4 - int(dot(vec4(fz > d.x, fz > d.y, fz > d.z, fz > d.w), vec4(1,1,1,1)));
    float bf = 1 - (abs(fz - d[c0]) / CSM_BLEND_BAND);

    int c1 = c0 + 1 * int(bf > 0);

    return vec3(c0, min(c1, CSM_COUNT-1), clamp(bf, 0, 1));
    #endif
}

float in_shadow(uint li) {
    vec3 ci = cascade_i();

    // GLSL 4.6 Spec - Section 4.1.7:
    // "Texture (e.g., texture2D), sampler, and samplerShadow types are opaque types, declared and
    // behaving as described above for opaque types. These types are only available when targeting
    // Vulkan. When aggregated into arrays within a shader, these types can only be indexed with a
    // dynamically uniform expression, or texture lookup will result in undefined values."

    uint ca = uint(ci.x);
    uint cb = uint(ci.y);
    float c = ci.z;

    vec3 p = vec3(vs_info.dir_lights[li].space[ca] * fs_info.world_frag_pos);
    vec3 q = vec3(vs_info.dir_lights[li].space[cb] * fs_info.world_frag_pos);
    p.xy = p.xy * 0.5 + 0.5;
    q.xy = q.xy * 0.5 + 0.5;

    #if 1 // Some attempt at further pcf sampling.
    float a = texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + ca]), p);
    float b = texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + cb]), q);

    return mix(a, b, c);
    #else
    vec2 coords[] = {
        vec2(-1, -1),
        vec2( 1, -1),
        vec2(-1,  1),
        vec2( 1,  1),
    };
    float ofs = 1 / vs_info.dlcs[3];

    float a = texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + ca]), p) * 0.2;
    float b = texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + cb]), q) * 0.2;

    for(uint i=0; i < 4; ++i) {
        vec3 j = vec3(p.x + ofs * coords[i].x, p.y + ofs * coords[i].y, p.z);
        vec3 k = vec3(q.x + ofs * coords[i].x, q.y + ofs * coords[i].y, q.z);
        a += texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + ca]), j) * 0.2;
        b += texture(nonuniformEXT(shadow_maps[li * CSM_COUNT + cb]), k) * 0.2;
    }

    return mix(a, b, c);
    #endif
}

void pmatubo() {
    debugPrintfEXT("base_color: %f, %f, %f, %f\nmetallic: %f\nroughness: %f\nnormal: %f\nocclusion: %f\nemissive: %f, %f, %f\nalpha_cutoff: %f\n",
            material_ubo.bbbb.x, material_ubo.bbbb.y, material_ubo.bbbb.z, material_ubo.bbbb.w,
            material_ubo.mrno.x, material_ubo.mrno.y, material_ubo.mrno.z, material_ubo.mrno.w,
            material_ubo.eeea.x, material_ubo.eeea.y, material_ubo.eeea.z, material_ubo.eeea.w);
}

#endif

#endif // ifdef GL_core_profile

#endif // include guard
