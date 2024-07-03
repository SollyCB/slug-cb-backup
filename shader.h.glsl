#ifndef SHADER_H_GLSL_
#define SHADER_H_GLSL_

#define DIR_LIGHT_COUNT 2
#define CSM_COUNT 4
#define CSM_BLEND_BAND 10
#define JOINT_COUNT 1
#define MORPH_WEIGHT_COUNT 1
#define SPLIT_SHADOW_MVP 1

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

    uvec4 dxxx; // dir light count, null, null, null
};

struct Vertex_Transforms {
    mat4 trs[JOINT_COUNT];
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
float sq(float x) { return x * x; }
float heaviside(float x) { return x > 0 ? 1 : 0; }

void pmat(mat4 m) {
    debugPrintfEXT("[ %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f]\n",
    m[0][0], m[1][0], m[2][0], m[3][0],
    m[0][1], m[1][1], m[2][1], m[3][1],
    m[0][2], m[1][2], m[2][2], m[3][2],
    m[0][3], m[1][3], m[2][3], m[3][3]);
}

void pv3(vec3 v) {
    debugPrintfEXT("%f, %f, %f\n", v.x, v.y, v.z);
}

void pv4(vec4 v) {
    debugPrintfEXT("%f, %f, %f, %f\n", v.x, v.y, v.z, v.w);
}

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
    vec3 ls_frag_pos[CSM_COUNT];
};

struct Fragment_Info {
    vec2 texcoord;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_eye_pos;
    vec3 view_frag_pos;
    vec3 ambient;
    vec4 cascade_boundaries; // @TODO Make vs_info visible in frag shader to prevent needless copies.

    Directional_Light dir_lights[DIR_LIGHT_COUNT];
};

#ifdef VERT // vertex shader only

#ifdef VERTEX_INPUT
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
#endif

layout(set = 0, binding = 0) uniform UBO_Vertex_Info { Vertex_Info vs_info; };
layout(set = 2, binding = 0) uniform UBO_Transforms { Vertex_Transforms transforms; };

layout(location = 0) out uint dir_light_count;
layout(location = 1) out Fragment_Info fs_info;

#endif

#ifdef FRAG // fragment shader only

layout(set = 1, binding = 0) uniform sampler2DShadow shadow_maps[DIR_LIGHT_COUNT * CSM_COUNT];
layout(set = 3, binding = 0) uniform UBO_Material_Uniforms { Material_Uniforms material_ubo; };
layout(set = 4, binding = 0) uniform sampler2D material_textures[2];

layout(location = 0) out vec4 fc;

layout(location = 0) flat in uint dir_light_count;
layout(location = 1) in Fragment_Info fs_info;

vec3 cascade_i() {
    float fz = fs_info.view_frag_pos.z;
    vec4  d  = fs_info.cascade_boundaries;

    int  j = 4 - int(dot(vec4(fz > d.x, fz > d.y, fz > d.z, fz > d.w), vec4(1,1,1,1)));
    int  i = max(j - 1, 0);
    vec4 b = vec4(fz-d.x,fz-d.y,fz-d.z,fz-d.w); // positive == before far plane

    float d0 = abs(b[i]);
    float d1 = abs(b[j]);
    uint sdi = i + 1 * int(d1 < d0); // cascade pivot index
    float sd = b[sdi];

    float bf = (sd / CSM_BLEND_BAND) * 0.5 + 0.5;

    return vec3(sdi + 1 * int(bf < 0), min(sdi + 1 * int(bf < 1), CSM_COUNT-1), clamp(1 - bf, 0, 1));
}

float in_shadow(uint li) {
    vec3 ci = cascade_i();
    // pv3(ci);

    uint ca = uint(ci.x);
    uint cb = uint(ci.y);
    float c = ci.z;

    float a = texture(shadow_maps[li + ca],
                      vec3(fs_info.dir_lights[li].ls_frag_pos[ca].xy * 0.5 + 0.5,
                           fs_info.dir_lights[li].ls_frag_pos[ca].z));
    float b = texture(shadow_maps[li + cb],
                      vec3(fs_info.dir_lights[li].ls_frag_pos[cb].xy * 0.5 + 0.5,
                           fs_info.dir_lights[li].ls_frag_pos[cb].z));

    return mix(a, b, c);
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
