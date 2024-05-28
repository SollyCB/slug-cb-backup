#ifndef SOL_SHADER_H_INCLUDE_GUARD_
#define SOL_SHADER_H_INCLUDE_GUARD_

#include "gltf.h"
#include "defs.h"

#define RECREATE_SHADER_DIR 1

// @Todo Soon I am going to want the ability to do shadow passes, for which I will
// need more shader types. I think should be implemented through extra flags in
// in the config stating if depth pass shaders should also be generated. Shader sets
// should then also include these other shader types.

typedef enum {
    DESCRIPTOR_SET_ORDER_VS_INFO,
    DESCRIPTOR_SET_ORDER_SHADOW_MAPS,
    DESCRIPTOR_SET_ORDER_TRANSFORMS_UBO,
    DESCRIPTOR_SET_ORDER_MATERIAL_UBO,
    DESCRIPTOR_SET_ORDER_MATERIAL_TEXTURES,
} descriptor_set_order;

// Vertex Shader Variable

typedef struct directional_light {
    vector position;
    vector direction;
    vector color;
    matrix space;
} In_Directional_Light;

typedef struct point_light {
    vector position;
    vector color;
    matrix space;
    float linear;
    float quadratic;
} In_Point_Light;

typedef struct vs_info {
    vector view_pos;
    vector ambient;
    matrix model;
    matrix view;
    matrix proj;

    In_Directional_Light dir_lights[2];

    uint dir_light_count; // dir light count, null, null, null

    uint pad[3];

    // Later
    #if 0
    uint point_light_count;
    In_Point_Light point_lights[1];
    #endif

} Vertex_Info;

typedef struct transforms_ubo {
    matrix node_trs;
    matrix node_tbn;
    matrix joints_trs[1];
    matrix joints_tbn[1];
    float morph_weights[1];
} Transforms_Ubo;

typedef struct material_ubo {
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3];
    float alpha_cutoff;
} Material_Ubo cl_align(16);

#define SHADER_MATERIAL_UBO_SIZE sizeof(Material_Ubo)

// #if SHADER_MATERIAL_UBO_SIZE != sizeof(gltf_material_uniforms)
//     #error SHADER_MATERIAL_UBO_SIZE must be equal in size to gltf_material_uniforms
// #endif

// @Hardware I can manage with set count 4 easily, but it would be more expensive:
// Transforms_Ubo and Material_Ubo can exist in the same set, but then the Transforms_Ubo
// descriptors must have per primitive copies, as opposed to per mesh, as Material_Ubo is
// a per primitive structure. Apparently the cap for my machine is 32 sets, so I guess I
// should not worry. I just remember that on certain devices (I am very sure that these were
// mobile devices) had limits as low as 4.
#define SHADER_MAX_DESCRIPTOR_SET_COUNT 5
#define SHADER_MAX_DESCRIPTOR_SET_COUNT_OUTSIDE_MODEL_SCOPE 2

#define SHADER_TRANSFORMS_UBO_SET_INDEX 2
#define SHADER_TRANSFORMS_UBO_BINDING_INDEX 0
#define SHADER_MATERIAL_TEXTURE_ARRAY_SET_INDEX 3
#define SHADER_MATERIAL_TEXTURE_ARRAY_BINDING_INDEX 0

enum {
    SHADER_CONFIG_VERT_BIT          = VK_SHADER_STAGE_VERTEX_BIT,
    SHADER_CONFIG_TESS_CTL_BIT      = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    SHADER_CONFIG_TESS_EVL_BIT      = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    SHADER_CONFIG_GEO_BIT           = VK_SHADER_STAGE_GEOMETRY_BIT,
    SHADER_CONFIG_FRAG_BIT          = VK_SHADER_STAGE_FRAGMENT_BIT,
    SHADER_CONFIG_COMP_BIT          = VK_SHADER_STAGE_COMPUTE_BIT,
    SHADER_CONFIG_INSTANCE_TRS_BIT  = 0x040,
    SHADER_CONFIG_INSTANCE_TBN_BIT  = 0x080,
    SHADER_CONFIG_DIR_SHADOWS_BIT   = 0x100,
    SHADER_CONFIG_POINT_SHADOWS_BIT = 0x200,

    SHADER_CONFIG_STAGE_BITS = VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT,
};
#define GRAPHICS_PIPELINE_SHADER_STAGE_LIMIT_VULKAN 5

// @Todo Add more shader types, e.g. depth. So change these current ones to be
// 'frag_color' or smtg. Or maybe better is to store shader_i on primitives as
// an array with a bit mask showing which shader types are available.
//
// I also really need to remake this in a way that does not create so many
// shaders. Due to my new understanding of tangents and normals and knowing
// that I always need them, I think I should keep an array of shader modules on
// the dir, then the sets take indices into this set, and if a vertex shader or
// a fragment shader hash matches for any type of shader, then it just copies
// the index. Basically I need to separate out the vertex and fragment shaders,
// because currently if either vertex or fragment shaders do not match, then
// both are remade, which is totally dumb since there will only be a couple of
// different fragment shaders. The only thing that effects the fragment shaders
// are the material, so you can only have as many variants of a fragments shader
// as you can have materials, but combined with the variations of vertex shader
// due to joint counts etc... So yeah, fix this, should be a very quick thing.
struct shader_set {
    uint flags; // config flags that the set was created with
    VkShaderModule vert;
    VkShaderModule frag;
};

extern const char *SHADER_ENTRY_POINT;

struct shader_dir {
    uint64 *hashes;
    struct shader_set *sets;
    struct gpu *gpu;
    allocator *alloc;
};

struct gpu;
struct shader_dir load_shader_dir(struct gpu *gpu, allocator *alloc);
void store_shader_dir(struct shader_dir *dir);

#if DEBUG
#define SHDR_OPTIMISATION shaderc_optimization_level_zero
#else
#define SHDR_OPTIMISATION shaderc_optimization_level_performance
#endif

// @ShaderCount Currently only using frag and vert shaders.
// This define is really just smtg to grep for or to make compile
// errors when I move to more shader types.
#define MAX_SHADERS_PER_SET 2

struct shader_config {
    uint32 flags;
    uint32 dir_light_count;
    uint32 point_light_count;
};

uint find_or_generate_primitive_shader_set(
    struct shader_dir    *dir,
    struct shader_config *conf,
    gltf                 *model,
    uint                  mesh_i,
    uint                  prim_i);

void find_or_generate_model_shader_sets(
    struct shader_dir    *dir,
    struct shader_config *conf,
    gltf                 *model);

static inline size_t calculate_transforms_ubo_size(gltf_mesh *mesh)
{
    // @Todo I want these 'sizeof's to read sizeof(Transforms_Ubo.node_trs), but that
    // is a compile error. See if I can get that working somehow.
    size_t ret = 0;
    ret += (sizeof(matrix) * 2) & max_if(!mesh->joint_count); // node_trs + node_tbn
    ret += (sizeof(matrix) * 2 * mesh->joint_count); // joint_trs + joint_tbn
    ret += (sizeof(float) * mesh->weight_count);
    return alloc_align(ret);
}

enum {
    TRANSFORMS_UBO_NODE_TRS,
    TRANSFORMS_UBO_NODE_TBN,
    TRANSFORMS_UBO_JOINTS_TRS,
    TRANSFORMS_UBO_JOINTS_TBN,
    TRANSFORMS_UBO_MORPH_WEIGHTS,
};
static inline uint transforms_ubo_offsetof(gltf_mesh *mesh, uint field)
{
    // @Optimise would be nice to remove the multiply.
    uint mul = mesh->joint_count ? mesh->joint_count : 1;
    uint size = 0;
    size += sizeof(matrix) * mul & maxif(field != TRANSFORMS_UBO_JOINTS_TRS && field != TRANSFORMS_UBO_NODE_TRS);
    size += sizeof(matrix) * mul & maxif(field == TRANSFORMS_UBO_MORPH_WEIGHTS && size);
    return size;
}

#endif
