#ifndef SOL_GPU_H_INCLUDE_GUARD_
#define SOL_GPU_H_INCLUDE_GUARD_

#include "sol_vulkan.h"

#define GPU_ALLOCATION_CALLBACKS NULL

#define GAC GPU_ALLOCATION_CALLBACKS

#include "defs.h"
#include "glfw.h"
#include "image.h"
#include "shader.h"
#include "gltf.h"
#include "thread.h"
#include "vulkan_errors.h"

enum gpu_flag_bits {
    GPU_UMA_BIT                                = 0x01,
    GPU_DISCRETE_TRANSFER_BIT                  = 0x02,
    GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT = 0x04,
    GPU_SAMPLER_ANISOTROPY_ENABLE_BIT          = 0x08,
};

/*
struct gpu_settings {
    VkSampleCountFlagBits sample_count           = VK_SAMPLE_COUNT_1_BIT;
    uint32                mip_levels             = 1;
    float                 anisotropy             = 0;
    VkViewport            viewport               = {};
    VkRect2D              scissor                = {};
    uint32                pl_dynamic_state_count = 2;
    VkDynamicState        pl_dynamic_states[2]   = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
};
*/
struct gpu_settings {
    VkSampleCountFlagBits texture_sample_count;
    float                 anisotropy;
    VkViewport            viewport;
    VkRect2D              scissor;
    uint32                pl_dynamic_state_count;
    VkDynamicState        pl_dynamic_states;
    struct {
        uint width;
        uint height;
    } shadow_maps;
};

#define GPU_SWAPCHAIN_MAX_IMAGE_COUNT 4

struct gpu_swapchain {
    uint                     i; // image index
    uint                     image_count;
    VkImage                  images[GPU_SWAPCHAIN_MAX_IMAGE_COUNT];
    VkImageView              image_views[GPU_SWAPCHAIN_MAX_IMAGE_COUNT];
    VkSurfaceKHR             surface;
    VkSwapchainKHR           swapchain;
    VkSwapchainCreateInfoKHR info;
};

enum {
    DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX = 0,
    DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX  = 1,
};

struct gpu_device_memory {
    VkDeviceMemory mem;
    size_t size;
    size_t used;
};

struct gpu_buffer {
    VkBuffer           buf;
    VkDeviceAddress    address;
    uchar             *data;
    size_t             size;
    size_t             used;
    VkBufferUsageFlags usage;
};

#define GPU_HDR_COLOR_ATTACHMENT_COUNT FRAME_COUNT
#define GPU_DEPTH_ATTACHMENT_COUNT     FRAME_COUNT
#define GPU_SHADOW_ATTACHMENT_COUNT    FRAME_COUNT
#define GPU_BIND_BUFFER_COUNT          FRAME_COUNT
#define GPU_TRANSFER_BUFFER_COUNT      FRAME_COUNT

struct gpu_memory {
    VkImageView hdr_color_views    [GPU_HDR_COLOR_ATTACHMENT_COUNT];
    VkImageView depth_views        [GPU_DEPTH_ATTACHMENT_COUNT];
    VkImageView shadow_views       [GPU_SHADOW_ATTACHMENT_COUNT];

    VkDeviceMemory depth_mems      [GPU_DEPTH_ATTACHMENT_COUNT];
    VkDeviceMemory hdr_color_mems  [GPU_HDR_COLOR_ATTACHMENT_COUNT];

    VkImage hdr_color_attachments  [GPU_HDR_COLOR_ATTACHMENT_COUNT];
    VkImage depth_attachments      [GPU_DEPTH_ATTACHMENT_COUNT];
    VkImage shadow_attachments     [GPU_SHADOW_ATTACHMENT_COUNT];

    VkDeviceMemory bind_mem;
    VkDeviceMemory transfer_mem;
    VkDeviceMemory descriptor_mem;

    struct gpu_device_memory texture_memory;

    struct gpu_buffer bind_buffer; // index, vertex, uniform
    struct gpu_buffer transfer_buffer;
    struct gpu_buffer descriptor_buffer_resource;
    struct gpu_buffer descriptor_buffer_sampler;
};

struct gpu_texture {
    VkImage      vkimage;
    VkImageView  view;
    size_t       size;
    struct image image;
};

struct shadow_maps {
    uint count;
    VkImage *images;
    VkImageView *views;
    VkSampler sampler;
    VkDescriptorSetLayout dsl;
    size_t db_offset;
};

#define GPU_MAX_DESCRIPTOR_SIZE 128

struct gpu_texture_deprecated {
    struct gpu_texture image;
    #if DESCRIPTOR_BUFFER
    uchar descriptor[GPU_MAX_DESCRIPTOR_SIZE];
    #endif
};

struct gpu_defaults {
    struct gpu_texture_deprecated texture;
    VkSampler          sampler;
    uint32             dynamic_state_count;
    VkDynamicState     dynamic_states[2];
    float              blend_constants[4];
    float              sampler_anisotropy;
};

struct gpu_descriptors {
    VkPhysicalDeviceDescriptorBufferPropertiesEXT props;
};

struct gpu {
    struct gpu_defaults defaults;

    struct gpu_descriptors descriptors;

    uint64 flags;
    uint64 hotload_flags; // hot load set bits
    struct gpu_settings settings;
    VkPhysicalDeviceProperties props;

    VkInstance       instance;
    VkDevice         device;
    VkPhysicalDevice physical_device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    uint32 graphics_queue_index;
    uint32 present_queue_index;
    uint32 transfer_queue_index;

    struct gpu_swapchain swapchain;
    struct gpu_memory mem;

    struct shader_dir shader_dir;
    VkPipelineCache pipeline_cache;

    allocator *alloc_heap;
    allocator *alloc_temp;
    thread_pool *threads;

    uint sampler_count;

    #if NO_DESCRIPTOR_BUFFER
    VkDescriptorPool resource_dp[THREAD_COUNT + 1]; // +1 for main thread
    VkDescriptorPool sampler_dp[THREAD_COUNT + 1];
    #endif

#if DEBUG
    VkDebugUtilsMessengerEXT dbg;
#endif
};

struct init_gpu_args {
    struct window *glfw;
    thread_pool *threads;
    allocator   *alloc_heap;
    allocator   *alloc_temp;
};

void init_gpu(struct gpu *gpu, struct init_gpu_args *args);
void gpu_poll_hotloader(struct gpu *gpu);
void shutdown_gpu(struct gpu *gpu);

bool create_shadow_maps(struct gpu *gpu, VkCommandBuffer transfer_cmd, VkCommandBuffer graphics_cmd, struct shadow_maps *maps);
void gpu_create_texture(struct gpu *gpu, struct image *image, struct gpu_texture *ret);
void gpu_destroy_image(struct gpu *gpu, struct gpu_texture *image);
void gpu_destroy_image_and_view(struct gpu *gpu, struct gpu_texture *image);
struct memreq gpu_texture_memreq(struct gpu *gpu, struct gpu_texture *image);
size_t gpu_allocate_image_memory(struct gpu *gpu, size_t size);
void gpu_bind_image(struct gpu *gpu, VkImage image, size_t ofs);
void gpu_create_texture_view(struct gpu *gpu, struct gpu_texture *image);
VkSampler gpu_create_gltf_sampler(struct gpu *gpu, gltf_sampler *info);
void gpu_destroy_sampler(struct gpu *gpu, VkSampler sampler);

bool resource_dp_allocate(struct gpu *gpu, uint thread_i, uint count, VkDescriptorSetLayout *layouts, VkDescriptorSet *sets);
bool sampler_dp_allocate(struct gpu *gpu, uint thread_i, uint count, VkDescriptorSetLayout *layouts, VkDescriptorSet *sets);

void resource_dp_reset(struct gpu *gpu, uint thread_i)
{
    vk_reset_descriptor_pool(gpu->device, gpu->resource_dp[thread_i], 0x0);
}
void sampler_dp_reset(struct gpu *gpu, uint thread_i)
{
    vk_reset_descriptor_pool(gpu->device, gpu->sampler_dp[thread_i], 0x0);
}

void reset_descriptor_pools(struct gpu *gpu)
{
    for(uint i=0; i < THREAD_COUNT+1; ++i) {
        resource_dp_reset(gpu, i);
        sampler_dp_reset(gpu, i);
    }
}

#define GPU_BUF_ALLOC_FAIL Max_u64
size_t gpu_buffer_allocate(struct gpu *gpu, struct gpu_buffer *buf, size_t size);

static inline size_t gpu_buffer_align(struct gpu *gpu, size_t size)
{
    return align(size, gpu->props.limits.optimalBufferCopyOffsetAlignment);
}

struct htp_rsc { // hdr_to_present_resources silly name, but others are too long
    VkShaderModule        shader_modules[2];
    VkDescriptorSetLayout dsl;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipeline;
    size_t                db_offset;
    size_t                vertex_offset;
};

struct renderpass;
bool htp_allocate_resources(
    struct gpu        *gpu,
    struct renderpass *rp,
    uint               subpass,
    VkCommandBuffer    transfer_cmd,
    VkCommandBuffer    graphics_cmd,
    struct htp_rsc    *rsc);

void htp_free_resources(struct gpu *gpu, struct htp_rsc *rsc);
void htp_commands(VkCommandBuffer cmd, struct gpu *gpu, struct htp_rsc *rsc);

struct vertex_info_descriptor { // @BadName
    VkDescriptorSetLayout dsl;
    size_t db_offset;
    size_t bb_offset;
};

Vertex_Info* init_vs_info(struct gpu *gpu, vector pos, vector fwd, struct vertex_info_descriptor *ret);

// @Todo All these update functions rely on UMA
static inline void update_vs_info_mat_model(struct gpu *gpu, size_t bb_ofs, matrix *model)
{
    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);
    vs->model = *model;
}

static inline void update_vs_info_mat_view(struct gpu *gpu, size_t bb_ofs, matrix *view)
{
    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);
    vs->view = *view;
}

static inline void update_vs_info_mat_proj(struct gpu *gpu, size_t bb_ofs, matrix *proj)
{
    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);
    vs->proj = *proj;
}

static inline void update_vs_info_pos_eye(struct gpu *gpu, size_t bb_ofs, vector eye_pos)
{
    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);
    vs->eye_pos = get_vector(eye_pos.x, eye_pos.y, eye_pos.z, 1);
}

static inline void update_vs_info_ambient(struct gpu *gpu, size_t bb_ofs, vector ambient)
{
    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);
    vs->ambient = get_vector(ambient.x, ambient.y, ambient.z, 1);
}

static inline uint next_swapchain_image(struct gpu *gpu, VkSemaphore semaphore, VkFence fence)
{
    uint i;
    VkResult check = vkAcquireNextImageKHR(gpu->device, gpu->swapchain.swapchain, 10e9, semaphore, fence, &i);
    DEBUG_VK_OBJ_CREATION(vkAcquireNextImageKHR, check);
    return i;
}

#define GPU_BIND_BUFFER_RESERVED_SIZE                1024
#define GPU_TRANSFER_BUFFER_RESERVED_SIZE            0
#define GPU_DESCRIPTOR_BUFFER_RESOURCE_RESERVED_SIZE 512
#define GPU_DESCRIPTOR_BUFFER_SAMPLER_RESERVED_SIZE  1024
#define GPU_IMAGE_MEMORY_RESERVED_SIZE               1024

static inline void reset_gpu_buffers(struct gpu *gpu)
{
    size_t bind_min        = GPU_BIND_BUFFER_RESERVED_SIZE;
    size_t transfer_min    = GPU_TRANSFER_BUFFER_RESERVED_SIZE;
    size_t db_resource_min = GPU_DESCRIPTOR_BUFFER_RESOURCE_RESERVED_SIZE;
    size_t db_sampler_min  = GPU_DESCRIPTOR_BUFFER_SAMPLER_RESERVED_SIZE;
    size_t image_min       = GPU_IMAGE_MEMORY_RESERVED_SIZE;

    atomic_store(&gpu->mem.bind_buffer.used, &bind_min);
    atomic_store(&gpu->mem.transfer_buffer.used, &transfer_min);
    atomic_store(&gpu->mem.descriptor_buffer_resource.used, &db_resource_min);
    atomic_store(&gpu->mem.descriptor_buffer_sampler.used, &db_sampler_min);
    atomic_store(&gpu->mem.texture_memory.used, &image_min);

    _mm_sfence();
}

void gpu_upload_images_with_base_offset(
    struct gpu       *gpu,
    uint              count,
    struct gpu_texture *images,
    size_t            base_offset,
    uint             *offsets,
    VkCommandBuffer   transfer,
    VkCommandBuffer   graphics);

void gpu_upload_bind_buffer(
    struct gpu      *gpu,
    uint             count,
    VkBufferCopy    *regions,
    struct range    *range,
    VkCommandBuffer  transfer,
    VkCommandBuffer  graphics);

void gpu_upload_descriptor_buffer(
    struct gpu      *gpu,
    uint             count,
    VkBufferCopy    *regions,
    struct range    *range,
    bool             resource,
    VkCommandBuffer  transfer,
    VkCommandBuffer  graphics);

static inline void gpu_upload_descriptor_buffer_resource(
    struct gpu      *gpu,
    uint             count,
    VkBufferCopy    *regions,
    struct range    *range,
    VkCommandBuffer  transfer_cmd,
    VkCommandBuffer  graphics_cmd)
{
    gpu_upload_descriptor_buffer(gpu, count, regions, range, true, transfer_cmd, graphics_cmd);
}

static inline void gpu_upload_descriptor_buffer_sampler(
    struct gpu      *gpu,
    uint             count,
    VkBufferCopy    *regions,
    struct range    *range,
    VkCommandBuffer  transfer_cmd,
    VkCommandBuffer  graphics_cmd)
{
    gpu_upload_descriptor_buffer(gpu, count, regions, range, false, transfer_cmd, graphics_cmd);
}

void gpu_blit_gltf_texture_mipmaps(gltf *model, struct gpu_texture *images, VkCommandBuffer graphics);

struct renderpass {
    VkRenderPass  rp;
    VkFramebuffer fb;
};

struct shadow_pass_info {
    struct renderpass *rp;
    struct gpu *gpu;
    struct shadow_maps *maps;
    struct load_model_ret *lmr;

#if SPLIT_SHADOW_MVP
    matrix *light_model;
    matrix *light_view;
    matrix *light_proj;
#else
    matrix *light_spaces;
#endif
};

void create_color_renderpass(struct gpu *gpu, struct renderpass *rp);
void create_shadow_renderpass(struct gpu *gpu, struct shadow_maps *shadow_maps, struct renderpass *rp);
void begin_color_renderpass(VkCommandBuffer cmd, struct renderpass *rp, VkRect2D area);
void do_shadow_pass(VkCommandBuffer cmd, struct shadow_pass_info *info, allocator *alloc);
void free_shadow_maps(struct gpu *gpu, struct shadow_maps *maps);

static inline void end_renderpass(VkCommandBuffer cmd) {vk_cmd_end_renderpass(cmd);}
static inline void destroy_renderpass(struct gpu *gpu, struct renderpass *rp)
{
    vk_destroy_renderpass(gpu->device, rp->rp, GAC);
    vk_destroy_framebuffer(gpu->device, rp->fb, GAC);
}

static inline void bind_descriptor_buffers(VkCommandBuffer cmd, struct gpu *gpu)
{
    VkDescriptorBufferBindingInfoEXT bi[2];
    bi[DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX] = (VkDescriptorBufferBindingInfoEXT) {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .address = gpu->mem.descriptor_buffer_resource.address,
        .usage = gpu->mem.descriptor_buffer_resource.usage,
    };
    bi[DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX] = (VkDescriptorBufferBindingInfoEXT) {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .address = gpu->mem.descriptor_buffer_sampler.address,
        .usage = gpu->mem.descriptor_buffer_sampler.usage,
    };
    vk_cmd_bind_descriptor_buffers_ext(cmd, 2, bi);
}

// copy into an array of combined image sampler descriptors at position i
void ds_cis_arrcpy(struct gpu *gpu, uchar *arr_start, uint i, uint arr_len, uchar *desc)
{
    if (gpu->descriptors.props.combinedImageSamplerDescriptorSingleArray) {
        memcpy(arr_start + i * gpu->descriptors.props.combinedImageSamplerDescriptorSize,
               desc, gpu->descriptors.props.combinedImageSamplerDescriptorSize);
    } else {
        size_t ofs = arr_len * gpu->descriptors.props.sampledImageDescriptorSize +
            i * gpu->descriptors.props.samplerDescriptorSize;

        memcpy(arr_start + i * gpu->descriptors.props.sampledImageDescriptorSize,
               desc, gpu->descriptors.props.sampledImageDescriptorSize);
        memcpy(arr_start + ofs, desc + gpu->descriptors.props.sampledImageDescriptorSize,
               gpu->descriptors.props.samplerDescriptorSize);
    }
}

VkCommandPool create_commandpool(VkDevice device, uint queue_family_index, uint flags);

static inline VkCommandPool create_graphics_command_pool(struct gpu *gpu)
{
    return create_commandpool(gpu->device, gpu->graphics_queue_index, 0x0);
}

static inline VkCommandPool create_transient_graphics_command_pool(struct gpu *gpu)
{
    return create_commandpool(gpu->device, gpu->graphics_queue_index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
}

static inline VkCommandPool create_transfer_command_pool(struct gpu *gpu)
{
    return create_commandpool(gpu->device, gpu->transfer_queue_index, 0x0);
}

static inline VkCommandPool create_transient_transfer_command_pool(struct gpu *gpu)
{
    return create_commandpool(gpu->device, gpu->transfer_queue_index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
}

static inline void reset_command_pool(struct gpu *gpu, VkCommandPool pool)
{
    VkResult check = vk_reset_command_pool(gpu->device, pool, 0x0);
    DEBUG_VK_OBJ_CREATION(vkResetCommandPool, check);
}

static inline void reset_command_pool_and_release_resources(struct gpu *gpu, VkCommandPool pool)
{
    VkResult check = vk_reset_command_pool(gpu->device, pool, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    DEBUG_VK_OBJ_CREATION(vkResetCommandPool, check);
}

static inline void allocate_command_buffers(struct gpu *gpu, VkCommandPool pool, uint count, VkCommandBuffer *cmds)
{
    VkCommandBufferAllocateInfo i = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = count,
    };
    VkResult check = vk_allocate_command_buffers(gpu->device, &i, cmds);
    DEBUG_VK_OBJ_CREATION(vkAllocateCommandBuffers, check);
}

static inline void begin_command_buffers(uint count, VkCommandBuffer *cmds)
{
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    for(uint i=0; i < count; ++i) {
        VkResult check = vk_begin_command_buffer(cmds[i], &bi);
        DEBUG_VK_OBJ_CREATION(vkBeginCommandBuffer, check);
    }
}

static inline void begin_onetime_command_buffers(uint count, VkCommandBuffer *cmds)
{
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    for(uint i=0; i < count; ++i) {
        VkResult check = vk_begin_command_buffer(cmds[i], &bi);
        DEBUG_VK_OBJ_CREATION(vkBeginCommandBuffer, check);
    }
}

static inline void end_command_buffer(VkCommandBuffer cmd)
{
    VkResult check = vk_end_command_buffer(cmd);
    DEBUG_VK_OBJ_CREATION(vkEndCommandBuffer, check);
}

VkSemaphore create_semaphore(VkDevice device, VkSemaphoreType type, uint64 initial_value);

static inline VkSemaphore create_binary_semaphore(struct gpu *gpu) {
    return create_semaphore(gpu->device, VK_SEMAPHORE_TYPE_BINARY, 0);
}
static inline VkSemaphore create_timeline_semaphore(struct gpu *gpu, uint64 initial_value) {
    return create_semaphore(gpu->device, VK_SEMAPHORE_TYPE_TIMELINE, initial_value);
}

static inline VkFence create_fence(struct gpu *gpu, bool signalled) {
    VkFenceCreateInfo info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    info.flags = signalled ? VK_FENCE_CREATE_SIGNALED_BIT : 0x0;

    VkFence ret;
    vk_create_fence(gpu->device, &info, GAC, &ret);
    return ret;
}

enum {
    PL_VERTEX_INPUT_BIT = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
    PL_COLOR_OUTPUT_BIT = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    PL_TRANSFER_BIT     = VK_PIPELINE_STAGE_TRANSFER_BIT,
};

inline static void semaphore_submit_info(VkSemaphore sem, uint64 val, uint stages, VkSemaphoreSubmitInfo *ret)
{
    *ret = (VkSemaphoreSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sem,
        .value = val,
        .stageMask = stages,
    };
}

inline static void transfer_semaphore_submit_info(VkSemaphore sem, uint64 val, VkSemaphoreSubmitInfo *ret)
{
    semaphore_submit_info(sem, val, PL_TRANSFER_BIT, ret);
}

inline static void vertex_input_semaphore_submit_info(VkSemaphore sem, uint64 val, VkSemaphoreSubmitInfo *ret)
{
    semaphore_submit_info(sem, val, PL_VERTEX_INPUT_BIT, ret);
}

inline static void color_output_semaphore_submit_info(VkSemaphore sem, uint64 val, VkSemaphoreSubmitInfo *ret)
{
    semaphore_submit_info(sem, val, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, ret);
}

inline static void all_graphics_semaphore_submit_info(VkSemaphore sem, uint64 val, VkSemaphoreSubmitInfo *ret)
{
    semaphore_submit_info(sem, val, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, ret);
}

inline static void command_buffer_submit_info(VkCommandBuffer command_buffer, VkCommandBufferSubmitInfo *ret)
{
     *ret = (VkCommandBufferSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = command_buffer,
    };
}

void queue_submit_info(
    uint                       cmd_count,
    VkCommandBufferSubmitInfo *cmd_infos,
    uint                       wait_count,
    VkSemaphoreSubmitInfo     *wait_infos,
    uint                       signal_count,
    VkSemaphoreSubmitInfo     *signal_infos,
    VkSubmitInfo2             *ret);

static inline void graphics_queue_submit(struct gpu *gpu, uint count, VkSubmitInfo2 *submit_infos, VkFence fence) {
    VkResult check = vk_queue_submit2(gpu->graphics_queue, count, submit_infos, fence);
    DEBUG_VK_OBJ_CREATION(vkQueueSubmit2, check);
}

static inline void transfer_queue_submit(struct gpu *gpu, uint count, VkSubmitInfo2 *submit_infos, VkFence fence) {
    VkResult check = vk_queue_submit2(gpu->transfer_queue, count, submit_infos, fence);
    DEBUG_VK_OBJ_CREATION(vkQueueSubmit2, check);
}

static inline void present_queue_submit(struct gpu *gpu, uint w_sem_count, VkSemaphore *w_sems)
{
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = w_sem_count,
        .pWaitSemaphores = w_sems,
        .swapchainCount = 1,
        .pSwapchains = &gpu->swapchain.swapchain,
        .pImageIndices = &gpu->swapchain.i,
        .pResults = NULL,
    };
    VkResult check = vk_queue_present_khr(gpu->present_queue, &pi);
    DEBUG_VK_OBJ_CREATION(vkQueuePresentKHR, check);
}

static inline void fence_wait_secs_and_reset(struct gpu *gpu, VkFence fence, uint time)
{
    VkResult r = vk_wait_for_fences(gpu->device, 1, &fence, 1, time * 1e9);
    DEBUG_VK_OBJ_CREATION(vkWaitForFences, r);
    vk_reset_fences(gpu->device, 1, &fence);
}

struct draw_box_rsc {
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkShaderModule modules[2];
};

// @Todo Only works on UMA
void draw_box(VkCommandBuffer cmd, struct gpu *gpu, struct box *box, bool wireframe,
              VkRenderPass rp, uint subpass, struct draw_box_rsc *rsc, matrix *space, vector color);
void draw_box_cleanup(struct gpu *gpu, struct draw_box_rsc *rsc);

struct draw_floor_rsc {
    VkShaderModule modules[2];
    VkPipelineLayout layout;
    VkPipeline pipeline;
};

#if NO_DESCRIPTOR_BUFFER
void draw_floor(VkCommandBuffer cmd, struct gpu *gpu, VkRenderPass rp, uint subpass,
        uint count, VkDescriptorSetLayout *dsls, VkDescriptorSet *sets, struct draw_floor_rsc *rsc);
#else
void draw_floor(VkCommandBuffer cmd, struct gpu *gpu, VkRenderPass rp, uint subpass,
                VkDescriptorSetLayout dsls[2], uint db_indices[2], size_t db_offsets[2],
                struct draw_floor_rsc *rsc);
#endif
void draw_floor_cleanup(struct gpu *gpu, struct draw_floor_rsc *rsc);

#endif // include guard
