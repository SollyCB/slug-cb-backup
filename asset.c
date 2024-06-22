#include "allocator.h"
#include "file.h"
#include "gpu.h"
#include "gltf.h"
#include "blend_types.h"
#include "math.h"
#include "shader.h"
#include "sol_vulkan.h"
#include "thread.h"
#include "asset.h"
#include "vulkan_errors.h"

struct model_memory_offsets {
    size_t           base_stage;
    size_t           base_bind;
    size_t           base_descriptor_resource;
    size_t           base_descriptor_sampler;
    uint            *buffers;
    uint            *transforms_ubos;
    uint            *transforms_ubo_dsls;
    uint            *mesh_instance_counts;
    uint            *material_textures_dsls;
    uint             material_ubo;
    uint             material_ubo_dsl_stride;
    struct pair_uint material_ubo_dsl;
};

struct model_resources {
    uint                   image_count;
    uint                   sampler_count;
    uint                   texture_dsl_count;
    uint                   mesh_count;
    uint                   pipeline_count;
    uint                   pipeline_layout_count;
    void                  *free_me;
    allocator             *alloc;
    struct gpu            *gpu;
    struct gpu_texture    *images;
    VkSampler             *samplers;
    VkDescriptorSetLayout *texture_dsls;
    VkDescriptorSetLayout *transforms_ubo_dsls;
    VkDescriptorSetLayout  material_ubo_dsl;
    VkPipeline            *pipelines;
    VkPipelineLayout      *pipeline_layouts;
    VkShaderModule         depth_shader_vert;
    VkShaderModule         depth_shader_frag;

    VkShaderModule shader_modules[2]; // @TempShader
};

struct model_texture_descriptors {
    uint   stride;
    uchar *data;
};

enum {
    MODEL_MATERIAL_CULL_BACK_BIT = 0x01,
    MODEL_MATERIAL_BLEND_BIT     = 0x02,
    MODEL_MATERIAL_TEXTURED_BIT  = 0x04,
};

struct model_material {
    uint flags;
};

static uint
load_model(
    struct load_model_arg  *arg,
    struct load_model_ret  *ret,
    struct model_resources *resources,
    struct allocators      *allocs,
    struct draw_model_info *draw_info);

static void model_cleanup(struct model_resources *resources);

static void* model_cleanup_tf(struct thread_work_arg *arg);

void load_model_tf(struct thread_work_arg *arg)
{
    struct load_model_info *lmi = arg->arg;
    lmi->ret->result = LOAD_MODEL_RESULT_INCOMPLETE;

    gltf *model = lmi->arg->model;
    uint prim_count = 0;
    uint attr_count = 0;
    uint attr_count_upper_bound = 0;
    uint cnt = 0;
    for(uint i=0; i < model->mesh_count; ++i) {
        prim_count += model->meshes[i].primitive_count;
        for(uint j=0; j < model->meshes[i].primitive_count; ++j) {
            cnt += model->meshes[i].primitives[j].attribute_count;
            for(uint k=0; k < model->meshes[i].primitives[j].target_count; ++k)
                cnt += model->meshes[i].primitives[j].morph_targets[k].attribute_count;
        }
        attr_count_upper_bound = cnt > attr_count_upper_bound ? cnt : attr_count_upper_bound;
        attr_count += cnt;
    }

    uint pipeline_count = prim_count + (prim_count * lmi->arg->depth_pass_count);

    // vulkan handles are typedef'd pointers which makes the linter complain.
    struct model_resources *resources;
    uint resources_size = sizeof(*resources)                      * 1                     +
                          sizeof(*resources->images)              * model->image_count    +
                          sizeof(*resources->samplers)            * model->sampler_count  + // NOLINT - sizeof(vulkan_handle)
                          sizeof(*resources->texture_dsls)        * model->material_count + // NOLINT - sizeof(vulkan_handle)
                          sizeof(*resources->transforms_ubo_dsls) * model->mesh_count     + // NOLINT - sizeof(vulkan_handle)
                          sizeof(*resources->pipelines)           * pipeline_count        + // NOLINT - sizeof(vulkan_handle)
                          sizeof(*resources->pipeline_layouts)    * (prim_count + 1);       // NOLINT - sizeof(vulkan_handle)

    struct draw_model_info *draw_info;
    uint draw_infos_size = sizeof(*draw_info)                                  * 1                      +
                           sizeof(*draw_info->mesh_instance_counts)            * model->mesh_count      +
                           sizeof(*draw_info->mesh_primitive_counts)           * model->mesh_count      +
                           sizeof(*draw_info->bind_buffers)                    * attr_count_upper_bound + // NOLINT - sizeof(vulkan_handle)
                           sizeof(*draw_info->primitive_infos)                 * prim_count             +
                           sizeof(*draw_info->primitive_infos->vertex_offsets) * attr_count;

    resources = allocate(arg->allocs.persistent, resources_size + draw_infos_size);
    draw_info = (struct draw_model_info*)((uchar*)resources + resources_size);

    memset(resources, 0, sizeof(*resources));
    memset(draw_info, 0, sizeof(*draw_info));

    resources->alloc = arg->allocs.persistent;
    resources->gpu = lmi->arg->gpu;

    resources->images               =    (struct gpu_texture*)(resources                      + 1);
    resources->samplers             =             (VkSampler*)(resources->images              + model->image_count);
    resources->texture_dsls         = (VkDescriptorSetLayout*)(resources->samplers            + model->sampler_count);
    resources->transforms_ubo_dsls  =                          resources->texture_dsls        + model->material_count;
    resources->pipelines            =            (VkPipeline*)(resources->transforms_ubo_dsls + model->mesh_count);
    resources->pipeline_layouts     =      (VkPipelineLayout*)(resources->pipelines           + pipeline_count);

    draw_info->pipelines = resources->pipelines;
    draw_info->pipeline_layouts = resources->pipeline_layouts;

    draw_info->mesh_instance_counts  =                             (uint*)(draw_info                        + 1);
    draw_info->mesh_primitive_counts =                                     draw_info->mesh_instance_counts  + model->mesh_count;
    draw_info->bind_buffers          =                         (VkBuffer*)(draw_info->mesh_primitive_counts + model->mesh_count);
    draw_info->primitive_infos       = (struct model_primitive_draw_info*)(draw_info->bind_buffers          + attr_count_upper_bound);

    for(uint i=0; i < attr_count_upper_bound; ++i)
        draw_info->bind_buffers[i] = lmi->arg->gpu->mem.bind_buffer.buf;

    resources->pipeline_count = pipeline_count;
    resources->pipeline_layout_count = prim_count + 1;

    uint ac = 0;
    uint pc = 0;
    for(uint i=0; i < model->mesh_count; ++i)
        for(uint j=0; j < model->meshes[i].primitive_count; ++j) {
            cnt = model->meshes[i].primitives[j].attribute_count;
            for(uint k=0; k < model->meshes[i].primitives[j].target_count; ++k)
                cnt += model->meshes[i].primitives[j].morph_targets[k].attribute_count;

            draw_info->primitive_infos[pc].vertex_offset_count = cnt;
            draw_info->primitive_infos[pc].vertex_offsets = (size_t*)(draw_info->primitive_infos + prim_count) + ac;

            ac += cnt;
            pc++;
        }

    lmi->ret->draw_info = draw_info;

    uint res = load_model(lmi->arg, lmi->ret, resources, &arg->allocs, draw_info);

    lmi->ret->result = res;

    // @Sync I wish I knew more about what atomics, fences, etc. guaranteed in terms of synchronisation.
    // Of course I can give the manual recitation exam answer, but I do not *understand* it. According
    // to my mental model, the below should work, and it does seem to.
    _mm_sfence();

    if (res != LOAD_MODEL_RESULT_SUCCESS)
        return;

    struct private_thread_work w = {
        .ready = lmi->ret->thread_cleanup_resources,
        .work.fn = cast_work_fn(model_cleanup_tf),
        .work.arg = cast_work_arg(resources),
    };
    thread_add_private_work(arg->self, &w);
}

static void* model_cleanup_tf(struct thread_work_arg *arg)
{
    struct model_resources *resources = arg->arg;
    model_cleanup(resources);
    return NULL;
}

static void model_cleanup(struct model_resources *resources)
{
    struct gpu *gpu = resources->gpu;

    vk_destroy_shader_module(gpu->device, resources->depth_shader_vert, GAC);
    vk_destroy_shader_module(gpu->device, resources->depth_shader_frag, GAC);

    // @TempShader
    vk_destroy_shader_module(gpu->device, resources->shader_modules[0], GAC);
    vk_destroy_shader_module(gpu->device, resources->shader_modules[1], GAC);

    for(uint i=0; i < resources->image_count; ++i)
        gpu_destroy_image_and_view(gpu, &resources->images[i]);

    for(uint i=0; i < resources->sampler_count; ++i)
        gpu_destroy_sampler(gpu, resources->samplers[i]);

    vk_destroy_descriptor_set_layout(gpu->device, resources->material_ubo_dsl, GAC);
    for(uint i=0; i < resources->texture_dsl_count; ++i)
        vk_destroy_descriptor_set_layout(gpu->device, resources->texture_dsls[i], GAC);

    for(uint i=0; i < resources->mesh_count; ++i)
        vk_destroy_descriptor_set_layout(gpu->device, resources->transforms_ubo_dsls[i], GAC);

    for(uint i=0; i < resources->pipeline_layout_count; ++i)
        vk_destroy_pipeline_layout(gpu->device, resources->pipeline_layouts[i], GAC);

    for(uint i=0; i < resources->pipeline_count; ++i)
        vk_destroy_pipeline(gpu->device, resources->pipelines[i], GAC);

    allocator *alloc = resources->alloc;
    deallocate(alloc, resources);
}

void draw_model_color(VkCommandBuffer cmd, struct draw_model_info *info)
{
    uint pc = 0;
    for(uint i=0; i < info->mesh_count; ++i)
        for(uint j=0; j < info->mesh_primitive_counts[i]; ++j) {
            vk_cmd_bind_pipeline(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    info->pipelines[pc]);

            // @Optimise It might be inefficient to reset all offsets (or it might be a nop
            // since I am setting offsets to what they already are), but I am not sure about
            // the compatibility of my pipeline layouts, need to check that passage again.
            vk_cmd_set_descriptor_buffer_offsets_ext(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    info->pipeline_layouts[pc],
                    0,
                    info->primitive_infos[pc].dsl_count,
                    info->primitive_infos[pc].db_indices,
                    info->primitive_infos[pc].db_offsets);

            vk_cmd_bind_vertex_buffers(cmd,
                    0,
                    info->primitive_infos[pc].vertex_offset_count,
                    info->bind_buffers,
                    info->primitive_infos[pc].vertex_offsets);

            if (info->primitive_infos[pc].draw_indexed) {

                vk_cmd_bind_index_buffer(cmd,
                     *info->bind_buffers,
                     info->primitive_infos[pc].index_offset,
                     info->primitive_infos[pc].index_type);

                vk_cmd_draw_indexed(cmd,
                                    info->primitive_infos[pc].draw_count,
                                    info->mesh_instance_counts[i],
                                    0, 0, 0);
            } else {
                vk_cmd_draw(cmd,
                            info->primitive_infos[pc].draw_count,
                            info->mesh_instance_counts[i],
                            0, 0);
            }

            pc++;
        }
}

void draw_model_depth(VkCommandBuffer cmd, struct draw_model_info *info, uint pass)
{
    uint pi = info->prim_count + (info->prim_count * pass);
    uint pc = 0;
    for(uint i=0; i < info->mesh_count; ++i)
        for(uint j=0; j < info->mesh_primitive_counts[i]; ++j) {
            vk_cmd_bind_pipeline(cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    info->pipelines[pi]);

            vk_cmd_bind_vertex_buffers(cmd,
                    0, 1,
                    info->bind_buffers,
                    info->primitive_infos[pc].vertex_offsets);

            if (info->primitive_infos[pc].draw_indexed) {

                vk_cmd_bind_index_buffer(cmd,
                     *info->bind_buffers,
                     info->primitive_infos[pc].index_offset,
                     info->primitive_infos[pc].index_type);

                vk_cmd_draw_indexed(cmd,
                                    info->primitive_infos[pc].draw_count,
                                    info->mesh_instance_counts[i],
                                    0, 0, 0);
            } else {
                vk_cmd_draw(cmd,
                            info->primitive_infos[pc].draw_count,
                            info->mesh_instance_counts[i],
                            0, 0);
            }

            pc++;
        }
}

static uint
allocate_model_resources(
    struct load_model_arg       *arg,
    struct load_model_ret       *ret,
    struct model_memory_offsets *offsets,
    struct model_resources      *resources,
    struct allocators           *allocs);

static struct model_texture_descriptors
get_model_texture_descriptors(
    struct load_model_arg  *arg,
    struct model_resources *resources,
    struct allocators      *allocs);

static struct model_material*
material_descriptors_and_pipeline_info(
    struct load_model_arg            *arg,
    struct model_texture_descriptors  textures,
    struct model_memory_offsets      *offsets,
    struct allocators                *allocs);

static uint
model_pipelines_transform_descriptors_and_draw_info(
    struct load_model_arg       *arg,
    struct model_resources      *resources,
    struct model_memory_offsets *offsets,
    struct model_material       *materials,
    struct allocators           *allocs,
    struct draw_model_info      *draw_info);

static void
model_node_transforms(
    struct load_model_arg       *arg,
    struct model_memory_offsets *offsets,
    struct allocators           *allocs);

static uint load_model(
    struct load_model_arg  *arg,
    struct load_model_ret  *ret,
    struct model_resources *resources,
    struct allocators      *allocs,
    struct draw_model_info *draw_info)
{
    // @Todo @Note Should allocation_mask be set or unset on failure?
    // Currently going for it will be set.
    ret->allocation_mask = 0x0;

    uint64 mark = allocator_used(allocs->temp);
    uint result = LOAD_MODEL_RESULT_SUCCESS;

    struct model_memory_offsets offsets;
    result = allocate_model_resources(arg, ret, &offsets, resources, allocs);
    if (result != LOAD_MODEL_RESULT_SUCCESS)
        goto fn_return;

    struct model_texture_descriptors texture_descriptors =
        get_model_texture_descriptors(arg, resources, allocs);

    struct model_material *materials =
        material_descriptors_and_pipeline_info(arg, texture_descriptors, &offsets, allocs);

    model_pipelines_transform_descriptors_and_draw_info(arg, resources, &offsets,
                                                        materials, allocs, draw_info);

    model_node_transforms(arg, &offsets, allocs);

fn_return: // goto label
    allocator_reset_linear_to(allocs->temp, mark);
    return result;
}

// @Optimise This function may be improved by being able to resume it from
// where it failed when more memory is available. Idk if this is really better
// than just sticking it back on the work queue though, keeping the threads
// dumb and just making sure that the scheduler is smart enough to not add so
// many jobs as to cause regular failure.
static uint allocate_model_resources(
    struct load_model_arg       *arg,
    struct load_model_ret       *ret,
    struct model_memory_offsets *offsets,
    struct model_resources      *resources,
    struct allocators           *allocs)
{
    gltf *model = arg->model;
    struct gpu *gpu = arg->gpu;
    uint result = LOAD_MODEL_RESULT_INCOMPLETE; // set on error

    void *offset_data = allocate(allocs->temp,
            sizeof(*offsets->buffers)                * model->buffer_count   +
            sizeof(*offsets->transforms_ubos)        * model->mesh_count     +
            sizeof(*offsets->transforms_ubo_dsls)    * model->mesh_count     +
            sizeof(*offsets->mesh_instance_counts)   * model->mesh_count     +
            sizeof(*offsets->material_textures_dsls) * model->material_count +
            sizeof(uint)                             * model->image_count * 2); // image offsets

    offsets->buffers                = offset_data;
    offsets->transforms_ubos        = offsets->buffers              + model->buffer_count;
    offsets->transforms_ubo_dsls    = offsets->transforms_ubos      + model->mesh_count;
    offsets->mesh_instance_counts   = offsets->transforms_ubo_dsls  + model->mesh_count;
    offsets->material_textures_dsls = offsets->mesh_instance_counts + model->mesh_count;

    uint *image_offsets_stage = offsets->material_textures_dsls + model->material_count;
    uint *image_offsets_device = image_offsets_stage            + model->image_count;

    smemset(offsets->mesh_instance_counts, 0,
           *offsets->mesh_instance_counts, model->mesh_count);
    for(uint i=0; i < arg->scene_count; ++i)
        for(uint j=0; j < model->scenes[arg->scenes[i]].node_count; ++j)
            gltf_count_mesh_instances(model->nodes,
                                      model->scenes[arg->scenes[i]].nodes[j],
                                      offsets->mesh_instance_counts);

    uint buffers_size = 0;
    for(uint i=0; i < model->buffer_count; ++i) {
        offsets->buffers[i] = buffers_size;
        buffers_size += model->buffers[i].byte_length;
    }

    uint transforms_ubo_dsls_size = 0;
    uint transforms_ubos_size     = 0;
    for(uint i=0; i < model->mesh_count; ++i) {
        offsets->transforms_ubos[i] = transforms_ubos_size + buffers_size;
        transforms_ubos_size += calculate_transforms_ubo_size(&model->meshes[i]) *
                                offsets->mesh_instance_counts[i];
        // @Optimise Check that the compiler knows to only zero the struct once
        // outside the loop.
        VkDescriptorSetLayoutBinding binding = {
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = offsets->mesh_instance_counts[i],
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        };
        VkDescriptorSetLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        VkResult check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC,
                &resources->transforms_ubo_dsls[i]);
        DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

        size_t size;
        vk_get_descriptor_set_layout_size_ext(gpu->device,
                resources->transforms_ubo_dsls[i], &size);

        size = align(size, gpu->descriptors.props.descriptorBufferOffsetAlignment);
        offsets->transforms_ubo_dsls[i] = transforms_ubo_dsls_size;
        transforms_ubo_dsls_size += size;

        resources->mesh_count++;
    }

    uint material_ubos_size;
    uint material_ubo_dsls_size;
/*  for(uint i=0; you == gay; ++i) */ { // @Bug Loops infinitely around 1 in every 10 runs...
        offsets->material_ubo = buffers_size + transforms_ubos_size;
        material_ubos_size = alloc_align(model->material_count * SHADER_MATERIAL_UBO_SIZE);

        VkDescriptorSetLayoutBinding binding = {
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        VkResult check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC,
                &resources->material_ubo_dsl);
        DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

        size_t size;
        vk_get_descriptor_set_layout_size_ext(gpu->device, resources->material_ubo_dsl, &size);

        size = align(size, gpu->descriptors.props.descriptorBufferOffsetAlignment);
        offsets->material_ubo_dsl = (struct pair_uint) {transforms_ubo_dsls_size,
                                                        transforms_ubo_dsls_size};
        material_ubo_dsls_size = size * model->material_count;
        offsets->material_ubo_dsl_stride = size;
    }

    uint material_textures_dsls_size = 0;
    resources->texture_dsl_count = 0;
    for(uint i=0; i < model->material_count; ++i) {
        if (!(model->materials[i].flags & GLTF_MATERIAL_TEXTURE_BITS))
            continue;

        // @Optimise Check that the compiler knows to only zero the struct once
        // outside the loop.
        VkDescriptorSetLayoutBinding binding = {
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = popcnt(model->materials[i].flags & GLTF_MATERIAL_TEXTURE_BITS),
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        VkResult check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC,
                &resources->texture_dsls[resources->texture_dsl_count]);
        DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

        size_t size;
        vk_get_descriptor_set_layout_size_ext(gpu->device,
                                              resources->texture_dsls[resources->texture_dsl_count],
                                              &size);

        size = align(size, gpu->descriptors.props.descriptorBufferOffsetAlignment);
        offsets->material_textures_dsls[i] = material_textures_dsls_size;
        material_textures_dsls_size += size;

        resources->texture_dsl_count++;
    }

    uint images_size_stage = 0;
    uint images_size_device = 0;
    uint images_base_alignment = 0;
    for(uint i=0; i < model->image_count; ++i) {
        struct image image = gltf_load_image(model, i);

        // @Optimise Might be better to create images after allocating to staging
        // buffer? Probably not, since stage allocation unlikely to fail.
        gpu_create_texture(gpu, &image, &resources->images[i]);
        struct memreq mr = gpu_texture_memreq(gpu, &resources->images[i]);

        if (!images_base_alignment)
            images_base_alignment = mr.alignment;

        images_size_device = align(images_size_device, mr.alignment);
        image_offsets_device[i] = images_size_device;
        images_size_device += mr.size;
        image_offsets_stage[i] = images_size_stage + (gpu->flags & GPU_UMA_BIT ? 0 :
                buffers_size + transforms_ubos_size + material_ubos_size);

        // Each image will require a bufcpy, so each should be aligned.
        images_size_stage += gpu_buffer_align(gpu, image_size(&image));

        resources->image_count++;
    }

    // Allocate image memory
    size_t base_image_device_offset = gpu_allocate_image_memory(gpu, images_size_device + images_base_alignment);
    if (base_image_device_offset == Max_u64) {
        result = LOAD_MODEL_RESULT_INSUFFICIENT_IMAGE_MEMORY;
        goto fail;
    } else {
        base_image_device_offset = align(base_image_device_offset, images_base_alignment);
    }
    ret->allocation_mask |= LOAD_MODEL_ALLOCATION_IMAGE_MEMORY_BIT;

    uint stage_size = images_size_stage;

    if (gpu->flags & GPU_UMA_BIT)
            stage_size += buffers_size + transforms_ubos_size + material_ubos_size;

    if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
        for(uint i=0; i < model->mesh_count; ++i)
            offsets->transforms_ubo_dsls[i] += stage_size;
        offsets->material_ubo_dsl.a += stage_size; // already contains transforms offset

        for(uint i=0; i < model->material_count; ++i)
            offsets->material_textures_dsls[i] += stage_size +
                                                  transforms_ubo_dsls_size +
                                                  material_ubo_dsls_size;
        stage_size += transforms_ubo_dsls_size +
                      material_ubo_dsls_size +
                      material_textures_dsls_size;
    }

    // Allocate stage memory
    offsets->base_stage = gpu_buffer_allocate(gpu, &gpu->mem.transfer_buffer, stage_size);
    if (offsets->base_stage == Max_u64) {
        result = LOAD_MODEL_RESULT_INSUFFICIENT_STAGE_MEMORY;
        goto fail;
    }
    ret->allocation_mask |= LOAD_MODEL_ALLOCATION_TRANSFER_BUFFER_BIT;

    // Allocate device memory
    uint bind_size = buffers_size + transforms_ubos_size + material_ubos_size;
    offsets->base_bind = gpu_buffer_allocate(gpu, &gpu->mem.bind_buffer, bind_size);
    if (offsets->base_bind == Max_u64) {
        result = LOAD_MODEL_RESULT_INSUFFICIENT_BIND_MEMORY;
        goto fail;
    }
    ret->allocation_mask |= LOAD_MODEL_ALLOCATION_BIND_BUFFER_BIT;

    // Allocate descriptor memory
    if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
        {
            offsets->base_descriptor_resource = gpu_buffer_allocate(
                    gpu, &gpu->mem.descriptor_buffer_resource,
                    transforms_ubo_dsls_size + material_ubo_dsls_size);
            if (offsets->base_descriptor_resource == Max_u64) {
                result = LOAD_MODEL_RESULT_INSUFFICIENT_RESOURCE_DESCRIPTOR_MEMORY;
                goto fail;
            }
            ret->allocation_mask |= LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_RESOURCE_BIT;

            VkBufferCopy bufcpy = {
                .size = transforms_ubo_dsls_size + material_ubo_dsls_size,
                .srcOffset = offsets->base_stage + offsets->transforms_ubo_dsls[0],
                .dstOffset = offsets->base_descriptor_resource,
            };
            struct range range = {bufcpy.dstOffset,bufcpy.size};
            gpu_upload_descriptor_buffer_resource(gpu, 1, &bufcpy, &range,
                    ret->cmd_transfer, ret->cmd_graphics);
        }
        if (material_textures_dsls_size) {
            offsets->base_descriptor_sampler = gpu_buffer_allocate(
                    gpu, &gpu->mem.descriptor_buffer_sampler,
                    material_textures_dsls_size);
            if (offsets->base_descriptor_sampler == Max_u64) {
                result = LOAD_MODEL_RESULT_INSUFFICIENT_SAMPLER_DESCRIPTOR_MEMORY;
                goto fail;
            }
            ret->allocation_mask |= LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_SAMPLER_BIT;

            VkBufferCopy bufcpy = {
                .size = material_textures_dsls_size,
                .srcOffset = offsets->base_stage + offsets->material_textures_dsls[0],
                .dstOffset = offsets->base_descriptor_sampler,
            };
            struct range range = {bufcpy.dstOffset,bufcpy.size};
            gpu_upload_descriptor_buffer_sampler(gpu, 1, &bufcpy, &range,
                    ret->cmd_transfer, ret->cmd_graphics);
        }
    } else {
        offsets->base_descriptor_resource = gpu_buffer_allocate(
                gpu, &gpu->mem.descriptor_buffer_resource,
                transforms_ubo_dsls_size + material_ubo_dsls_size);
        ret->allocation_mask |= LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_RESOURCE_BIT;

        if (offsets->base_descriptor_resource == Max_u64) {
            result = LOAD_MODEL_RESULT_INSUFFICIENT_RESOURCE_DESCRIPTOR_MEMORY;
            goto fail;
        }
        if (material_textures_dsls_size) {
            offsets->base_descriptor_sampler = gpu_buffer_allocate(
                    gpu, &gpu->mem.descriptor_buffer_sampler,
                    material_textures_dsls_size);
            ret->allocation_mask |= LOAD_MODEL_ALLOCATION_DESCRIPTOR_BUFFER_SAMPLER_BIT;

            if (offsets->base_descriptor_sampler == Max_u64) {
                result = LOAD_MODEL_RESULT_INSUFFICIENT_SAMPLER_DESCRIPTOR_MEMORY;
                goto fail;
            }
        }
    }

    // @Optimise This is slightly inefficient, as this will copy data which is not relevant
    // to the gpu, such as animation keyframes. There could be marks in the buffers which
    // indicate where the buffer data is that is useful, or mark buffer views. But this would
    // require reading and coagulating every mesh primitive attribute.
    if (gpu->flags & GPU_UMA_BIT) {
        for(uint i=0; i < model->buffer_count; ++i)
            gltf_read_buffer(model, i,
                             (char*)gpu->mem.bind_buffer.data + offsets->base_bind + offsets->buffers[i]);
    } else {
        for(uint i=0; i < model->buffer_count; ++i)
            gltf_read_buffer(model, i,
                             (char*)gpu->mem.transfer_buffer.data + offsets->base_stage + offsets->buffers[i]);
        VkBufferCopy bufcpy = {
            .srcOffset = offsets->base_stage,
            .dstOffset = offsets->base_bind,
            .size = bind_size,
        };
        struct range range = {bufcpy.dstOffset,bufcpy.size};
        gpu_upload_bind_buffer(gpu, 1, &bufcpy, &range, ret->cmd_transfer, ret->cmd_graphics);
    }

    for(uint i=0; i < model->image_count; ++i) {
        memcpy((char*)gpu->mem.transfer_buffer.data + offsets->base_stage + image_offsets_stage[i],
                resources->images[i].image.data, image_size(&resources->images[i].image));
        gpu_bind_image(gpu, resources->images[i].vkimage, base_image_device_offset + image_offsets_device[i]);
        gpu_create_texture_view(gpu, &resources->images[i]);
    }
    gpu_upload_images_with_base_offset(gpu, model->image_count, resources->images,
            offsets->base_stage, image_offsets_stage, ret->cmd_transfer, ret->cmd_graphics);
    // This also transfers the image layout to shader read only.
    gpu_blit_gltf_texture_mipmaps(model, resources->images, ret->cmd_graphics);

    for(uint i=0; i < model->sampler_count; ++i) {
        resources->samplers[i] = gpu_create_gltf_sampler(gpu, &model->samplers[i]);
        if (!resources->samplers[i]) {
            result = LOAD_MODEL_RESULT_EXCEEDED_MAX_SAMPLER_COUNT;
            goto fail;
        }
        resources->sampler_count++;
    }

    return LOAD_MODEL_RESULT_SUCCESS;

fail: // goto label
    model_cleanup(resources);
    return result;
}

static struct model_texture_descriptors
get_model_texture_descriptors(
    struct load_model_arg  *arg,
    struct model_resources *resources,
    struct allocators      *allocs)
{
    gltf *model = arg->model;
    struct gpu *gpu = arg->gpu;

    struct model_texture_descriptors ret;
    ret.stride = gpu->descriptors.props.combinedImageSamplerDescriptorSize;
    ret.data = allocate(allocs->temp, ret.stride * model->texture_count);

    for(uint i=0; i < model->texture_count; ++i) {
        // @Optimise I made a comment on this elsewhere in this file:
        // can the compiler figure out which fields I am setting to
        // the same value every loop and lift it, and only zero the
        // struct once (this syntax zeros other fields), or will it
        // zero the struct everytime, and store to imageLayout everytime?
        VkDescriptorImageInfo ii = {
            .sampler = resources->samplers[model->textures[i].sampler],
            .imageView = resources->images[model->textures[i].source].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorGetInfoEXT gi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .data.pCombinedImageSampler = &ii,
        };
        vk_get_descriptor_ext(gpu->device, &gi, ret.stride, ret.data + ret.stride*i);
    }
    return ret;
}

#if DEBUG
    #define GLTF_MATERIAL_TEXTURE_INFOS_HAVE_NOT_BEEN_REORDERED_CHECK() \
        { \
            log_print_error_if(GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT != 1, "gltf_material texture flag bits have changed"); \
            log_print_error_if(ctz(GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT)   - ctz(GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT)         != 1, "gltf_material flag bits have changed"); \
            log_print_error_if(ctz(GLTF_MATERIAL_NORMAL_TEXTURE_BIT)               - ctz(GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT) != 1, "gltf_material flag bits have changed"); \
            log_print_error_if(ctz(GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT)            - ctz(GLTF_MATERIAL_NORMAL_TEXTURE_BIT)             != 1, "gltf_material flag bits have changed"); \
            log_print_error_if(ctz(GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT)             - ctz(GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT)          != 1, "gltf_material flag bits have changed"); \
            log_print_error_if(offsetof(gltf_material, metallic_roughness) - offsetof(gltf_material, base_color)         != sizeof(gltf_texture_info), "gltf_material texture infos were reordered"); \
            log_print_error_if(offsetof(gltf_material, normal)             - offsetof(gltf_material, metallic_roughness) != sizeof(gltf_texture_info), "gltf_material texture infos were reordered"); \
            log_print_error_if(offsetof(gltf_material, occlusion)          - offsetof(gltf_material, normal)             != sizeof(gltf_texture_info), "gltf_material texture infos were reordered"); \
            log_print_error_if(offsetof(gltf_material, emissive)           - offsetof(gltf_material, occlusion)          != sizeof(gltf_texture_info), "gltf_material texture infos were reordered"); \
        }
#else
    #define GLTF_MATERIAL_TEXTURE_INFOS_HAVE_NOT_BEEN_REORDERED_CHECK()
#endif

// @Todo @ColorBlending gltf materials can define alpha coverage as "BLEND".
// Basic blend operations in vulkan are add and subtract, but a 'VK_OP_BLEND'
// can be added by enabling advanced blending. This is something I want to look
// at and would be set up in this function.
//
// @Optimise Currently this function assumes one color attachment and satisfies
// this with a reference to a global in the blend_types.c source file. To me
// this seems better than all the movs to fill in the different blend types
// each time the function is called, plus the memcpys into memory persistent
// outside of the scope. In my head this is fine as memory is memory, but STB
// said smtg about global accesses being funny in the speed discussion with Jon
// Blow. I need to find the bit again to better understand what he was saying.
// My other option is to move the blend attachments into the calling function
// so that it is on the stack and will not go out of scope, but this only
// eliminates the memcpys, not all the stores to initialize the structs on the
// call of the calling function... In hh, Casey says memory is memory, and
// other than NUMA and thread local and stuff, I think that that is largely
// true. But idk, I need to better check what STB was saying.
static struct model_material* material_descriptors_and_pipeline_info(
    struct load_model_arg            *arg,
    struct model_texture_descriptors  textures,
    struct model_memory_offsets      *offsets,
    struct allocators                *allocs)
{
    struct gpu *gpu = arg->gpu;
    gltf *model = arg->model;

    struct model_material *ret = sallocate(allocs->temp, *ret, model->material_count);
    smemset(ret, 0, *ret, model->material_count);

    uchar *ubo_data;
    if (gpu->flags & GPU_UMA_BIT)
        ubo_data = gpu->mem.bind_buffer.data + offsets->base_bind + offsets->material_ubo;
    else
        ubo_data = gpu->mem.transfer_buffer.data + offsets->base_stage + offsets->material_ubo;

     uchar *ubo_dsl_data,*texture_dsl_data;
    if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
        ubo_dsl_data = gpu->mem.transfer_buffer.data +
                       offsets->base_stage +
                       offsets->material_ubo_dsl.a;

        // specific dsl offsets added when used
        texture_dsl_data = gpu->mem.transfer_buffer.data +
                           offsets->base_stage;
    } else {
        ubo_dsl_data = gpu->mem.descriptor_buffer_resource.data +
                       offsets->base_descriptor_resource +
                       offsets->material_ubo_dsl.b;

        // specific dsl offsets added when used
        texture_dsl_data = gpu->mem.descriptor_buffer_sampler.data +
                           offsets->base_descriptor_sampler;
    }

    for(uint i=0; i < model->material_count; ++i) {
        VkDescriptorAddressInfoEXT ai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
            .address = gpu->mem.bind_buffer.address +
                       offsets->base_bind +
                       offsets->material_ubo +
                       SHADER_MATERIAL_UBO_SIZE*i,
            .range = SHADER_MATERIAL_UBO_SIZE,
        };
        VkDescriptorGetInfoEXT gi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .data.pUniformBuffer = &ai,
        };
        vk_get_descriptor_ext(gpu->device, &gi,
                gpu->descriptors.props.uniformBufferDescriptorSize,
                ubo_dsl_data + i*offsets->material_ubo_dsl_stride);
    }

    for(uint i=0; i < model->material_count; ++i) {
        uint f = model->materials[i].flags;
        ret[i].flags |= MODEL_MATERIAL_BLEND_BIT     &  maxif(f & GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT);
        ret[i].flags |= MODEL_MATERIAL_CULL_BACK_BIT & zeroif(f & GLTF_MATERIAL_DOUBLE_SIDED_BIT);
        ret[i].flags |= MODEL_MATERIAL_TEXTURED_BIT  &  maxif(f & GLTF_MATERIAL_TEXTURE_BITS);

        assert(SHADER_MATERIAL_UBO_SIZE == 48);
        memcpy(ubo_data + i * SHADER_MATERIAL_UBO_SIZE, &model->materials[i].uniforms, sizeof(model->materials[i].uniforms));

        // The below code requires the gltf_material struct to act as an array
        // of texture infos with each texture's corresponding flag bit at the
        // same index. This method (with the check) is more robust and more
        // compact than copy and pasting the same memcpy code for each texture
        // field.
        GLTF_MATERIAL_TEXTURE_INFOS_HAVE_NOT_BEEN_REORDERED_CHECK();

        f &= GLTF_MATERIAL_TEXTURE_BITS;
        uint pc = popcnt(f);
        gltf_texture_info *texture_infos = &model->materials[i].base_color;

        for(uint j=0; j < pc; ++j) {
            uint tz = ctz(f);
            f &= ~(1<<tz);

            ds_cis_arrcpy(gpu, texture_dsl_data + offsets->material_textures_dsls[i], j, pc,
                          textures.data + textures.stride * texture_infos[tz].texture);

            // no longer need the stage offset
            if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT)
                offsets->material_textures_dsls[i] -= offsets->material_textures_dsls[0];
        }
    }
    return ret;
}

// @Todo Geometry shader? When I get point lights up, but that will likely be long in the future.
struct model_shaders { // @Note This struct is cast to an array for the pipeline create info.
    VkPipelineShaderStageCreateInfo vertex;
    VkPipelineShaderStageCreateInfo fragment;
};

static uint model_vertex_state_and_draw_info(
    struct load_model_arg                  *arg,
    struct model_resources                 *resources,
    struct model_memory_offsets            *offsets,
    struct model_material                  *materials,
    uint                                    mesh_i,
    uint                                    prim_i,
    struct model_primitive_draw_info       *ret,
    VkDescriptorSetLayout                  *dsl_buf,
    VkPipelineVertexInputStateCreateInfo   *vi,
    VkPipelineInputAssemblyStateCreateInfo *ia);

void draw_model_color(VkCommandBuffer cmd, struct draw_model_info *info);

static uint
model_pipelines_transform_descriptors_and_draw_info(
    struct load_model_arg       *arg,
    struct model_resources      *resources,
    struct model_memory_offsets *offsets,
    struct model_material       *materials,
    struct allocators           *allocs,
    struct draw_model_info      *draw_info)
{
    struct gpu *gpu = arg->gpu;
    gltf *model = arg->model;

    draw_info->mesh_count = model->mesh_count;

    // @Note The descriptor offset code in the loop is particularly brutal to ensure is correct.
    uint pc = 0;
    uint ac = 0;
    uint descriptor_size = gpu->descriptors.props.uniformBufferDescriptorSize;
    for(uint i=0; i < model->mesh_count; ++i) {
        pc += model->meshes[i].primitive_count;

        for(uint j=0; j < model->meshes[i].primitive_count; ++j) {
            ac += model->meshes[i].primitives[j].attribute_count;
            for(uint k=0; k < model->meshes[i].primitives[j].target_count; ++k)
                ac += model->meshes[i].primitives[j].morph_targets[k].attribute_count;
        }

        draw_info->mesh_primitive_counts[i] = model->meshes[i].primitive_count;
        draw_info->mesh_instance_counts[i] = offsets->mesh_instance_counts[i];

        uint ubo_size = calculate_transforms_ubo_size(&model->meshes[i]);
        // @Optimise Check what this loop compiles to and whether stuff gets lifted out properly.
        for(uint j=0; j < offsets->mesh_instance_counts[i]; ++j) {
            size_t ubo_offset = offsets->base_bind + offsets->transforms_ubos[i] + ubo_size*j;

            VkDescriptorAddressInfoEXT bai = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                .address = gpu->mem.bind_buffer.address + ubo_offset,
                .range = ubo_size,
            };
            VkDescriptorGetInfoEXT gi = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .data.pUniformBuffer = &bai,
            };

            if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
                size_t dsl_offset = offsets->base_stage +
                                    offsets->transforms_ubo_dsls[i] +
                                    descriptor_size * j;
                vk_get_descriptor_ext(gpu->device, &gi, descriptor_size,
                                      gpu->mem.transfer_buffer.data + dsl_offset);
            } else {
                size_t dsl_offset = offsets->base_descriptor_resource +
                                    offsets->transforms_ubo_dsls[i] +
                                    descriptor_size * j;
                vk_get_descriptor_ext(gpu->device, &gi, descriptor_size,
                                      gpu->mem.descriptor_buffer_resource.data + dsl_offset);
            }
        }
        // no longer require stage offset if it was there.
        if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT)
            offsets->transforms_ubo_dsls[i] -= offsets->transforms_ubo_dsls[0];
    }

    draw_info->prim_count = pc;

    {
        struct file f = file_read_bin_all("shaders/depth.vert.spv", allocs->temp);
        VkShaderModuleCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = f.size,
            .pCode = (const uint32*)f.data,
        };
        VkResult check = vk_create_shader_module(gpu->device, &ci, GAC, &resources->depth_shader_vert);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);
    } {
        struct file f = file_read_bin_all("shaders/depth.frag.spv", allocs->temp);
        VkShaderModuleCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = f.size,
            .pCode = (const uint32*)f.data,
        };
        VkResult check = vk_create_shader_module(gpu->device, &ci, GAC, &resources->depth_shader_frag);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);
    }

    VkPipelineShaderStageCreateInfo depth_shaders[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = resources->depth_shader_vert,
            .pName = SHADER_ENTRY_POINT,
        }, {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = resources->depth_shader_frag,
            .pName = SHADER_ENTRY_POINT,
        },
    };

    VkPipelineViewportStateCreateInfo color_viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &arg->viewport,
        .scissorCount = 1,
        .pScissors = &arg->scissor,
    };

    VkViewport dvp = {
        .width = gpu->settings.shadow_maps.width,
        .height = gpu->settings.shadow_maps.height,
        .minDepth = 0,
        .maxDepth = 1,
    };

    VkRect2D dsci = {
        .extent = (VkExtent2D) {.width  = gpu->settings.shadow_maps.width,
                                .height = gpu->settings.shadow_maps.height,}
    };

    VkPipelineViewportStateCreateInfo depth_viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &dvp,
        .scissorCount = 1,
        .pScissors = &dsci,
    };


    // @Todo This is supposed to be an 'over' operator blend. I am assuming
    // that that is the same as the vulkan VK_BLEND_OVER.
    //
    // @Todo depth biasing. I did some brief testing as I could see shadow
    // acne, but it did not seem to do anything. Idk how it is supposed to
    // interact with sampling in the shader calculations.
    VkPipelineRasterizationStateCreateInfo color_rasterization[2] = {
        (VkPipelineRasterizationStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_LINE & maxif(arg->flags & LOAD_MODEL_WIREFRAME_BIT),
            .cullMode = VK_CULL_MODE_NONE,
            .lineWidth = 1.0f,
        },
        (VkPipelineRasterizationStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_LINE & maxif(arg->flags & LOAD_MODEL_WIREFRAME_BIT),
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .lineWidth = 1.0f,
        },
    };

    VkPipelineRasterizationStateCreateInfo depth_rasterization = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_LINE & maxif(arg->flags & LOAD_MODEL_WIREFRAME_BIT),
            .cullMode = VK_CULL_MODE_FRONT_BIT,
            .lineWidth = 1.0f,

            .depthBiasEnable = VK_TRUE,
            .depthBiasConstantFactor = 1.005,
            .depthBiasSlopeFactor = 1.1,
            // .depthBiasClamp = 1,
    };

    // @Todo I want to look at multisampling. Idk how important it is for a good image.
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo color_depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = 1,
        .depthWriteEnable = 1, // @DepthPass @ForwardPlus
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    VkPipelineDepthStencilStateCreateInfo depth_depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = 1,
        .depthWriteEnable = 1,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    // @Optimise These blend attachments are references to globals, idk if that
    // is slow. STB mentioned smtg weird about it...
    VkPipelineColorBlendStateCreateInfo blend[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &COLOR_BLEND_NONE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &COLOR_BLEND_ALPHA,
        }
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 0,
    };

    struct model_shaders *shaders;
    VkPipelineVertexInputStateCreateInfo *vi;
    VkPipelineInputAssemblyStateCreateInfo *ia;
    VkVertexInputBindingDescription *vb;
    VkVertexInputAttributeDescription *va;

    VkGraphicsPipelineCreateInfo *pipeline_infos = allocate(allocs->temp,
            sizeof(*pipeline_infos) * pc * (arg->depth_pass_count + 1) +
            sizeof(*shaders)        * pc * 1                           +
            sizeof(*vi)             * pc * 2                           +
            sizeof(*ia)             * pc * 2                           +
            sizeof(*vb)             * ac * 1                           +
            sizeof(*va)             * ac * 1);

    shaders =                   (struct model_shaders*)(pipeline_infos + pc * (arg->depth_pass_count + 1));
    vi      =   (VkPipelineVertexInputStateCreateInfo*)(shaders        + pc * 1);
    ia      = (VkPipelineInputAssemblyStateCreateInfo*)(vi             + pc * 2);
    vb      =        (VkVertexInputBindingDescription*)(ia             + pc * 2);
    va      =      (VkVertexInputAttributeDescription*)(vb             + ac * 1);

    VkShaderModule mod_v;
    VkShaderModule mod_f;
    {
        struct file f = file_read_bin_all("shaders/manual.vert.spv", allocs->temp);
        VkShaderModuleCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = f.size,
            .pCode = (const uint32*)f.data,
        };
        VkResult check = vk_create_shader_module(gpu->device, &ci, GAC, &mod_v);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);

        // @TempShader
        resources->shader_modules[0] = mod_v;
    } {
        struct file f = file_read_bin_all("shaders/manual.frag.spv", allocs->temp);
        VkShaderModuleCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = f.size,
            .pCode = (const uint32*)f.data,
        };
        VkResult check = vk_create_shader_module(gpu->device, &ci, GAC, &mod_f);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);

        resources->shader_modules[1] = mod_f;
    }

    {
        #if 0 // @RemoveMe Broke up push constants for testing the matrices in the shadow/depth shaders.
        VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(matrix),
        };
        VkPipelineLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
        };
        VkResult check = vk_create_pipeline_layout(gpu->device, &ci, GAC, &resources->pipeline_layouts[pc]);
        DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);
        #else
        VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(matrix) * 3,
        };
        VkPipelineLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
        };
        VkResult check = vk_create_pipeline_layout(gpu->device, &ci, GAC, &resources->pipeline_layouts[pc]);
        DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);
        #endif
    }

    VkDescriptorSetLayout dsl_buf[SHADER_MAX_DESCRIPTOR_SET_COUNT];
    pc = 0;
    ac = 0;
    for(uint i=0; i < model->mesh_count; ++i) // color pipelines
        for(uint j=0; j < model->meshes[i].primitive_count; ++j) {
            gltf_mesh_primitive *prim = &model->meshes[i].primitives[j];

            shaders[pc] = (struct model_shaders) {
                .vertex = (VkPipelineShaderStageCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = mod_v,// gpu->shader_dir.sets[prim->shader_i].vert,
                    .pName = SHADER_ENTRY_POINT,
                },
                .fragment = (VkPipelineShaderStageCreateInfo) {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = mod_f, // gpu->shader_dir.sets[prim->shader_i].frag,
                    .pName = SHADER_ENTRY_POINT,
                },
            };

            vi[pc].pVertexBindingDescriptions   = vb + ac;
            vi[pc].pVertexAttributeDescriptions = va + ac;

            ac += model_vertex_state_and_draw_info(arg, resources, offsets, materials, i, j,
                                                  &draw_info->primitive_infos[pc], dsl_buf,
                                                  vi + pc, ia + pc);
            assert(dsl_buf[arg->dsl_count] == resources->transforms_ubo_dsls[i]);

            VkPipelineLayoutCreateInfo plc = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = draw_info->primitive_infos[pc].dsl_count,
                .pSetLayouts = dsl_buf,
            };
            VkResult check = vk_create_pipeline_layout(gpu->device, &plc, GAC, &resources->pipeline_layouts[pc]);
            DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);

            pipeline_infos[pc] = (VkGraphicsPipelineCreateInfo) {
                .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .flags               = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                .stageCount          = 2, // @ShaderCount
                .pStages             = (VkPipelineShaderStageCreateInfo*)&shaders[pc],
                .pVertexInputState   = &vi[pc],
                .pInputAssemblyState = &ia[pc],
                .pViewportState      = &color_viewport,
                .pRasterizationState = &color_rasterization[flag_check(materials[prim->material].flags, MODEL_MATERIAL_CULL_BACK_BIT)],
                .pMultisampleState   = &multisample,
                .pDepthStencilState  = &color_depth,
                .pColorBlendState    = &blend[flag_check(materials[prim->material].flags, MODEL_MATERIAL_BLEND_BIT)],
                .pDynamicState       = &dyn,
                .layout              = resources->pipeline_layouts[pc],
                .renderPass          = arg->color_renderpass,
                .subpass             = arg->color_subpass,
            };
            pc++;
        }

    uint prim_count = pc;
    for(uint i=0; i < model->mesh_count; ++i) // depth pipelines initial
        for(uint j=0; j < model->meshes[i].primitive_count; ++j) {
            gltf_mesh_primitive *prim = &model->meshes[i].primitives[j];

            vi[pc] = vi[pc - prim_count];
            vi[pc].vertexBindingDescriptionCount = 1; // only position
            vi[pc].vertexAttributeDescriptionCount = 1;

            ia[pc] = ia[pc - prim_count];

            pipeline_infos[pc] = (VkGraphicsPipelineCreateInfo) {
                .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .flags               = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                .stageCount          = 2, // @ShaderCount
                .pStages             = depth_shaders,
                .pVertexInputState   = &vi[pc],
                .pInputAssemblyState = &ia[pc],
                .pViewportState      = &depth_viewport,
                .pRasterizationState = &depth_rasterization, // pipeline_infos[pc - prim_count].pRasterizationState,
                .pMultisampleState   = &multisample,
                .pDepthStencilState  = &depth_depth,
                .pColorBlendState    = pipeline_infos[pc - prim_count].pColorBlendState,
                .pDynamicState       = &dyn,
                .layout              = resources->pipeline_layouts[prim_count],
                .renderPass          = arg->depth_renderpass,
                .subpass             = 0,
            };
            pc++;
        }

    for(uint j=1; j < arg->depth_pass_count; ++j) // depth pipelines copy out
        for(uint i=0; i < prim_count; ++i) {
            pipeline_infos[pc] = pipeline_infos[prim_count];
            pipeline_infos[pc].subpass = j;
            pc++;
        }

    VkResult check = vk_create_graphics_pipelines(gpu->device, gpu->pipeline_cache, pc, pipeline_infos,
                                                  GAC, resources->pipelines);
    DEBUG_VK_OBJ_CREATION(vkCreateGraphicsPipelines, check);

    return pc;
}

#if DEBUG
    #define DESCRIPTOR_SET_ORDER_CHECK() \
        do { \
            log_print_error_if(DESCRIPTOR_SET_ORDER_MATERIAL_UBO      - DESCRIPTOR_SET_ORDER_TRANSFORMS_UBO != 1, "Descriptor sets were reordered"); \
            log_print_error_if(DESCRIPTOR_SET_ORDER_MATERIAL_TEXTURES - DESCRIPTOR_SET_ORDER_MATERIAL_UBO   != 1, "Descriptor sets were reordered"); \
        } while(0)
#else
    #define DESCRIPTOR_SET_ORDER_CHECK()
#endif

inline static size_t model_get_accessor_ofs(
    struct gpu                  *gpu,
    gltf                        *model,
    uint                         accessor_i,
    struct model_memory_offsets *offsets)
{
    gltf_accessor *acc = &model->accessors[accessor_i];
    gltf_buffer_view *bv = &model->buffer_views[acc->buffer_view];
    uint buf = offsets->buffers[bv->buffer];
    if (gpu->flags & GPU_UMA_BIT)
        return offsets->base_bind + buf + bv->byte_offset + acc->byte_offset;
    else
        return offsets->base_stage + buf + bv->byte_offset + acc->byte_offset;
}

static uint model_vertex_state_and_draw_info(
    struct load_model_arg                  *arg,
    struct model_resources                 *resources,
    struct model_memory_offsets            *offsets,
    struct model_material                  *materials,
    uint                                    mesh_i,
    uint                                    prim_i,
    struct model_primitive_draw_info       *ret,
    VkDescriptorSetLayout                  *dsl_buf,
    VkPipelineVertexInputStateCreateInfo   *vi,
    VkPipelineInputAssemblyStateCreateInfo *ia)
{
    gltf *model = arg->model;
    gltf_mesh_primitive *prim = &model->meshes[mesh_i].primitives[prim_i];

    if (prim->indices != Max_u32) {
        ret->draw_indexed = true;
        ret->draw_count = model->accessors[prim->indices].count;
        ret->index_offset = model_get_accessor_ofs(arg->gpu, model, prim->indices, offsets);
        assert(VK_INDEX_TYPE_UINT32 == 1 && VK_INDEX_TYPE_UINT16 == 0);
        ret->index_type = flag_check(model->accessors[prim->indices].flags, GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT);
    } else {
        ret->draw_indexed = false;
        ret->draw_count = model->accessors[prim->attributes[0].accessor].count;
        assert(prim->attributes[0].type == GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION);
    }

    for(uint i=0; i < arg->dsl_count; ++i) {
        ret->db_offsets[i] = arg->db_offsets[i];
        ret->db_indices[i] = arg->db_indices[i];
        dsl_buf[i] = arg->dsls[i];
    }
    ret->dsl_count = arg->dsl_count;

    // @Robustness The below feels quite fragile. It is not automatically
    // synced well with the auto shader generation. Plus its relying on this
    // check which is fine, but then the setting of the indices itself is error
    // prone: it would be easy to choose the wrong index or offset from the
    // wrong field. The best thing would be to somehow make this a loop.
    DESCRIPTOR_SET_ORDER_CHECK();

    // @Note dsl offsets have already had their stage offset removed if it was there.
    ret->db_indices[ret->dsl_count] = DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX;
    ret->db_offsets[ret->dsl_count] = offsets->base_descriptor_resource +
                                      offsets->transforms_ubo_dsls[mesh_i];
    dsl_buf[ret->dsl_count] = resources->transforms_ubo_dsls[mesh_i];
    ret->dsl_count++;

    if (prim->material != Max_u32) {
        ret->db_indices[ret->dsl_count] = DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX;
        ret->db_offsets[ret->dsl_count] = offsets->base_descriptor_resource +
                                          offsets->material_ubo_dsl.b +
                                          offsets->material_ubo_dsl_stride * prim->material;
        dsl_buf[ret->dsl_count] = resources->material_ubo_dsl;
        ret->dsl_count++;

        ret->db_indices[ret->dsl_count] = DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX;
        ret->db_offsets[ret->dsl_count] = offsets->base_descriptor_sampler +
                                          offsets->material_textures_dsls[prim->material];
        // @Note Maybe this can read out of bounds, but I doubt it will ever segfault.
        dsl_buf[ret->dsl_count] = resources->texture_dsls[prim->material];
        ret->dsl_count += flag_check(materials[prim->material].flags, MODEL_MATERIAL_TEXTURED_BIT);
    }

    *ia = (VkPipelineInputAssemblyStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = (VkPrimitiveTopology)prim->topology,
    };

    VkVertexInputBindingDescription *bindings = (VkVertexInputBindingDescription*)vi->pVertexBindingDescriptions;
    VkVertexInputAttributeDescription *attrs = (VkVertexInputAttributeDescription*)vi->pVertexAttributeDescriptions;

    uint ac = 0;
    for(uint i=0; i < prim->attribute_count; ++i) {
        gltf_accessor *accessor = &model->accessors[prim->attributes[i].accessor];
        gltf_buffer_view *buffer_view = &model->buffer_views[accessor->buffer_view];

        ret->vertex_offsets[ac] = offsets->base_bind +
                                  offsets->buffers[buffer_view->buffer] +
                                  buffer_view->byte_offset +
                                  accessor->byte_offset;
        bindings[ac] = (VkVertexInputBindingDescription) {
            .binding = ac,
            .stride = buffer_view->byte_stride ? buffer_view->byte_stride : accessor->byte_stride, // Why can this not be zero...? Why bother with the format??
        };

        attrs[ac] = (VkVertexInputAttributeDescription) {
            .location = ac,
            .binding = ac,
            .format = accessor->vkformat,
        };
        ac++;
    }
    for(uint j=0; j < prim->target_count; ++j) {
        gltf_mesh_primitive_attribute *attributes = prim->morph_targets[j].attributes;
        for(uint i=0; i < prim->morph_targets[j].attribute_count; ++i) {
            gltf_accessor *accessor = &model->accessors[attributes[i].accessor];
            gltf_buffer_view *buffer_view = &model->buffer_views[accessor->buffer_view];

            ret->vertex_offsets[ac] = offsets->base_bind +
                                      offsets->buffers[buffer_view->buffer] +
                                      buffer_view->byte_offset +
                                      accessor->byte_offset;
            bindings[ac] = (VkVertexInputBindingDescription) {
                .binding = ac,
                .stride = buffer_view->byte_stride,
            };
            attrs[ac] = (VkVertexInputAttributeDescription) {
                .location = ac,
                .binding = ac,
                .format = accessor->vkformat,
            };
            ac++;
        }
    }

    vi->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi->vertexBindingDescriptionCount   = ac;
    vi->vertexAttributeDescriptionCount = ac;

    return ac;
}

struct model_animations_arg {
    matrix *xforms;
    uint   *weight_offsets;
    float  *weight_data;
};

struct model_animation_masks {
    uint64 xforms[GLTF_U64_NODE_MASK];
    uint64 weights[GLTF_U64_NODE_MASK];
};

#if GLTF_U64_SKIN_MASK > 1
    #error The below member 'struct model_scene_meshes.skin_mask' needs to be an array
#endif

struct model_scene_meshes {
    uint64 skin_mask;
    uint64 node_mask[GLTF_U64_NODE_MASK];
};

struct model_build_transform_ubo_arg {
    gltf                      *model;
    uchar                     *ubo_data;
    uint64                    *weight_anim_mask;
    matrix                    *xforms;
    uint                      *weight_offsets;
    float                     *weight_data;
    struct model_scene_meshes *meshes;
};

static void model_animations(
    struct load_model_arg        *lm_arg,
    struct model_memory_offsets  *offsets,
    struct model_animations_arg  *arg,
    struct model_animation_masks *ret);

static void model_build_transform_ubo(
    uint                                  mesh,
    struct model_build_transform_ubo_arg *arg);

// @Todo TBN matrices.
//
// @Optimise This is a lot of arguments for a function that I
// want to be lightning... Maybe it would be faster to visit
// fields in pieces, so one pass does the transforms, and a
// different pass does the weights?
static inline void model_node_global_transforms(
    uint64                     xforms_mask[GLTF_U64_NODE_MASK],
    matrix                    *anim_xforms,
    matrix                    *global_xforms,
    struct model_scene_meshes *meshes,
    matrix                    *parent_xform,
    gltf_node                 *nodes,
    uint                       node,
    uint64                    *mesh_mask)
{
    uint64 one = 1;

    // @Optimise It would be really nice to remove all these branches. Maybe the setup
    // of the data layout before this function can be better.
    if (xforms_mask[node>>6] & (one << (node & 63)))
        mul_matrix(parent_xform, &anim_xforms[node], &global_xforms[node]);
    else
        if (nodes[node].flags & GLTF_NODE_MATRIX_BIT) {
            mul_matrix(parent_xform, &nodes[node].mat, &global_xforms[node]);
        } else if (nodes[node].flags & GLTF_NODE_TRS_BIT) {
            convert_trs(&nodes[node].trs, &global_xforms[node]);
            mul_matrix(parent_xform, &global_xforms[node], &global_xforms[node]);
        } else {
            copy_matrix(&global_xforms[node], parent_xform);
        }

    log_print_error_if(nodes[node].mesh == Max_u32 && nodes[node].skin != Max_u32,
                       "node declares a skin but not a mesh.");

    uint64 mesh_test = maxif(nodes[node].mesh + 1);
    uint64 skin_test = maxif(nodes[node].skin + 1);
    *mesh_mask |= (one & mesh_test) << (nodes[node].mesh & mesh_test);

    struct model_scene_meshes *mesh = &meshes[nodes[node].mesh & mesh_test];
    mesh->node_mask[node>>6] |= (one & mesh_test) << (node & 63);
    mesh->skin_mask |= (one & skin_test) << (nodes[node].skin & skin_test);

    for(uint i=0; i < nodes[node].child_count; ++i)
        model_node_global_transforms(xforms_mask, anim_xforms, global_xforms, meshes,
                &global_xforms[node], nodes, nodes[node].children[i], mesh_mask);
}

static void model_node_transforms(
    struct load_model_arg       *arg,
    struct model_memory_offsets *offsets,
    struct allocators           *allocs)
{
    struct gpu *gpu = arg->gpu;
    gltf *model = arg->model;

    struct model_scene_meshes *scene_meshes;
    uint *mesh_counts;
    uint *weight_offsets = allocate_and_zero(allocs->temp,
                                             sizeof(*weight_offsets) * model->node_count +
                                             sizeof(*scene_meshes) * model->mesh_count +
                                             sizeof(*mesh_counts) * model->mesh_count);
    scene_meshes = (struct model_scene_meshes*)(weight_offsets + model->node_count);
    mesh_counts = (uint*)(scene_meshes + model->mesh_count);

    uint weight_count = 0;
    for(uint i=0; i < model->node_count; ++i) {
        weight_offsets[i] = weight_count;
        weight_count += model->nodes[i].weight_count;
    }

    float *weight_data;
    matrix *global_xforms;
    matrix *anim_xforms = allocate(allocs->temp, sizeof(*anim_xforms) * model->node_count +
                                                 sizeof(*global_xforms) * model->node_count +
                                                 sizeof(*weight_data) * weight_count);
    global_xforms = anim_xforms + model->node_count;
    weight_data = (float*)(global_xforms + model->node_count);

    // @Todo This memset is to be able to sum animated weights together. I
    // think that this is the correct behaviour, but now I think about it it
    // might instead be to multiply? Not sure.
    smemset(weight_data, 0, *weight_data, weight_count);

    // @Todo I do not know what I meant by the below comment.
    // @Optimise The compiler might be copying the pointers here rather than
    // just optimising out anim_arg. It seems to pretty aggressively optimise
    // out even on -Og so I assume its fine.
    struct model_animations_arg anim_arg = {
        .xforms = anim_xforms,
        .weight_offsets = weight_offsets,
        .weight_data = weight_data,
    };
    struct model_animation_masks anim_masks;
    count_identity_matrix(model->node_count, anim_arg.xforms);
    model_animations(arg, offsets, &anim_arg, &anim_masks);

    struct model_build_transform_ubo_arg ubo_build_arg;
    ubo_build_arg.model = model;
    ubo_build_arg.meshes = scene_meshes;
    ubo_build_arg.weight_anim_mask = anim_masks.weights;
    ubo_build_arg.xforms = global_xforms;
    ubo_build_arg.weight_offsets = weight_offsets;
    ubo_build_arg.weight_data = weight_data;

    uchar* ubo_data_base;
    if (gpu->flags & GPU_UMA_BIT)
        ubo_data_base = gpu->mem.bind_buffer.data + offsets->base_bind;
    else
        ubo_data_base = gpu->mem.transfer_buffer.data + offsets->base_stage;

    for(uint i=0; i < arg->scene_count; ++i)
        for(uint j=0; j < model->scenes[arg->scenes[i]].node_count; ++j) {
            assert(model->mesh_count <= 64);
            uint64 mesh_mask = 0;

            memset(scene_meshes, 0, sizeof(*scene_meshes) * model->mesh_count);
            model_node_global_transforms(anim_masks.xforms, anim_arg.xforms, global_xforms,
                                        scene_meshes, &IDENTITY_MATRIX, model->nodes,
                                        model->scenes[arg->scenes[i]].nodes[j], &mesh_mask);
            uint pc = popcnt(mesh_mask);
            for(uint k=0; k < pc; ++k) {
                uint tz = ctz(mesh_mask);
                mesh_mask &= ~(1<<tz);

                // @Optimise I call calculate_transforms_ubo_size a lot in this file. Maybe
                // should be storing the sizes along with the offsets.
                ubo_build_arg.ubo_data = ubo_data_base + offsets->transforms_ubos[tz] +
                    calculate_transforms_ubo_size(&model->meshes[tz]) * mesh_counts[tz];
                model_build_transform_ubo(tz, &ubo_build_arg);

                mesh_counts[tz]++;
            }
        }
}

struct model_animation_timestep {
    uint  frame_i;
    float lerp_constant;
};

// @Todo I do not know if I am supposed to do something special
// if the animation time > max_time. It feels as though I should
// be lerping frame 0 and frame n for a repeating animation like
// a walk, but then again that also seems impossible.
inline static
struct model_animation_timestep get_model_animation_timestep(
    float  time,
    float  min,
    float  max,
    uint   count,
    float *data)
{
    // @Todo Not sure if this is correct, see above.
    time = time > max ? time - max : time;

    // gltf spec, clamp animation to frame 0 if time < min
    if (time <= min)
        return (struct model_animation_timestep){0,0};

    uint i;
    for(i=0; i < count; ++i)
        if (data[i] > time)
            break;

    return (struct model_animation_timestep) {
        .frame_i = i-1,
        .lerp_constant = (time - data[i-1]) / (data[i] - data[i-1]),
    };
}

// @Todo I am not sure if I am supposed to the divide with an integer and then cast
// to a float, or if the divide should be done as a float.
static inline void convert_accessor_s8(uint ofs, void *data, uint count, float *ret)
{
    int8 *d = (int8*)data + ofs;
    float div = (float)1 / 127;
    for(uint i=0; i < count; ++i)
        ret[i] = fmax((float)d[i] * div, -1.0);
}

static inline void convert_accessor_u8(uint ofs, void *data, uint count, float *ret)
{
    uint8 *d = (uint8*)data + ofs;
    float div = (float)1 / 255;
    for(uint i=0; i < count; ++i)
        ret[i] = (float)d[i] * div;
}

static inline void convert_accessor_s16(uint ofs, void *data, uint count, float *ret)
{
    int16 *d = (int16*)data + ofs;
    float div = (float)1 / 32767;
    for(uint i=0; i < count; ++i)
        ret[i] = fmax((float)d[i] * div, -1.0);
}

static inline void convert_accessor_u16(uint ofs, void *data, uint count, float *ret)
{
    uint16 *d = (uint16*)data + ofs;
    float div = (float)1 / 65535;
    for(uint i=0; i < count; ++i)
        ret[i] = (float)d[i] * div;
}

static inline void convert_accessor_float(uint ofs, void *data, uint count, float *ret)
{
    float *d = (float*)data + ofs;
    for(uint i=0; i < count; ++i)
        ret[i] = d[i];
}

typedef void (*convert_accessor_fn)(uint, void*, uint, float*);
convert_accessor_fn ACCESSOR_CONVERT_VECTOR_FNS[6] = {
    convert_accessor_s8,
    convert_accessor_u8,
    convert_accessor_s16,
    convert_accessor_u16,
   (convert_accessor_fn)NULL, // int is impossible
    convert_accessor_float,
};

#if DEBUG
    #define ACCESSOR_COMPONENT_TYPE_FLAGS_ORDER_CHECK() \
        do { \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT           !=  1, "accessor flags were reordered"); \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT  !=  2, "accessor flags were reordered"); \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT          !=  4, "accessor flags were reordered"); \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT !=  8, "accessor flags were reordered"); \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT   != 16, "accessor flags were reordered"); \
            log_print_error_if(GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT          != 32, "accessor flags were reordered"); \
        } while (0)
#else
    #define ACCESSOR_COMPONENT_TYPE_FLAGS_ORDER_CHECK()
#endif

// ofs (offset) should be given irregardless of the underlying data component type, as data
// is cast to the correct type in the function and then offset accordingly, e.g. if you want
// the 2nd vector in 'data', pass ofs = 2 * 3 (2nd item, stride of 3 components).
static inline void
convert_accessor(uint ofs, uint accessor_flags, void *data, uint count, float *ret)
{
    // @Test @Optimise Since these functions are so small it may well be better
    // to go through a switch. Idk if the compiler cannot optimise well through
    // the indexed function calls or if it would get confused or smtg.
#if 1
    ACCESSOR_COMPONENT_TYPE_FLAGS_ORDER_CHECK();
    ACCESSOR_CONVERT_VECTOR_FNS[ctz(accessor_flags)](ofs, data, count, ret);
#else
    switch(accessor_flags & GLTF_ACCESSOR_COMPONENT_TYPE_BITS) {
    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT:
    {
        int8 *d = (int8*)data + ofs;
        float div = (float) 1 / 127;
        for(uint i=0; i < count; ++i)
            ret[i] = fmax((float)d[i] * div, -1.0);
        break;
    }
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT:
    {
        uint8 *d = (uint8*)data + ofs;
        float div = (float)1 / 255;
        for(uint i=0; i < count; ++i)
            ret[i] = (float)d[i] * div;
        break;
    }
    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT:
    {
        int16 *d = (int16*)data + ofs;
        float div = (float)1 / 32767;
        for(uint i=0; i < count; ++i)
            ret[i] = fmax((float)d[i] * div, -1.0);
        break;
    }
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT:
    {
        uint16 *d = (uint16*)data + ofs;
        float div = (float)1 / 65535;
        for(uint i=0; i < count; ++i)
            ret[i] = (float)d[i] * div;
        break;
    }
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT:
    {
        float *d = (float*)data + ofs;
        for(uint i=0; i < count; ++i)
            ret[i] = d[i];
        break;
    }
    default:
        log_print_error("This should be impossible");
    }
#endif
}

static inline void model_anim_transform_translation(struct model_animation_timestep timestep,
                                                    uint accessor_flags, float* data, matrix* ret)
{
    float *uvec1 = data + (timestep.frame_i+0) * 3;
    float *uvec2 = data + (timestep.frame_i+1) * 3;
    vector v1 = get_vector(uvec1[0], uvec1[1], uvec1[2], 0);
    vector v2 = get_vector(uvec2[0], uvec2[1], uvec2[2], 0);
    vector vt = lerp_vector(v1, v2, timestep.lerp_constant);
    translation_matrix(vt, ret);
}

static inline void model_anim_transform_rotation(struct model_animation_timestep timestep,
                                                 uint accessor_flags, float* data, matrix* ret)
{
/*
    @Todo Something to try when I am more confident that all the other stuff is working.
    I am 100% on gltf data alignment yet.
    vector qa[2];
    convert_accessor_vector(timestep.frame_i, accessor_flags, data, 8, qa);
*/
    vector q1 = {0};
    vector q2 = {0};
    convert_accessor((timestep.frame_i+0) * 4, accessor_flags, data, 4, (float*)&q1);
    convert_accessor((timestep.frame_i+1) * 4, accessor_flags, data, 4, (float*)&q2);
    vector q = lerp_vector(q1, q2, timestep.lerp_constant);
    rotation_matrix(q, ret);
}

static inline void model_anim_transform_scale(struct model_animation_timestep timestep,
                                              uint accessor_flags, float* data, matrix* ret)
{
    float *uvec1 = data + (timestep.frame_i+0) * 3;
    float *uvec2 = data + (timestep.frame_i+1) * 3;
    vector v1 = get_vector(uvec1[0], uvec1[1], uvec1[2], 0);
    vector v2 = get_vector(uvec2[0], uvec2[1], uvec2[2], 0);
    vector vs = lerp_vector(v1, v2, timestep.lerp_constant);
    scale_matrix(vs, ret);
}

typedef void (*model_anim_transform_fn)(struct model_animation_timestep, uint, float*, matrix*);
model_anim_transform_fn MODEL_ANIM_TRANSFORM_FNS[3] = {
    model_anim_transform_translation,
    model_anim_transform_rotation,
    model_anim_transform_scale,
};

static inline void model_node_translation(struct trs *trs, matrix *ret)
{
    translation_matrix(trs->t, ret);
}

static inline void model_node_rotation(struct trs *trs, matrix *ret)
{
    rotation_matrix(trs->r, ret);
}

static inline void model_node_scale(struct trs *trs, matrix *ret)
{
    scale_matrix(trs->s, ret);
}

typedef void (*model_node_trs_fn)(struct trs *trs, matrix *ret);
model_node_trs_fn NODE_TRS_FNS[3] = {
    model_node_translation,
    model_node_rotation,
    model_node_scale,
};

static inline void
model_anim_weights(struct model_animation_timestep timestep, uint accessor_flags,
                   uint count, void *output_data, float *weight_data_to, float anim_weight)
{
    assert(count < 32 && "Below array too small");
    float buf[32];

    convert_accessor(timestep.frame_i, accessor_flags, output_data, count*2, buf);
    for(uint i=0; i < count; ++i)
        weight_data_to[i] += lerp(buf[i], buf[count+i], timestep.lerp_constant) * anim_weight;
}

inline static uchar*
model_get_accessor_data(
    struct gpu                  *gpu,
    gltf                        *model,
    uint                         accessor_i,
    struct model_memory_offsets *offsets)
{
    gltf_accessor *acc = &model->accessors[accessor_i];
    gltf_buffer_view *bv = &model->buffer_views[acc->buffer_view];
    uint buf = offsets->buffers[bv->buffer];
    uchar *data;
    if (gpu->flags & GPU_UMA_BIT)
        data = gpu->mem.bind_buffer.data + offsets->base_bind +
               buf + bv->byte_offset + acc->byte_offset;
    else
        data = gpu->mem.transfer_buffer.data + offsets->base_stage +
               buf + bv->byte_offset + acc->byte_offset;
    return data;
}

static void model_animations(
    struct load_model_arg     *lm_arg,
    struct model_memory_offsets  *offsets,
    struct model_animations_arg  *arg,
    struct model_animation_masks *ret)
{
    struct gpu *gpu = lm_arg->gpu;
    gltf *model = lm_arg->model;
    memset(ret, 0, sizeof(*ret));

    uint64 one = 1;
    for(uint j=0; j < lm_arg->animation_count; ++j) {
        gltf_animation *anim = &model->animations[lm_arg->animations[j].index];
        for(uint i=0; i < anim->target_count; ++i) {
            uint mask = anim->targets[i].path_mask;
            uint pc = popcnt(mask);
            matrix trs[3];
            uint node = anim->targets[i].node;
            log_print_error_if(model->nodes[node].flags & GLTF_NODE_MATRIX_BIT,
                               "node is targeted for animation but has matrix property set, this is disallowed by the spec.");
            for(uint k=0; k < pc; ++k) {
                uint tz = ctz(mask);
                mask &= ~(1<<tz);

                gltf_animation_sampler *sampler = &anim->samplers[anim->targets[i].samplers[tz]];
                gltf_accessor *input = &model->accessors[sampler->input];

                struct model_animation_timestep timestep =
                    get_model_animation_timestep(
                        lm_arg->animations[j].time,
                        input->max_min.min[0],
                        input->max_min.max[0],
                        input->count,
                        (float*)model_get_accessor_data(gpu, model, sampler->input, offsets));

                gltf_accessor *output = &model->accessors[sampler->output];

                // The below 'if else' block relies on this assertion.
                assert(GLTF_ANIMATION_PATH_WEIGHTS_BIT != 0x08 && "animation path flag bits have changed");

                if (tz == 3) {
                    model_anim_weights(
                            timestep,
                            output->flags,
                            output->count,
                            model_get_accessor_data(gpu, model, sampler->output, offsets),
                            arg->weight_data + arg->weight_offsets[node],
                            lm_arg->animations[j].weight);
                    ret->weights[node>>6] |= one << (node & 63);
                } else {
                    assert(GLTF_ANIMATION_PATH_TRANSLATION_BIT == 1 &&
                           GLTF_ANIMATION_PATH_ROTATION_BIT == 2 &&
                           GLTF_ANIMATION_PATH_SCALE_BIT == 4);
                    // @Optimise @Test Maybe this is faster as a switch?
                    MODEL_ANIM_TRANSFORM_FNS[tz](
                            timestep,
                            output->flags,
                            (float*)model_get_accessor_data(gpu, model, sampler->output, offsets),
                            &trs[tz]);
                    ret->xforms[node>>6] |= one << (node & 63);
                }
            }
            mask = ~(anim->targets[i].path_mask | GLTF_ANIMATION_PATH_WEIGHTS_BIT);
            pc = popcnt(mask);
            for(uint k=0; k < pc; ++k) {
                uint tz = ctz(mask);
                mask &= ~(1<<tz);
                NODE_TRS_FNS[tz](&model->nodes[node].trs, &trs[tz]);
            }
            mul_matrix(&trs[0], &trs[1], &trs[1]);
            mul_matrix(&trs[1], &trs[2], &trs[2]);
            mul_matrix(&trs[2], &arg->xforms[node], &arg->xforms[node]);
        }
    }
}

static void model_build_transform_ubo(uint mesh, struct model_build_transform_ubo_arg *arg)
{
    gltf *model = arg->model;
    uchar *ubo_data = arg->ubo_data;
    uint64 skins = arg->meshes[mesh].skin_mask;
    uint64 *nodes = arg->meshes[mesh].node_mask;
    uint64 one = 1;

    uint joints_trs_ofs = transforms_ubo_offsetof(&model->meshes[mesh], TRANSFORMS_UBO_JOINTS_TRS);
    uint joints_tbn_ofs = transforms_ubo_offsetof(&model->meshes[mesh], TRANSFORMS_UBO_JOINTS_TBN);

    uint pc = popcnt(skins);
    for(uint j=0; j < pc; ++j) {
        uint tz = ctz(skins);
        skins &= ~(one<<tz);
        gltf_skin *skin = &model->skins[tz];
        for(uint i=0; i < skin->joint_count; ++i) {
            memcpy(ubo_data + joints_trs_ofs + sizeof(matrix) * i,
                   arg->xforms + skin->joints[i], sizeof(matrix));
            memcpy(ubo_data + joints_tbn_ofs + sizeof(matrix) * i,
                   arg->xforms + skin->joints[i], sizeof(matrix)); // @TODO TBN matrices.
        }
    }

    uint node_trs_ofs = transforms_ubo_offsetof(&model->meshes[mesh], TRANSFORMS_UBO_NODE_TRS);
    uint node_tbn_ofs = transforms_ubo_offsetof(&model->meshes[mesh], TRANSFORMS_UBO_NODE_TBN);
    uint node_w_ofs = transforms_ubo_offsetof(&model->meshes[mesh], TRANSFORMS_UBO_MORPH_WEIGHTS);

    bool skinned = model->meshes[mesh].joint_count;
    for(uint i=0; i < carrlen(arg->meshes[mesh].node_mask); ++i) {
        pc = popcnt(nodes[i]);
        for(uint j=0; j < pc; ++j) {
            uint tz = ctz(nodes[i]);
            nodes[i] &= ~(one<<tz);

            uint node = tz + (i * 64);

            if (!skinned) {
                memcpy(ubo_data + node_trs_ofs, arg->xforms + node, sizeof(matrix));
                memcpy(ubo_data + node_tbn_ofs, arg->xforms + node, sizeof(matrix)); // @TODO TBN matrices.
            }

            // @Test @Optimise Weights should maybe be copied into weight_data in the animation
            // function regardless of their being animated as that would remove the branch here. It
            // would increase the run time of the animations function, but I think that is worth it
            // as I feel that this loop will run more times than the animation function.
            if (model->nodes[node].weight_count) {
                if (arg->weight_anim_mask[node>>6] & (one << (node & 63)))
                    memcpy(ubo_data + node_w_ofs, arg->weight_data + arg->weight_offsets[node],
                           sizeof(float) * model->nodes[node].weight_count);
                else
                    memcpy(ubo_data + node_w_ofs, model->nodes[i].weights,
                           sizeof(float) * model->nodes[node].weight_count);
            }
        }
    }
}
