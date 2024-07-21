#ifndef SOL_SHADER_H_INCLUDE_GUARD_
#define SOL_SHADER_H_INCLUDE_GUARD_

#include "gltf.h"
#include "defs.h"
#include "shader.h.glsl"

#define SHADER_MATERIAL_UBO_SIZE sizeof(Material_Uniforms)

#define SHADER_MAX_DESCRIPTOR_SET_COUNT 5
#define SHADER_MAX_DESCRIPTOR_SET_COUNT_OUTSIDE_MODEL_SCOPE 2
#define SHADER_MAX_DESCRIPTOR_SET_BINDING_COUNT 1 // current max used bindings in a set
#define SHADER_MAX_PUSH_CONSTANT_RANGE_COUNT 2

// I need to think about what I want to do with the below and with shader.c
// I am fairly certain that everything below this point is currently unused.
/*--------------------------------------------------------------------------------*/

#define RECREATE_SHADER_DIR 1

typedef enum {
    DESCRIPTOR_SET_ORDER_VS_INFO,
    DESCRIPTOR_SET_ORDER_SHADOW_MAPS,
    DESCRIPTOR_SET_ORDER_TRANSFORMS_UBO,
    DESCRIPTOR_SET_ORDER_MATERIAL_UBO,
    DESCRIPTOR_SET_ORDER_MATERIAL_TEXTURES,
} descriptor_set_order;

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

#endif
