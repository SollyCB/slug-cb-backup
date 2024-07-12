#ifndef SOL_GLTF_H_INCLUDE_GUARD_
#define SOL_GLTF_H_INCLUDE_GUARD_

#include "sol_vulkan.h"
#include "defs.h"
#include "file.h"
#include "math.h"
#include "test.h"
#include "string.h"
#include "image.h"

#include "gltf_limits.h"

inline static bool gltf_is_node_mask_zero(uint64 node_mask[GLTF_U64_NODE_MASK])
{
    bool b = false;
    for(uint i=0; i < GLTF_U64_NODE_MASK; ++i)
        b = node_mask[i] || b;
    return !b;
}

inline static bool gltf_is_skin_mask_zero(uint64 skin_mask)
{
    return !skin_mask;
}

typedef enum {
    GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT           = 0x0001,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT  = 0x0002,
    GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT          = 0x0004,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT = 0x0008,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT   = 0x0010,
    GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT          = 0x0020,

    GLTF_ACCESSOR_TYPE_SCALAR_BIT = 0x0040,
    GLTF_ACCESSOR_TYPE_VEC2_BIT   = 0x0080,
    GLTF_ACCESSOR_TYPE_VEC3_BIT   = 0x0100,
    GLTF_ACCESSOR_TYPE_VEC4_BIT   = 0x0200,
    GLTF_ACCESSOR_TYPE_MAT2_BIT   = 0x0400,
    GLTF_ACCESSOR_TYPE_MAT3_BIT   = 0x0800,
    GLTF_ACCESSOR_TYPE_MAT4_BIT   = 0x1000,

    GLTF_ACCESSOR_NORMALIZED_BIT  = 0x2000,
    GLTF_ACCESSOR_SPARSE_BIT      = 0x4000,
    GLTF_ACCESSOR_MINMAX_BIT      = 0x8000,

    GLTF_ACCESSOR_COMPONENT_TYPE_BITS = GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT           |
                                        GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT  |
                                        GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT          |
                                        GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT |
                                        GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT   |
                                        GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT,

    GLTF_ACCESSOR_TYPE_BITS = GLTF_ACCESSOR_TYPE_SCALAR_BIT |
                              GLTF_ACCESSOR_TYPE_VEC2_BIT   |
                              GLTF_ACCESSOR_TYPE_VEC3_BIT   |
                              GLTF_ACCESSOR_TYPE_VEC4_BIT   |
                              GLTF_ACCESSOR_TYPE_MAT2_BIT   |
                              GLTF_ACCESSOR_TYPE_MAT3_BIT   |
                              GLTF_ACCESSOR_TYPE_MAT4_BIT,
} gltf_accessor_flag_bits;

typedef struct {
    gltf_accessor_flag_bits component_type;
    uint buffer_view;
    uint64 byte_offset;
} gltf_accessor_sparse_indices;

typedef struct {
    uint32 buffer_view;
    uint64 byte_offset;
} gltf_accessor_sparse_values;

typedef struct {
    uint count;
    gltf_accessor_sparse_indices indices;
    gltf_accessor_sparse_values values;
} gltf_accessor_sparse;

typedef struct {
    float max[16];
    float min[16];
} gltf_accessor_max_min;

// @Optimise I thought that I made sparse and min_max pointers to keep the accessor arrays
// more contiguous, especially as this data is normally empty. I assume that the struct is
// still small enough for to not really be an issue, but I would want to know for sure.

typedef struct {
    uint flags;
    VkFormat vkformat;
    uint byte_stride;
    uint count;
    uint buffer_view;
    uint64 byte_offset;
    gltf_accessor_sparse sparse;
    gltf_accessor_max_min max_min;
} gltf_accessor;

#define GLTF_ANIMATION_UBO_DESCRIPTOR_SET_INDEX 3
#define GLTF_ANIMATION_UBO_BINDING_INDEX        0

typedef enum {
    GLTF_ANIMATION_PATH_TRANSLATION_BIT          = 0x01,
    GLTF_ANIMATION_PATH_ROTATION_BIT             = 0x02,
    GLTF_ANIMATION_PATH_SCALE_BIT                = 0x04,
    GLTF_ANIMATION_PATH_WEIGHTS_BIT              = 0x08,

    GLTF_ANIMATION_PATH_BITS = GLTF_ANIMATION_PATH_TRANSLATION_BIT |
                               GLTF_ANIMATION_PATH_ROTATION_BIT    |
                               GLTF_ANIMATION_PATH_SCALE_BIT       |
                               GLTF_ANIMATION_PATH_WEIGHTS_BIT,
} gltf_animation_path_bits;

typedef enum {
    GLTF_ANIMATION_INTERPOLATION_LINEAR,
    GLTF_ANIMATION_INTERPOLATION_STEP,
    GLTF_ANIMATION_INTERPOLATION_CUBICSPLINE,
} gltf_animation_interpolation;

typedef struct {
    uint input;
    uint output;
    gltf_animation_interpolation interpolation;
} gltf_animation_sampler;

typedef struct {
    uint path_mask;
    uint node;
    uint16 samplers[popcnt(GLTF_ANIMATION_PATH_BITS)]; // This could be u8 probably.
} gltf_animation_target;

typedef struct {
    uint target_count;
    uint sampler_count;
    gltf_animation_target *targets;
    gltf_animation_sampler *samplers;
} gltf_animation;

typedef struct {
    string uri;
    uint64 byte_length;
} gltf_buffer;

typedef enum {
    GLTF_BUFFER_VIEW_INDEX_BUFFER_BIT  = 0x01,
    GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT = 0x02,

    GLTF_BUFFER_VIEW_BUFFER_TYPE_BITS = GLTF_BUFFER_VIEW_INDEX_BUFFER_BIT  |
                                        GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT,
} gltf_buffer_view_flag_bits;

typedef struct {
    uint flags;
    uint buffer;
    uint64 byte_stride;
    uint64 byte_offset;
    uint64 byte_length;
} gltf_buffer_view;

typedef enum {
    GLTF_CAMERA_ORTHOGRAPHIC_BIT = 0x01,
    GLTF_CAMERA_PERSPECTIVE_BIT  = 0x02,

    GLTF_CAMERA_TYPE_BITS = GLTF_CAMERA_ORTHOGRAPHIC_BIT |
                            GLTF_CAMERA_PERSPECTIVE_BIT,
} gltf_camera_flags;

typedef struct {
    float xmag;
    float ymag;
    float zfar;
    float znear;
} gltf_camera_orthographic;

typedef struct {
    float aspect_ratio;
    float yfov;
    float zfar;
    float znear;
} gltf_camera_perspective;

typedef struct {
    uint flags;
    union {
        gltf_camera_orthographic orthographic;
        gltf_camera_perspective perspective;
    };
} gltf_camera;

typedef enum { // I intend to add ktx2.
    GLTF_IMAGE_JPEG_BIT = 0x01,
    GLTF_IMAGE_PNG_BIT  = 0x02,

    GLTF_IMAGE_MIME_TYPE_BITS = GLTF_IMAGE_JPEG_BIT | GLTF_IMAGE_PNG_BIT,
} gltf_image_flag_bits;

typedef struct {
    uint flags;
    uint buffer_view;
    string uri;
} gltf_image;

enum {
    GLTF_MATERIAL_BASE_COLOR_TEXTURE_SET_INDEX = 0,
    GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_SET_INDEX = 1,
    GLTF_MATERIAL_NORMAL_TEXTURE_SET_INDEX = 2,
    GLTF_MATERIAL_OCCLUSION_TEXTURE_SET_INDEX = 3,
    GLTF_MATERIAL_EMISSIVE_TEXTURE_SET_INDEX = 4,
};

typedef enum {
    GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT         = 0x001,
    GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT = 0x002,
    GLTF_MATERIAL_NORMAL_TEXTURE_BIT             = 0x004,
    GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT          = 0x008,
    GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT           = 0x010,
    GLTF_MATERIAL_ALPHA_MODE_OPAQUE_BIT          = 0x020,
    GLTF_MATERIAL_ALPHA_MODE_MASK_BIT            = 0x040,
    GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT           = 0x080,
    GLTF_MATERIAL_DOUBLE_SIDED_BIT               = 0x100,

    GLTF_MATERIAL_TEXTURE_BITS = GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT | GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT |
                                 GLTF_MATERIAL_NORMAL_TEXTURE_BIT     | GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT          |
                                 GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT,

    GLTF_MATERIAL_ALPHA_MODE_BITS = GLTF_MATERIAL_ALPHA_MODE_OPAQUE_BIT | GLTF_MATERIAL_ALPHA_MODE_MASK_BIT |
                                    GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT,
} gltf_material_flag_bits;

typedef struct {
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3];
    float alpha_cutoff;
} gltf_material_uniforms cl_align(16); // faster stores and easier descriptor allocation

typedef struct {
    uint texture;
    uint texcoord;
} gltf_texture_info;

typedef struct {
    gltf_material_uniforms uniforms;
    gltf_texture_info base_color;
    gltf_texture_info metallic_roughness;
    gltf_texture_info normal;
    gltf_texture_info occlusion;
    gltf_texture_info emissive;
    uint flags;
} gltf_material;

typedef enum {
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION    = 0,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL      = 1,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT     = 2,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD    = 3,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR       = 4,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS      = 5,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS     = 6,
} gltf_mesh_primitive_attribute_type;

typedef enum {
     GLTF_MESH_PRIMITIVE_POINT_LIST     = 0,
     GLTF_MESH_PRIMITIVE_LINE_LIST      = 1,
     GLTF_MESH_PRIMITIVE_LINE_STRIP     = 2,
     GLTF_MESH_PRIMITIVE_TRIANGLE_LIST  = 3,
     GLTF_MESH_PRIMITIVE_TRIANGLE_STRIP = 4,
     GLTF_MESH_PRIMITIVE_TRIANGLE_FAN   = 5,
} gltf_mesh_primitive_topology;

typedef struct {
    uint n;
    uint accessor;
    gltf_mesh_primitive_attribute_type type;
} gltf_mesh_primitive_attribute;

typedef struct {
    uint attribute_count;
    gltf_mesh_primitive_attribute *attributes;
} gltf_mesh_primitive_morph_target;

typedef struct {
    uint shader_i;
    uint indices;
    uint material;
    uint attribute_count;
    uint target_count;
    gltf_mesh_primitive_attribute *attributes;
    gltf_mesh_primitive_morph_target *morph_targets;
    gltf_mesh_primitive_topology topology;
} gltf_mesh_primitive;

typedef struct {
    uint joint_count;
    uint max_instance_count;
    uint primitive_count;
    uint primitives_without_material_count;
    uint weight_count;
    gltf_mesh_primitive *primitives;
    float *weights;
} gltf_mesh;

enum {
    GLTF_NODE_TRS_BIT    = 0x01,
    GLTF_NODE_MATRIX_BIT = 0x02,
};

typedef struct {
    uint flags;
    uint camera;
    uint skin;
    uint mesh;
    uint child_count;
    uint weight_count;
    uint *children;
    float *weights;
    union {
        matrix mat;
        struct trs trs;
    };
} gltf_node;

typedef enum {
    GLTF_SAMPLER_FILTER_NEAREST = 0, // VK_FILTER_NEAREST = 0
    GLTF_SAMPLER_FILTER_LINEAR  = 1, // VK_FILTER_LINEAR = 1
} gltf_sampler_filter;

typedef enum {
    GLTF_SAMPLER_MIPMAP_MODE_NEAREST = 0, // VK_MIPMAP_MODE_NEAREST = 0
    GLTF_SAMPLER_MIPMAP_MODE_LINEAR  = 1, // VK_MIPMAP_MODE_NEAREST = 1
} gltf_sampler_mipmap_mode;

typedef enum {
    GLTF_SAMPLER_ADDRESS_MODE_REPEAT          = 0, // VK_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1, // VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   = 2, // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
} gltf_sampler_address_mode;

typedef struct {
    gltf_sampler_mipmap_mode mipmap_mode;
    gltf_sampler_filter mag_filter;
    gltf_sampler_filter min_filter;
    gltf_sampler_address_mode wrap_u; // vulkan uses 'u' and 'v', gltf uses 's' and 't'
    gltf_sampler_address_mode wrap_v;
} gltf_sampler;

typedef struct {
    string name;
    uint node_count;
    uint *nodes;
} gltf_scene;

typedef struct {
    uint inverse_bind_matrices;
    uint skeleton;
    uint joint_count;
    uint *joints;
} gltf_skin;

typedef struct {
    uint sampler;
    uint source;
} gltf_texture;

typedef struct {
    uint scene;
    uint accessor_count;
    uint animation_count;
    uint buffer_count;
    uint buffer_view_count;
    uint camera_count;
    uint image_count;
    uint material_count;
    uint mesh_count;
    uint node_count;
    uint sampler_count;
    uint scene_count;
    uint skin_count;
    uint texture_count;
    gltf_accessor    *accessors;
    gltf_animation   *animations;
    gltf_buffer      *buffers;
    gltf_buffer_view *buffer_views;
    gltf_camera      *cameras;
    gltf_image       *images;
    gltf_material    *materials;
    gltf_mesh        *meshes;
    gltf_node        *nodes;
    gltf_sampler     *samplers;
    gltf_scene       *scenes;
    gltf_skin        *skins;
    gltf_texture     *textures;

    string            dir;

    struct {
        uint  size;
        void *data;
    } meta;

} gltf;

static inline void
gltf_count_mesh_instances(gltf_node *nodes, uint node, uint *instance_counts, uint64 *skin_mask)
{
    uint mesh = nodes[node].mesh != Max_u32 ? nodes[node].mesh : 0;
    uint skin = nodes[node].skin != Max_u32 ? nodes[node].skin : 0;

    instance_counts[mesh] += nodes[node].mesh != Max_u32;
    *skin_mask |= (1 << skin) & maxif(nodes[node].skin != Max_u32);
    assert(skin < 64);

    for(uint i=0; i < nodes[node].child_count; ++i)
        gltf_count_mesh_instances(nodes, nodes[node].children[i], instance_counts, skin_mask);
}

static inline struct image gltf_load_image(gltf *model, uint image_i)
{
    char uri[256];
    memcpy(uri, model->dir.cstr, model->dir.len);
    memcpy(uri + model->dir.len, model->images[image_i].uri.cstr,
                                 model->images[image_i].uri.len + 1);
    return load_image(uri);
}

static inline int gltf_open_buffer_w(gltf *g, uint buf_i)
{
    char buf[128];
    memcpy(buf, g->dir.cstr, g->dir.len);
    memcpy(buf + g->dir.len, g->buffers[buf_i].uri.cstr,
                             g->buffers[buf_i].uri.len + 1);
    return file_open(buf, WRITE);
}

static inline void gltf_read_buffer(gltf *model, uint buf_i, char *to)
{
    char uri[256];
    memcpy(uri, model->dir.cstr, model->dir.len);
    memcpy(uri + model->dir.len, model->buffers[buf_i].uri.cstr, model->buffers[buf_i].uri.len + 1);
    file_open_read(uri, 0, model->buffers[buf_i].byte_length, to);
}

struct shader_dir; // @Review I do want to reimplement these better...
struct shader_config;
void parse_gltf(const char *file_name, struct shader_dir *dir, struct shader_config *conf, allocator *temp, allocator *persistent, gltf *ret);
void load_gltf(const char *file_name, struct shader_dir *dir, struct shader_config *conf, allocator *temp, allocator *persistent, gltf *g);
void store_gltf(gltf *model, const char *file_name, allocator *alloc);

#if TEST
void test_gltf(test_suite *suite);
#endif

#endif // include guard
