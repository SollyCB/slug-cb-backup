#ifndef SOL_ASSET_H_INCLUDE_GUARD_
#define SOL_ASSET_H_INCLUDE_GUARD_

#include "thread.h"
#include "gpu.h"

enum {
    LOAD_MODEL_RESULT_INCOMPLETE,
    LOAD_MODEL_RESULT_SUCCESS,
    LOAD_MODEL_RESULT_INSUFFICIENT_IMAGE_MEMORY,
    LOAD_MODEL_RESULT_INSUFFICIENT_STAGE_MEMORY,
    LOAD_MODEL_RESULT_INSUFFICIENT_BIND_MEMORY,
    LOAD_MODEL_RESULT_INSUFFICIENT_RESOURCE_DESCRIPTOR_MEMORY,
    LOAD_MODEL_RESULT_INSUFFICIENT_SAMPLER_DESCRIPTOR_MEMORY,
    LOAD_MODEL_RESULT_EXCEEDED_MAX_SAMPLER_COUNT,
};

enum {
    LOAD_MODEL_ALLOCATION_BIND_BUFFER_BIT                = 0x01,
    LOAD_MODEL_ALLOCATION_TRANSFER_BUFFER_BIT            = 0x02,
    LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_RESOURCE_BIT = 0x04,
    LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_SAMPLER_BIT  = 0x08,
    LOAD_MODEL_ALLOCATION_IMAGE_MEMORY_BIT               = 0x10,
};

enum {
    LOAD_MODEL_WIREFRAME_BIT = 0x01,
};

// @Unimplemented Currently just using the SUBPASS_COLOR bit index. I need to
// set up different shader types and stuff before this will work.
enum {
    LOAD_MODEL_SUBPASS_DEPTH = 0x01,
    LOAD_MODEL_SUBPASS_DRAW  = 0x02,
    LOAD_MODEL_SUBPASS_LAST_MEMBER,
    LOAD_MODEL_SUBPASS_COUNT = count_enum_flags(LOAD_MODEL_SUBPASS_LAST_MEMBER),
};

struct animation_info {
    uint  index;
    float time;
    float weight;
};

struct load_model_arg {
    uint                   flags;
    uint                   dsl_count;
    uint                   animation_count;
    uint                   scene_count;
    uint                   subpass_mask;
    uint                   color_subpass;
    uint                   depth_pass_count;
    gltf                  *model;
    struct gpu            *gpu;
    struct animation_info *animations;
    uint                  *scenes;
    VkDescriptorSetLayout  dsls[SHADER_MAX_DESCRIPTOR_SET_COUNT_OUTSIDE_MODEL_SCOPE];
    uint                   db_indices[SHADER_MAX_DESCRIPTOR_SET_COUNT_OUTSIDE_MODEL_SCOPE];
    size_t                 db_offsets[SHADER_MAX_DESCRIPTOR_SET_COUNT_OUTSIDE_MODEL_SCOPE];
    VkRenderPass           color_renderpass;
    VkRenderPass           depth_renderpass;
    VkViewport             viewport;
    VkRect2D               scissor;
};

struct model_primitive_draw_info {
    bool         draw_indexed;
    uint         draw_count;
    uint         vertex_offset_count;
    uint         dsl_count;
    VkIndexType  index_type;
    uint         db_indices[SHADER_MAX_DESCRIPTOR_SET_COUNT];
    size_t       index_offset;
    size_t       db_offsets[SHADER_MAX_DESCRIPTOR_SET_COUNT];
    size_t      *vertex_offsets;
};

struct draw_model_info {
    uint                              mesh_count;
    uint                              prim_count;
    uint                             *mesh_instance_counts;
    uint                             *mesh_primitive_counts;
    VkPipeline                       *pipelines;
    VkPipelineLayout                 *pipeline_layouts;
    struct model_primitive_draw_info *primitive_infos;
    VkBuffer                         *bind_buffers;
};

struct load_model_ret {
    uint                    result;
    uint                    allocation_mask;
    VkCommandBuffer         cmd_transfer;
    VkCommandBuffer         cmd_graphics;
    struct draw_model_info *draw_info;
    bool32                 *thread_cleanup_resources; // @Todo Need a way to communicate that clean up completed?
};

struct load_model_info {
    struct load_model_arg *arg;
    struct load_model_ret *ret;
};

/* - work argument should be a 'struct load_model_info', where 'ret' has all command
   buffers started.
   - 'ret.result' will be any LOAD_MODEL_RESULT other than
   LOAD_MODEL_RESULT_INCOMPLETE when ret is safe to use; command buffers will still be in
   the recording state.
   - draw_info pointer will be overwritten with a pointer to a draw info structure, if 'result'
   is appropriate, 'draw_model' can be called
   - 'ret.thread_cleanup_resources' should be set to true when the model resources can be
   freed, the address itself must not be moved. */
void load_model_tf(struct thread_work_arg *arg);

void draw_model_color(VkCommandBuffer cmd, struct draw_model_info *info);
void draw_model_depth(VkCommandBuffer cmd, struct draw_model_info *info, uint pass);

#endif

/*
    end by jenny's birthday woohoo!!!!!
*/
