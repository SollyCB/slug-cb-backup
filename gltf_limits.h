#ifndef GLTF_LIMITS_H_
#define GLTF_LIMITS_H_

#define GLTF_JOINT_COUNT 24
#define GLTF_MORPH_WEIGHT_COUNT 8
#define GLTF_MAX_NODE_COUNT 128
#define GLTF_MAX_MESH_COUNT 32
#define GLTF_U64_NODE_MASK 2
#define GLTF_U64_SKIN_MASK 1
#define GLTF_U64_MESH_MASK 1

// current max number of textures that can exist on a gltf 2.0 material, not a cap
#define GLTF_MAX_MATERIAL_TEXTURE_COUNT 5

#ifdef GL_core_profile
#define GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT         0x001
#define GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT 0x002
#define GLTF_MATERIAL_NORMAL_TEXTURE_BIT             0x004
#define GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT          0x008
#define GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT           0x010
#define GLTF_MATERIAL_ALPHA_MODE_OPAQUE_BIT          0x020
#define GLTF_MATERIAL_ALPHA_MODE_MASK_BIT            0x040
#define GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT           0x080
#define GLTF_MATERIAL_DOUBLE_SIDED_BIT               0x100
#endif

#endif // include guard
