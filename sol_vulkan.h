#ifndef SOL_VULKAN_H_INCLUDE_GUARD_
#define SOL_VULKAN_H_INCLUDE_GUARD_

#if SHADER_C
    #include <shaderc/shaderc.h>
#endif

#include <vulkan/vulkan.h>

enum {
    SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_INSTANCE,
    SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_DEVICE,
    SOL_VK_DISPATCH_TABLE_INIT_STAGE_POST_DEVICE,
};

struct vulkan_dispatch_table {
    PFN_vkCreateInstance create_instance;
    PFN_vkDestroyInstance destroy_instance;
    PFN_vkEnumerateInstanceExtensionProperties enumerate_instance_extension_properties;
    PFN_vkCreateDebugUtilsMessengerEXT create_debug_utils_messenger_ext;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_utils_messenger_ext;
    PFN_vkCreateSwapchainKHR create_swapchain_khr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images_khr;
    PFN_vkDestroySwapchainKHR destroy_swapchain_khr;
    PFN_vkGetBufferDeviceAddress get_buffer_device_address;
    PFN_vkCreateDescriptorPool create_descriptor_pool;
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool;
    PFN_vkAllocateDescriptorSets allocate_descriptor_sets;
    PFN_vkUpdateDescriptorSets update_descriptor_sets;
    PFN_vkResetDescriptorPool reset_descriptor_pool;
    PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout;
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout;
    PFN_vkGetDescriptorEXT get_descriptor_ext;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_descriptor_set_layout_binding_offset_ext;
    PFN_vkGetDescriptorSetLayoutSizeEXT get_descriptor_set_layout_size_ext;
    PFN_vkCreateShaderModule create_shader_module;
    PFN_vkDestroyShaderModule destroy_shader_module;
    PFN_vkCreateBuffer create_buffer;
    PFN_vkDestroyBuffer destroy_buffer;
    PFN_vkCreateImage create_image;
    PFN_vkDestroyImage destroy_image;
    PFN_vkGetImageMemoryRequirements get_image_memory_requirements;
    PFN_vkBindImageMemory bind_image_memory;
    PFN_vkCreateImageView create_image_view;
    PFN_vkDestroyImageView destroy_image_view;
    PFN_vkCreateSampler create_sampler;
    PFN_vkDestroySampler destroy_sampler;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines;
    PFN_vkDestroyPipeline destroy_pipeline;
    PFN_vkCreateCommandPool create_command_pool;
    PFN_vkDestroyCommandPool destroy_command_pool;
    PFN_vkResetCommandPool reset_command_pool;
    PFN_vkAllocateCommandBuffers allocate_command_buffers;
    PFN_vkCreateFramebuffer create_framebuffer;
    PFN_vkDestroyFramebuffer destroy_framebuffer;
    PFN_vkCreateRenderPass create_renderpass;
    PFN_vkDestroyRenderPass destroy_renderpass;
    PFN_vkCreatePipelineLayout create_pipeline_layout;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout;
    PFN_vkCreateSemaphore create_semaphore;
    PFN_vkDestroySemaphore destroy_semaphore;
    PFN_vkCreateFence create_fence;
    PFN_vkDestroyFence destroy_fence;
    PFN_vkBeginCommandBuffer begin_command_buffer;
    PFN_vkEndCommandBuffer end_command_buffer;
    PFN_vkQueueSubmit2 queue_submit2;
    PFN_vkQueuePresentKHR queue_present_khr;
    PFN_vkWaitForFences wait_for_fences;
    PFN_vkResetFences reset_fences;
    PFN_vkCmdExecuteCommands cmd_execute_commands;
    PFN_vkCmdBeginRenderPass cmd_begin_renderpass;
    PFN_vkCmdEndRenderPass cmd_end_renderpass;
    PFN_vkCmdNextSubpass cmd_next_subpass;
    PFN_vkCmdPipelineBarrier2 cmd_pipeline_barrier2;
    PFN_vkCmdCopyBuffer cmd_copy_buffer;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image;
    PFN_vkCmdBlitImage2 cmd_blit_image_2;
    PFN_vkCmdBindPipeline cmd_bind_pipeline;
    PFN_vkCmdBindIndexBuffer cmd_bind_index_buffer;
    PFN_vkCmdBindVertexBuffers cmd_bind_vertex_buffers;
    PFN_vkCmdBindDescriptorBuffersEXT cmd_bind_descriptor_buffers_ext;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT cmd_set_descriptor_buffer_offsets_ext;
    PFN_vkCmdPushConstants cmd_push_constants;
    PFN_vkCmdDrawIndexed cmd_draw_indexed;
    PFN_vkCmdDraw cmd_draw;
};

extern struct vulkan_dispatch_table vulkan_dispatch_table;

void init_vk_dispatch_table(int stage, VkInstance instance, VkDevice device);

static inline VkResult vk_create_instance(
    const VkInstanceCreateInfo  *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance                  *pInstance)
{
    return vulkan_dispatch_table.create_instance(pCreateInfo, pAllocator, pInstance);
}

static inline void vk_destroy_instance(
    VkInstance                   instance,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_instance(instance, pAllocator);
}

static inline VkResult vk_enumerate_instance_extension_properties(
    const char            *pLayerName,
    uint32_t              *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    return vulkan_dispatch_table.enumerate_instance_extension_properties(pLayerName, pPropertyCount, pProperties);
}

static inline VkResult vk_create_debug_utils_messenger_ext(
    VkInstance                                instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks              *pAllocator,
    VkDebugUtilsMessengerEXT                 *pMessenger)
{
    return vulkan_dispatch_table.create_debug_utils_messenger_ext(instance, pCreateInfo, pAllocator, pMessenger);
}

static inline void vk_destroy_debug_utils_messenger_ext(
    VkInstance                   instance,
    VkDebugUtilsMessengerEXT     messenger,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_debug_utils_messenger_ext(instance, messenger, pAllocator);
}

static inline VkResult vk_create_swapchain_khr(
    VkDevice                        device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks    *pAllocator,
    VkSwapchainKHR                 *pSwapchain)
{
    return vulkan_dispatch_table.create_swapchain_khr(device, pCreateInfo, pAllocator, pSwapchain);
}

static inline VkResult vk_get_swapchain_images_khr(
    VkDevice        device,
    VkSwapchainKHR  swapchain,
    uint32_t       *pSwapchainImageCount,
    VkImage        *pSwapchainImages)
{
    return vulkan_dispatch_table.get_swapchain_images_khr(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

static inline void vk_destroy_swapchain_khr(
    VkDevice                     device,
    VkSwapchainKHR               swapchain,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_swapchain_khr(device, swapchain, pAllocator);
}

static inline VkDeviceAddress vk_get_buffer_device_address(
    VkDevice                         device,
    const VkBufferDeviceAddressInfo *pInfo)
{
    return vulkan_dispatch_table.get_buffer_device_address(device, pInfo);
}

static inline VkResult vk_create_descriptor_set_layout(
    VkDevice                         device,
    VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    VkAllocationCallbacks           *pAllocator,
    VkDescriptorSetLayout           *pSetLayout)
{
    return vulkan_dispatch_table.create_descriptor_set_layout(device, pCreateInfo, pAllocator, pSetLayout);
}

static inline void vk_destroy_descriptor_set_layout(
    VkDevice               device,
    VkDescriptorSetLayout  setLayout,
    VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_descriptor_set_layout(device, setLayout, pAllocator);
}

static inline VkResult vk_create_descriptor_pool(
    VkDevice                          device,
    const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks*      pAllocator,
    VkDescriptorPool*                 pDescriptorPool)
{
    return vulkan_dispatch_table.create_descriptor_pool(device, pCreateInfo, pAllocator, pDescriptorPool);
}

static inline void vk_destroy_descriptor_pool(
    VkDevice                     device,
    VkDescriptorPool             descriptorPool,
    const VkAllocationCallbacks* pAllocator)
{
    vulkan_dispatch_table.destroy_descriptor_pool(device, descriptorPool, pAllocator);
}

static inline VkResult vk_allocate_descriptor_sets(
    VkDevice                           device,
    const VkDescriptorSetAllocateInfo* pAllocateInfo,
    VkDescriptorSet*                   pDescriptorSets)
{
    return vulkan_dispatch_table.allocate_descriptor_sets(device, pAllocateInfo, pDescriptorSets);
}

static inline void vk_update_descriptor_sets(
    VkDevice                    device,
    uint32_t                    descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t                    descriptorCopyCount,
    const VkCopyDescriptorSet*  pDescriptorCopies)
{
    vulkan_dispatch_table.update_descriptor_sets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

static inline VkResult vk_reset_descriptor_pool(
    VkDevice                   device,
    VkDescriptorPool           descriptorPool,
    VkDescriptorPoolResetFlags flags)
{
    return vulkan_dispatch_table.reset_descriptor_pool(device, descriptorPool, flags);
}

static inline void vk_get_descriptor_ext(
    VkDevice                      device,
    const VkDescriptorGetInfoEXT *pDescriptorGetInfo,
    uint64_t                      dataSize,
    void                         *pDescriptor)
{
    return vulkan_dispatch_table.get_descriptor_ext(device, pDescriptorGetInfo, dataSize, pDescriptor);
}

static inline void vk_get_descriptor_set_layout_binding_offset_ext(
    VkDevice               device,
    VkDescriptorSetLayout  layout,
    uint32_t               binding,
    uint64_t              *offset)
{
    vulkan_dispatch_table.get_descriptor_set_layout_binding_offset_ext(device, layout, binding, offset);
}

static inline void vk_get_descriptor_set_layout_size_ext(
    VkDevice               device,
    VkDescriptorSetLayout  layout,
    uint64_t              *pLayoutSizeInBytes)
{
    return vulkan_dispatch_table.get_descriptor_set_layout_size_ext(device, layout, pLayoutSizeInBytes);
}

static inline VkResult vk_create_shader_module(
    VkDevice                        device,
    const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks    *pAllocator,
    VkShaderModule                 *pShaderModule)
{
    return vulkan_dispatch_table.create_shader_module(device, pCreateInfo, pAllocator, pShaderModule);
}

static inline void vk_destroy_shader_module(
    VkDevice                    device,
    VkShaderModule              shaderModule,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_shader_module(device, shaderModule, pAllocator);
}

static inline VkResult vk_create_buffer(
    VkDevice                     device,
    const VkBufferCreateInfo     *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkBuffer                    *pBuffer)
{
    return vulkan_dispatch_table.create_buffer(device, pCreateInfo, pAllocator, pBuffer);
}

static inline void vk_destroy_buffer(
    VkDevice                     device,
    VkBuffer                     buffer,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_buffer(device, buffer, pAllocator);
}

static inline VkResult vk_create_image(
    VkDevice                     device,
    const VkImageCreateInfo     *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkImage                     *pImage)
{
    return vulkan_dispatch_table.create_image(device, pCreateInfo, pAllocator, pImage);
}

static inline void vk_destroy_image(
    VkDevice                     device,
    VkImage                      image,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_image(device, image, pAllocator);
}

static inline VkResult vk_create_image_view(
    VkDevice                      device,
    const VkImageViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkImageView                 *pImageView)
{
    return vulkan_dispatch_table.create_image_view(device, pCreateInfo, pAllocator, pImageView);
}

static inline void vk_destroy_image_view(
    VkDevice                     device,
    VkImageView                  view,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_image_view(device, view, pAllocator);
}

static inline void vk_get_image_memory_requirements(
    VkDevice              device,
    VkImage               image,
    VkMemoryRequirements *pMemoryRequirements)
{
    return vulkan_dispatch_table.get_image_memory_requirements(device, image, pMemoryRequirements);
}

static inline VkResult vk_bind_image_memory(
    VkDevice       device,
    VkImage        image,
    VkDeviceMemory memory,
    VkDeviceSize   memoryOffset)
{
    return vulkan_dispatch_table.bind_image_memory(device, image, memory, memoryOffset);
}

static inline VkResult vk_create_sampler(
    VkDevice                    device,
    const VkSamplerCreateInfo   *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSampler                   *pSampler)
{
    return vulkan_dispatch_table.create_sampler(device, pCreateInfo, pAllocator, pSampler);
}

static inline void vk_destroy_sampler(
    VkDevice                     device,
    VkSampler                    sampler,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_sampler(device, sampler, pAllocator);
}

static inline VkResult vk_create_graphics_pipelines(
    VkDevice                            device,
    VkPipelineCache                     pipelineCache,
    uint32_t                            createInfoCount,
    const VkGraphicsPipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks        *pAllocator,
    VkPipeline                         *pPipelines)
{
    return vulkan_dispatch_table.create_graphics_pipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

static inline void vk_destroy_pipeline(
    VkDevice               device,
    VkPipeline             pipeline,
    VkAllocationCallbacks *pAllocationCallbacks)
{
    return vulkan_dispatch_table.destroy_pipeline(device, pipeline, pAllocationCallbacks);
}

static inline VkResult vk_create_command_pool(
    VkDevice                       device,
    const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks   *pAllocator,
    VkCommandPool                 *pCommandPool)
{
    return vulkan_dispatch_table.create_command_pool(device, pCreateInfo, pAllocator, pCommandPool);
}

static inline void vk_destroy_command_pool(
    VkDevice                     device,
    VkCommandPool                commandPool,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_command_pool(device, commandPool, pAllocator);
}

static inline VkResult vk_reset_command_pool(
    VkDevice                device,
    VkCommandPool           commandPool,
    VkCommandPoolResetFlags flags)
{
    return vulkan_dispatch_table.reset_command_pool(device, commandPool, flags);
}

static inline VkResult vk_allocate_command_buffers(
    VkDevice                           device,
    const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer                   *pCommandBuffers)
{
    return vulkan_dispatch_table.allocate_command_buffers(device, pAllocateInfo, pCommandBuffers);
}

static inline VkResult vk_create_framebuffer(
    VkDevice                       device,
    const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks   *pAllocator,
    VkFramebuffer                 *pFramebuffer)
{
    return vulkan_dispatch_table.create_framebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
}


static inline void vk_destroy_framebuffer(
    VkDevice                     device,
    VkFramebuffer                framebuffer,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_framebuffer(device, framebuffer, pAllocator);
}

static inline VkResult vk_create_renderpass(
    VkDevice                      device,
    const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks  *pAllocator,
    VkRenderPass                 *pRenderpass)
{
    return vulkan_dispatch_table.create_renderpass(device, pCreateInfo, pAllocator, pRenderpass);
}

static inline void vk_destroy_renderpass(
    VkDevice                     device,
    VkRenderPass                 renderpass,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_renderpass(device, renderpass, pAllocator);
}

static inline VkResult vk_create_pipeline_layout(
    VkDevice                          device,
    const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks      *pAllocator,
    VkPipelineLayout                 *pPipelineLayout)
{
    return vulkan_dispatch_table.create_pipeline_layout(device, pCreateInfo, pAllocator, pPipelineLayout);
}

static inline void vk_destroy_pipeline_layout(
    VkDevice                     device,
    VkPipelineLayout             pipelineLayout,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_pipeline_layout(device, pipelineLayout, pAllocator);
}

static inline VkResult vk_create_semaphore(
    VkDevice                     device,
    const VkSemaphoreCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSemaphore                 *pSemaphore)
{
    return vulkan_dispatch_table.create_semaphore(device, pCreateInfo, pAllocator, pSemaphore);
}

static inline void vk_destroy_semaphore(
    VkDevice                     device,
    VkSemaphore                  semaphore,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_semaphore(device, semaphore, pAllocator);
}

static inline VkResult vk_create_fence(
    VkDevice                     device,
    const VkFenceCreateInfo     *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkFence                     *pFence)
{
    return vulkan_dispatch_table.create_fence(device, pCreateInfo, pAllocator, pFence);
}

static inline void vk_destroy_fence(
    VkDevice                     device,
    VkFence                      fence,
    const VkAllocationCallbacks *pAllocator)
{
    return vulkan_dispatch_table.destroy_fence(device, fence, pAllocator);
}

static inline VkResult vk_begin_command_buffer(
    VkCommandBuffer                 commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo)
{
    return vulkan_dispatch_table.begin_command_buffer(commandBuffer, pBeginInfo);
}

static inline VkResult vk_end_command_buffer(VkCommandBuffer commandBuffer)
{
    return vulkan_dispatch_table.end_command_buffer(commandBuffer);
}

static inline VkResult vk_queue_submit2(
    VkQueue              queue,
    uint32_t             submitCount,
    const VkSubmitInfo2 *pSubmits,
    VkFence              fence)
{
    return vulkan_dispatch_table.queue_submit2(queue, submitCount, pSubmits, fence);
}

static inline VkResult vk_queue_present_khr(
    VkQueue                 queue,
    const VkPresentInfoKHR *pPresentInfo)
{
    return vulkan_dispatch_table.queue_present_khr(queue, pPresentInfo);
}

static inline VkResult vk_wait_for_fences(
    VkDevice       device,
    uint32_t       fenceCount,
    const VkFence *pFences,
    VkBool32       waitAll,
    uint64_t       timeout)
{
    return vulkan_dispatch_table.wait_for_fences(device, fenceCount, pFences, waitAll, timeout);
}

VkResult vk_reset_fences(
    VkDevice       device,
    uint32_t       fenceCount,
    const VkFence *pFences)
{
    return vulkan_dispatch_table.reset_fences(device, fenceCount, pFences);
}

void vk_cmd_execute_commands(
    VkCommandBuffer        commandBuffer,
    uint32_t               commandBufferCount,
    const VkCommandBuffer *pCommandBuffers)
{
    return vulkan_dispatch_table.cmd_execute_commands(commandBuffer, commandBufferCount, pCommandBuffers);
}

void vk_cmd_begin_renderpass(
    VkCommandBuffer        commandBuffer,
    VkRenderPassBeginInfo *pBeginInfo,
    VkSubpassContents      subpassContents)
{
    return vulkan_dispatch_table.cmd_begin_renderpass(commandBuffer, pBeginInfo, subpassContents);
}

void vk_cmd_end_renderpass(VkCommandBuffer commandBuffer)
{
    return vulkan_dispatch_table.cmd_end_renderpass(commandBuffer);
}

void vk_cmd_next_subpass(VkCommandBuffer commandBuffer, VkSubpassContents subpassContents)
{
    return vulkan_dispatch_table.cmd_next_subpass(commandBuffer, subpassContents);
}

static inline void vk_cmd_pipeline_barrier2(
    VkCommandBuffer         commandBuffer,
    const VkDependencyInfo* pDependencyInfo)
{
    return vulkan_dispatch_table.cmd_pipeline_barrier2(commandBuffer, pDependencyInfo);
}

static inline void vk_cmd_copy_buffer(
    VkCommandBuffer     commandBuffer,
    VkBuffer            srcBuffer,
    VkBuffer            dstBuffer,
    uint32_t            regionCount,
    const VkBufferCopy* pRegions)
{
    return vulkan_dispatch_table.cmd_copy_buffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

static inline void vk_cmd_copy_buffer_to_image(
    VkCommandBuffer          commandBuffer,
    VkBuffer                 srcBuffer,
    VkImage                  dstImage,
    VkImageLayout            dstImageLayout,
    uint32_t                 regionCount,
    const VkBufferImageCopy *pRegions)
{
    return vulkan_dispatch_table.cmd_copy_buffer_to_image(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

static inline void vk_cmd_blit_image_2(
    VkCommandBuffer         commandBuffer,
    const VkBlitImageInfo2 *pBlitImageInfo)
{
    return vulkan_dispatch_table.cmd_blit_image_2(commandBuffer, pBlitImageInfo);
}

static inline void vk_cmd_bind_pipeline(
    VkCommandBuffer     commandBuffer,
    VkPipelineBindPoint bindPoint,
    VkPipeline          pipeline)
{
    return vulkan_dispatch_table.cmd_bind_pipeline(commandBuffer, bindPoint, pipeline);
}

static inline void vk_cmd_bind_index_buffer(
    VkCommandBuffer commandBuffer,
    VkBuffer        buffer,
    VkDeviceSize    offset,
    VkIndexType     indexType)
{
    return vulkan_dispatch_table.cmd_bind_index_buffer(commandBuffer, buffer, offset, indexType);
}

static inline void vk_cmd_bind_vertex_buffers(
    VkCommandBuffer     commandBuffer,
    uint32_t            firstBinding,
    uint32_t            bindingCount,
    const VkBuffer     *pBuffers,
    const VkDeviceSize *pOffsets)
{
    return vulkan_dispatch_table.cmd_bind_vertex_buffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

static inline void vk_cmd_bind_descriptor_buffers_ext(
    VkCommandBuffer                         commandBuffer,
    uint32_t                                bufferCount,
    const VkDescriptorBufferBindingInfoEXT *pBindingInfos)
{
    return vulkan_dispatch_table.cmd_bind_descriptor_buffers_ext(commandBuffer, bufferCount, pBindingInfos);
}

static inline void vk_cmd_set_descriptor_buffer_offsets_ext(
    VkCommandBuffer     commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout    layout,
    uint32_t            firstSet,
    uint32_t            setCount,
    const uint32_t     *pBufferIndices,
    const VkDeviceSize *pOffsets)
{
    return vulkan_dispatch_table.cmd_set_descriptor_buffer_offsets_ext(commandBuffer, pipelineBindPoint, layout, firstSet, setCount, pBufferIndices, pOffsets);
}

static inline void vk_cmd_push_constants(
    VkCommandBuffer    commandBuffer,
    VkPipelineLayout   layout,
    VkShaderStageFlags stageFlags,
    uint32_t           offset,
    uint32_t           size,
    const void        *pValues)
{
    return vulkan_dispatch_table.cmd_push_constants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

static inline void vk_cmd_draw(
    VkCommandBuffer commandBuffer,
    uint32_t        vertexCount,
    uint32_t        instanceCount,
    uint32_t        firstVertex,
    uint32_t        firstInstance)
{
    return vulkan_dispatch_table.cmd_draw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

static inline void vk_cmd_draw_indexed(
    VkCommandBuffer commandBuffer,
    uint32_t        indexCount,
    uint32_t        instanceCount,
    uint32_t        firstIndex,
    int32_t         vertexOffset,
    uint32_t        firstInstance)
{
    return vulkan_dispatch_table.cmd_draw_indexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

#endif // include guard
