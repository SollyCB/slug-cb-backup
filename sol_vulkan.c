#include "sol_vulkan.h"
#include "defs.h"

static inline PFN_vkCreateInstance sol_vkCreateInstance() {
    PFN_vkCreateInstance fn = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    log_print_error_if(!fn, "PFN_vkCreateInstance not present");
    return fn;
}

static inline PFN_vkDestroyInstance sol_vkDestroyInstance(VkInstance instance) {
    PFN_vkDestroyInstance fn = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    log_print_error_if(!fn, "PFN_vkDestroyInstance not present");
    return fn;
}

static inline PFN_vkEnumerateInstanceExtensionProperties sol_vkEnumerateInstanceExtensionProperties() {
    PFN_vkEnumerateInstanceExtensionProperties fn = (PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");
    log_print_error_if(!fn, "PFN_vkEnumerateInstanceExtensionProperties not present");
    return fn;
}

static inline PFN_vkCreateDebugUtilsMessengerEXT sol_vkCreateDebugUtilsMessengerEXT(VkInstance instance) {
    // @Validation Layers are controlled by vkconfig now.
    // PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    // log_print_error_if(!fn, "PFN_vkCreateDebugUtilsMessengerEXT not present");
    // return fn;
}

static inline PFN_vkDestroyDebugUtilsMessengerEXT sol_vkDestroyDebugUtilsMessengerEXT(VkInstance instance) {
    // @Validation Layers are controlled by vkconfig now.
    // PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    // log_print_error_if(!fn, "PFN_vkDestroyDebugUtilsMessengerEXT not present");
    // return fn;
}

static inline PFN_vkCreateSwapchainKHR sol_vkCreateSwapchainKHR(VkDevice device) {
    PFN_vkCreateSwapchainKHR fn = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    log_print_error_if(!fn, "PFN_vkCreateSwapchainKHR not present");
    return fn;
}

static inline PFN_vkGetSwapchainImagesKHR sol_vkGetSwapchainImagesKHR(VkDevice device) {
    PFN_vkGetSwapchainImagesKHR fn = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
    log_print_error_if(!fn, "PFN_vkGetSwapchainImagesKHR not present");
    return fn;
}

static inline PFN_vkDestroySwapchainKHR sol_vkDestroySwapchainKHR(VkDevice device) {
    PFN_vkDestroySwapchainKHR fn = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    log_print_error_if(!fn, "PFN_vkDestroySwapchainKHR not present");
    return fn;
}

static inline PFN_vkGetBufferDeviceAddress sol_vkGetBufferDeviceAddress(VkDevice device) {
    PFN_vkGetBufferDeviceAddress fn = (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress");
    log_print_error_if(!fn, "PFN_vkGetBufferDeviceAddress not present");
    return fn;
}

static inline PFN_vkCreateDescriptorSetLayout sol_vkCreateDescriptorSetLayout(VkDevice device) {
    PFN_vkCreateDescriptorSetLayout fn = (PFN_vkCreateDescriptorSetLayout)vkGetDeviceProcAddr(device, "vkCreateDescriptorSetLayout");
    log_print_error_if(!fn, "PFN_vkCreateDescriptorSetLayout not present");
    return fn;
}

static inline PFN_vkDestroyDescriptorSetLayout sol_vkDestroyDescriptorSetLayout(VkDevice device) {
    PFN_vkDestroyDescriptorSetLayout fn = (PFN_vkDestroyDescriptorSetLayout)vkGetDeviceProcAddr(device, "vkDestroyDescriptorSetLayout");
    log_print_error_if(!fn, "PFN_vkDestroyDescriptorSetLayout not present");
    return fn;
}

static inline PFN_vkGetDescriptorEXT sol_vkGetDescriptorEXT(VkDevice device) {
    PFN_vkGetDescriptorEXT fn = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorEXT");
    log_print_error_if(!fn, "PFN_vkGetDescriptorEXT not present");
    return fn;
}

static inline PFN_vkGetDescriptorSetLayoutBindingOffsetEXT sol_vkGetDescriptorSetLayoutBindingOffsetEXT(VkDevice device) {
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT fn = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorSetLayoutBindingOffsetEXT");
    log_print_error_if(!fn, "PFN_vkGetDescriptorSetLayoutBindingOffsetEXT not present");
    return fn;
}

static inline PFN_vkGetDescriptorSetLayoutSizeEXT sol_vkGetDescriptorSetLayoutSizeEXT(VkDevice device) {
    PFN_vkGetDescriptorSetLayoutSizeEXT fn = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorSetLayoutSizeEXT");
    log_print_error_if(!fn, "PFN_vkGetDescriptorSetLayoutSizeEXT not present");
    return fn;
}

static inline PFN_vkCreateShaderModule sol_vkCreateShaderModule(VkDevice device) {
    PFN_vkCreateShaderModule fn = (PFN_vkCreateShaderModule)vkGetDeviceProcAddr(device, "vkCreateShaderModule");
    log_print_error_if(!fn, "PFN_vkCreateShaderModule not present");
    return fn;
}

static inline PFN_vkDestroyShaderModule sol_vkDestroyShaderModule(VkDevice device) {
    PFN_vkDestroyShaderModule fn = (PFN_vkDestroyShaderModule)vkGetDeviceProcAddr(device, "vkDestroyShaderModule");
    log_print_error_if(!fn, "PFN_vkDestroyShaderModule not present");
    return fn;
}

static inline PFN_vkCreateBuffer sol_vkCreateBuffer(VkDevice device) {
    PFN_vkCreateBuffer fn = (PFN_vkCreateBuffer)vkGetDeviceProcAddr(device, "vkCreateBuffer");
    log_print_error_if(!fn, "PFN_vkCreateBuffer not present");
    return fn;
}

static inline PFN_vkDestroyBuffer sol_vkDestroyBuffer(VkDevice device) {
    PFN_vkDestroyBuffer fn = (PFN_vkDestroyBuffer)vkGetDeviceProcAddr(device, "vkDestroyBuffer");
    log_print_error_if(!fn, "PFN_vkDestroyBuffer not present");
    return fn;
}

static inline PFN_vkCreateImage sol_vkCreateImage(VkDevice device) {
    PFN_vkCreateImage fn = (PFN_vkCreateImage)vkGetDeviceProcAddr(device, "vkCreateImage");
    log_print_error_if(!fn, "PFN_vkCreateImage not present");
    return fn;
}

static inline PFN_vkDestroyImage sol_vkDestroyImage(VkDevice device) {
    PFN_vkDestroyImage fn = (PFN_vkDestroyImage)vkGetDeviceProcAddr(device, "vkDestroyImage");
    log_print_error_if(!fn, "PFN_vkDestroyImage not present");
    return fn;
}

static inline PFN_vkCreateImageView sol_vkCreateImageView(VkDevice device) {
    PFN_vkCreateImageView fn = (PFN_vkCreateImageView)vkGetDeviceProcAddr(device, "vkCreateImageView");
    log_print_error_if(!fn, "PFN_vkCreateImageView not present");
    return fn;
}

static inline PFN_vkDestroyImageView sol_vkDestroyImageView(VkDevice device) {
    PFN_vkDestroyImageView fn = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(device, "vkDestroyImageView");
    log_print_error_if(!fn, "PFN_vkDestroyImageView not present");
    return fn;
}

static inline PFN_vkGetImageMemoryRequirements sol_vkGetImageMemoryRequirements(VkDevice device) {
    PFN_vkGetImageMemoryRequirements fn = (PFN_vkGetImageMemoryRequirements)vkGetDeviceProcAddr(device, "vkGetImageMemoryRequirements");
    log_print_error_if(!fn, "PFN_vkGetImageMemoryRequirements not present");
    return fn;
}

static inline PFN_vkBindImageMemory sol_vkBindImageMemory(VkDevice device) {
    PFN_vkBindImageMemory fn = (PFN_vkBindImageMemory)vkGetDeviceProcAddr(device, "vkBindImageMemory");
    log_print_error_if(!fn, "PFN_vkBindImageMemory not present");
    return fn;
}

static inline PFN_vkCreateSampler sol_vkCreateSampler(VkDevice device) {
    PFN_vkCreateSampler fn = (PFN_vkCreateSampler)vkGetDeviceProcAddr(device, "vkCreateSampler");
    log_print_error_if(!fn, "PFN_vkCreateSampler not present");
    return fn;
}

static inline PFN_vkDestroySampler sol_vkDestroySampler(VkDevice device) {
    PFN_vkDestroySampler fn = (PFN_vkDestroySampler)vkGetDeviceProcAddr(device, "vkDestroySampler");
    log_print_error_if(!fn, "PFN_vkDestroySampler not present");
    return fn;
}

static inline PFN_vkCreateGraphicsPipelines sol_vkCreateGraphicsPipelines(VkDevice device) {
    PFN_vkCreateGraphicsPipelines fn = (PFN_vkCreateGraphicsPipelines)vkGetDeviceProcAddr(device, "vkCreateGraphicsPipelines");
    log_print_error_if(!fn, "PFN_vkCreateGraphicsPipelines not present");
    return fn;
}

static inline PFN_vkDestroyPipeline sol_vkDestroyPipeline(VkDevice device) {
    PFN_vkDestroyPipeline fn = (PFN_vkDestroyPipeline)vkGetDeviceProcAddr(device, "vkDestroyPipeline");
    log_print_error_if(!fn, "PFN_vkDestroyPipeline not present");
    return fn;
}

static inline PFN_vkCreateCommandPool sol_vkCreateCommandPool(VkDevice device) {
    PFN_vkCreateCommandPool fn = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");
    log_print_error_if(!fn, "PFN_vkCreateCommandPool not present");
    return fn;
}

static inline PFN_vkDestroyCommandPool sol_vkDestroyCommandPool(VkDevice device) {
    PFN_vkDestroyCommandPool fn = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(device, "vkDestroyCommandPool");
    log_print_error_if(!fn, "PFN_vkDestroyCommandPool not present");
    return fn;
}

static inline PFN_vkResetCommandPool sol_vkResetCommandPool(VkDevice device) {
    PFN_vkResetCommandPool fn = (PFN_vkResetCommandPool)vkGetDeviceProcAddr(device, "vkResetCommandPool");
    log_print_error_if(!fn, "PFN_vkResetCommandPool not present");
    return fn;
}

static inline PFN_vkAllocateCommandBuffers sol_vkAllocateCommandBuffers(VkDevice device) {
    PFN_vkAllocateCommandBuffers fn = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
    log_print_error_if(!fn, "PFN_vkAllocateCommandBuffers not present");
    return fn;
}

static inline PFN_vkCreateFramebuffer sol_vkCreateFramebuffer(VkDevice device) {
    PFN_vkCreateFramebuffer fn = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(device, "vkCreateFramebuffer");
    log_print_error_if(!fn, "PFN_vkCreateFramebuffer not present");
    return fn;
}

static inline PFN_vkDestroyFramebuffer sol_vkDestroyFramebuffer(VkDevice device) {
    PFN_vkDestroyFramebuffer fn = (PFN_vkDestroyFramebuffer)vkGetDeviceProcAddr(device, "vkDestroyFramebuffer");
    log_print_error_if(!fn, "PFN_vkDestroyFramebuffer not present");
    return fn;
}

static inline PFN_vkCreateRenderPass sol_vkCreateRenderPass(VkDevice device) {
    PFN_vkCreateRenderPass fn = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(device, "vkCreateRenderPass");
    log_print_error_if(!fn, "PFN_vkCreateRenderPass not present");
    return fn;
}

static inline PFN_vkDestroyRenderPass sol_vkDestroyRenderPass(VkDevice device) {
    PFN_vkDestroyRenderPass fn = (PFN_vkDestroyRenderPass)vkGetDeviceProcAddr(device, "vkDestroyRenderPass");
    log_print_error_if(!fn, "PFN_vkDestroyRenderPass not present");
    return fn;
}

static inline PFN_vkCreatePipelineLayout sol_vkCreatePipelineLayout(VkDevice device) {
    PFN_vkCreatePipelineLayout fn = (PFN_vkCreatePipelineLayout)vkGetDeviceProcAddr(device, "vkCreatePipelineLayout");
    log_print_error_if(!fn, "PFN_vkCreatePipelineLayout not present");
    return fn;
}

static inline PFN_vkDestroyPipelineLayout sol_vkDestroyPipelineLayout(VkDevice device) {
    PFN_vkDestroyPipelineLayout fn = (PFN_vkDestroyPipelineLayout)vkGetDeviceProcAddr(device, "vkDestroyPipelineLayout");
    log_print_error_if(!fn, "PFN_vkDestroyPipelineLayout not present");
    return fn;
}

static inline PFN_vkCreateSemaphore sol_vkCreateSemaphore(VkDevice device) {
    PFN_vkCreateSemaphore fn = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(device, "vkCreateSemaphore");
    log_print_error_if(!fn, "PFN_vkCreateSemaphore not present");
    return fn;
}

static inline PFN_vkDestroySemaphore sol_vkDestroySemaphore(VkDevice device) {
    PFN_vkDestroySemaphore fn = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(device, "vkDestroySemaphore");
    log_print_error_if(!fn, "PFN_vkDestroySemaphore not present");
    return fn;
}

static inline PFN_vkCreateFence sol_vkCreateFence(VkDevice device) {
    PFN_vkCreateFence fn = (PFN_vkCreateFence)vkGetDeviceProcAddr(device, "vkCreateFence");
    log_print_error_if(!fn, "PFN_vkCreateFence not present");
    return fn;
}

static inline PFN_vkDestroyFence sol_vkDestroyFence(VkDevice device) {
    PFN_vkDestroyFence fn = (PFN_vkDestroyFence)vkGetDeviceProcAddr(device, "vkDestroyFence");
    log_print_error_if(!fn, "PFN_vkDestroyFence not present");
    return fn;
}

static inline PFN_vkBeginCommandBuffer sol_vkBeginCommandBuffer(VkDevice device) {
    PFN_vkBeginCommandBuffer fn = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
    log_print_error_if(!fn, "PFN_vkBeginCommandBuffer not present");
    return fn;
}

static inline PFN_vkEndCommandBuffer sol_vkEndCommandBuffer(VkDevice device) {
    PFN_vkEndCommandBuffer fn = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
    log_print_error_if(!fn, "PFN_vkEndCommandBuffer not present");
    return fn;
}

static inline PFN_vkQueueSubmit2 sol_vkQueueSubmit2(VkDevice device) {
    PFN_vkQueueSubmit2 fn = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(device, "vkQueueSubmit2");
    log_print_error_if(!fn, "PFN_vkQueueSubmit2 not present");
    return fn;
}

static inline PFN_vkQueuePresentKHR sol_vkQueuePresentKHR(VkDevice device) {
    PFN_vkQueuePresentKHR fn = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
    log_print_error_if(!fn, "PFN_vkQueuePresentKHR not present");
    return fn;
}

static inline PFN_vkWaitForFences sol_vkWaitForFences(VkDevice device) {
    PFN_vkWaitForFences fn = (PFN_vkWaitForFences)vkGetDeviceProcAddr(device, "vkWaitForFences");
    log_print_error_if(!fn, "PFN_vkWaitForFences not present");
    return fn;
}

static inline PFN_vkResetFences sol_vkResetFences(VkDevice device) {
    PFN_vkResetFences fn = (PFN_vkResetFences)vkGetDeviceProcAddr(device, "vkResetFences");
    log_print_error_if(!fn, "PFN_vkResetFences not present");
    return fn;
}

static inline PFN_vkCmdExecuteCommands sol_vkCmdExecuteCommands(VkDevice device) {
    PFN_vkCmdExecuteCommands fn = (PFN_vkCmdExecuteCommands)vkGetDeviceProcAddr(device, "vkCmdExecuteCommands");
    log_print_error_if(!fn, "PFN_vkCmdExecuteCommands not present");
    return fn;
}

static inline PFN_vkCmdBeginRenderPass sol_vkCmdBeginRenderpass(VkDevice device) {
    PFN_vkCmdBeginRenderPass fn = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass");
    log_print_error_if(!fn, "PFN_vkCmdBeginRenderPass not present");
    return fn;
}

static inline PFN_vkCmdEndRenderPass sol_vkCmdEndRenderpass(VkDevice device) {
    PFN_vkCmdEndRenderPass fn = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(device, "vkCmdEndRenderPass");
    log_print_error_if(!fn, "PFN_vkCmdEndRenderPass not present");
    return fn;
}

static inline PFN_vkCmdNextSubpass sol_vkCmdNextSubpass(VkDevice device) {
    PFN_vkCmdNextSubpass fn = (PFN_vkCmdNextSubpass)vkGetDeviceProcAddr(device, "vkCmdNextSubpass");
    log_print_error_if(!fn, "PFN_vkCmdNextSubpass not present");
    return fn;
}

static inline PFN_vkCmdPipelineBarrier2 sol_vkCmdPipelineBarrier2(VkDevice device) {
    PFN_vkCmdPipelineBarrier2 fn = (PFN_vkCmdPipelineBarrier2)vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2");
    log_print_error_if(!fn, "PFN_vkCmdPipelineBarrier2 not present");
    return fn;
}

static inline PFN_vkCmdCopyBuffer sol_vkCmdCopyBuffer(VkDevice device) {
    PFN_vkCmdCopyBuffer fn = (PFN_vkCmdCopyBuffer)vkGetDeviceProcAddr(device, "vkCmdCopyBuffer");
    log_print_error_if(!fn, "PFN_vkCmdCopyBuffer not present");
    return fn;
}

static inline PFN_vkCmdCopyBufferToImage sol_vkCmdCopyBufferToImage(VkDevice device) {
    PFN_vkCmdCopyBufferToImage fn = (PFN_vkCmdCopyBufferToImage)vkGetDeviceProcAddr(device, "vkCmdCopyBufferToImage");
    log_print_error_if(!fn, "PFN_vkCmdCopyBufferToImage not present");
    return fn;
}

static inline PFN_vkCmdBlitImage2 sol_vkCmdBlitImage2(VkDevice device) {
    PFN_vkCmdBlitImage2 fn = (PFN_vkCmdBlitImage2)vkGetDeviceProcAddr(device, "vkCmdBlitImage2");
    log_print_error_if(!fn, "PFN_vkCmdBlitImage2 not present");
    return fn;
}

static inline PFN_vkCmdBindPipeline sol_vkCmdBindPipeline(VkDevice device) {
    PFN_vkCmdBindPipeline fn = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(device, "vkCmdBindPipeline");
    log_print_error_if(!fn, "PFN_vkCmdBindPipeline not present");
    return fn;
}

static inline PFN_vkCmdBindIndexBuffer sol_vkCmdBindIndexBuffer(VkDevice device) {
    PFN_vkCmdBindIndexBuffer fn = (PFN_vkCmdBindIndexBuffer)vkGetDeviceProcAddr(device, "vkCmdBindIndexBuffer");
    log_print_error_if(!fn, "PFN_vkCmdBindIndexBuffer not present");
    return fn;
}

static inline PFN_vkCmdBindVertexBuffers sol_vkCmdBindVertexBuffers(VkDevice device) {
    PFN_vkCmdBindVertexBuffers fn = (PFN_vkCmdBindVertexBuffers)vkGetDeviceProcAddr(device, "vkCmdBindVertexBuffers");
    log_print_error_if(!fn, "PFN_vkCmdBindVertexBuffers not present");
    return fn;
}

static inline PFN_vkCmdBindDescriptorBuffersEXT sol_vkCmdBindDescriptorBuffersEXT(VkDevice device) {
    PFN_vkCmdBindDescriptorBuffersEXT fn = (PFN_vkCmdBindDescriptorBuffersEXT)vkGetDeviceProcAddr(device, "vkCmdBindDescriptorBuffersEXT");
    log_print_error_if(!fn, "PFN_vkCmdBindDescriptorBuffersEXT not present");
    return fn;
}

static inline PFN_vkCmdSetDescriptorBufferOffsetsEXT sol_vkCmdSetDescriptorBufferOffsetsEXT(VkDevice device) {
    PFN_vkCmdSetDescriptorBufferOffsetsEXT fn = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetDeviceProcAddr(device, "vkCmdSetDescriptorBufferOffsetsEXT");
    log_print_error_if(!fn, "PFN_vkCmdSetDescriptorBufferOffsetsEXT not present");
    return fn;
}

static inline PFN_vkCmdPushConstants sol_vkCmdPushConstants(VkDevice device) {
    PFN_vkCmdPushConstants fn = (PFN_vkCmdPushConstants)vkGetDeviceProcAddr(device, "vkCmdPushConstants");
    log_print_error_if(!fn, "PFN_vkCmdPushConstants not present");
    return fn;
}

static inline PFN_vkCmdDrawIndexed sol_vkCmdDrawIndexed(VkDevice device) {
    PFN_vkCmdDrawIndexed fn = (PFN_vkCmdDrawIndexed)vkGetDeviceProcAddr(device, "vkCmdDrawIndexed");
    log_print_error_if(!fn, "PFN_vkCmdDrawIndexed not present");
    return fn;
}

static inline PFN_vkCmdDraw sol_vkCmdDraw(VkDevice device) {
    PFN_vkCmdDraw fn = (PFN_vkCmdDraw)vkGetDeviceProcAddr(device, "vkCmdDraw");
    log_print_error_if(!fn, "PFN_vkCmdDraw not present");
    return fn;
}

struct vulkan_dispatch_table vulkan_dispatch_table;

void init_vk_dispatch_table(int stage, VkInstance instance, VkDevice device)
{
    switch(stage) {
        case SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_INSTANCE:
            vulkan_dispatch_table.create_instance = sol_vkCreateInstance();
            vulkan_dispatch_table.enumerate_instance_extension_properties = sol_vkEnumerateInstanceExtensionProperties();
            break;
        case SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_DEVICE:
            vulkan_dispatch_table.destroy_instance = sol_vkDestroyInstance(instance);
            #if DEBUG
            vulkan_dispatch_table.create_debug_utils_messenger_ext = sol_vkCreateDebugUtilsMessengerEXT(instance);
            vulkan_dispatch_table.destroy_debug_utils_messenger_ext = sol_vkDestroyDebugUtilsMessengerEXT(instance);
            #endif
            break;
        case SOL_VK_DISPATCH_TABLE_INIT_STAGE_POST_DEVICE:
            vulkan_dispatch_table.create_image_view                            = sol_vkCreateImageView(device);
            vulkan_dispatch_table.destroy_image_view                           = sol_vkDestroyImageView(device);
            vulkan_dispatch_table.create_swapchain_khr                         = sol_vkCreateSwapchainKHR(device);
            vulkan_dispatch_table.get_swapchain_images_khr                     = sol_vkGetSwapchainImagesKHR(device);
            vulkan_dispatch_table.destroy_swapchain_khr                        = sol_vkDestroySwapchainKHR(device);
            vulkan_dispatch_table.get_buffer_device_address                    = sol_vkGetBufferDeviceAddress(device);
            vulkan_dispatch_table.create_descriptor_set_layout                 = sol_vkCreateDescriptorSetLayout(device);
            vulkan_dispatch_table.destroy_descriptor_set_layout                = sol_vkDestroyDescriptorSetLayout(device);
            vulkan_dispatch_table.get_descriptor_ext                           = sol_vkGetDescriptorEXT(device);
            vulkan_dispatch_table.get_descriptor_set_layout_binding_offset_ext = sol_vkGetDescriptorSetLayoutBindingOffsetEXT(device);
            vulkan_dispatch_table.get_descriptor_set_layout_size_ext           = sol_vkGetDescriptorSetLayoutSizeEXT(device);
            vulkan_dispatch_table.create_shader_module                         = sol_vkCreateShaderModule(device);
            vulkan_dispatch_table.destroy_shader_module                        = sol_vkDestroyShaderModule(device);
            vulkan_dispatch_table.create_buffer                                = sol_vkCreateBuffer(device);
            vulkan_dispatch_table.destroy_buffer                               = sol_vkDestroyBuffer(device);
            vulkan_dispatch_table.create_image                                 = sol_vkCreateImage(device);
            vulkan_dispatch_table.destroy_image                                = sol_vkDestroyImage(device);
            vulkan_dispatch_table.create_image_view                            = sol_vkCreateImageView(device);
            vulkan_dispatch_table.destroy_image_view                           = sol_vkDestroyImageView(device);
            vulkan_dispatch_table.get_image_memory_requirements                = sol_vkGetImageMemoryRequirements(device);
            vulkan_dispatch_table.bind_image_memory                            = sol_vkBindImageMemory(device);
            vulkan_dispatch_table.create_sampler                               = sol_vkCreateSampler(device);
            vulkan_dispatch_table.destroy_sampler                              = sol_vkDestroySampler(device);
            vulkan_dispatch_table.create_graphics_pipelines                    = sol_vkCreateGraphicsPipelines(device);
            vulkan_dispatch_table.destroy_pipeline                             = sol_vkDestroyPipeline(device);
            vulkan_dispatch_table.create_command_pool                          = sol_vkCreateCommandPool(device);
            vulkan_dispatch_table.destroy_command_pool                         = sol_vkDestroyCommandPool(device);
            vulkan_dispatch_table.reset_command_pool                           = sol_vkResetCommandPool(device);
            vulkan_dispatch_table.allocate_command_buffers                     = sol_vkAllocateCommandBuffers(device);
            vulkan_dispatch_table.create_framebuffer                           = sol_vkCreateFramebuffer(device);
            vulkan_dispatch_table.destroy_framebuffer                          = sol_vkDestroyFramebuffer(device);
            vulkan_dispatch_table.create_renderpass                            = sol_vkCreateRenderPass(device);
            vulkan_dispatch_table.destroy_renderpass                           = sol_vkDestroyRenderPass(device);
            vulkan_dispatch_table.create_pipeline_layout                       = sol_vkCreatePipelineLayout(device);
            vulkan_dispatch_table.destroy_pipeline_layout                      = sol_vkDestroyPipelineLayout(device);
            vulkan_dispatch_table.create_semaphore                             = sol_vkCreateSemaphore(device);
            vulkan_dispatch_table.destroy_semaphore                            = sol_vkDestroySemaphore(device);
            vulkan_dispatch_table.create_fence                                 = sol_vkCreateFence(device);
            vulkan_dispatch_table.destroy_fence                                = sol_vkDestroyFence(device);
            vulkan_dispatch_table.begin_command_buffer                         = sol_vkBeginCommandBuffer(device);
            vulkan_dispatch_table.end_command_buffer                           = sol_vkEndCommandBuffer(device);
            vulkan_dispatch_table.queue_submit2                                = sol_vkQueueSubmit2(device);
            vulkan_dispatch_table.queue_present_khr                            = sol_vkQueuePresentKHR(device);
            vulkan_dispatch_table.wait_for_fences                              = sol_vkWaitForFences(device);
            vulkan_dispatch_table.reset_fences                                 = sol_vkResetFences(device);
            vulkan_dispatch_table.cmd_copy_buffer                              = sol_vkCmdCopyBuffer(device);
            vulkan_dispatch_table.cmd_copy_buffer_to_image                     = sol_vkCmdCopyBufferToImage(device);
            vulkan_dispatch_table.cmd_execute_commands                         = sol_vkCmdExecuteCommands(device);
            vulkan_dispatch_table.cmd_begin_renderpass                         = sol_vkCmdBeginRenderpass(device);
            vulkan_dispatch_table.cmd_end_renderpass                           = sol_vkCmdEndRenderpass(device);
            vulkan_dispatch_table.cmd_next_subpass                             = sol_vkCmdNextSubpass(device);
            vulkan_dispatch_table.cmd_pipeline_barrier2                        = sol_vkCmdPipelineBarrier2(device);
            vulkan_dispatch_table.cmd_blit_image_2                             = sol_vkCmdBlitImage2(device);
            vulkan_dispatch_table.cmd_bind_pipeline                            = sol_vkCmdBindPipeline(device);
            vulkan_dispatch_table.cmd_bind_index_buffer                        = sol_vkCmdBindIndexBuffer(device);
            vulkan_dispatch_table.cmd_bind_vertex_buffers                      = sol_vkCmdBindVertexBuffers(device);
            vulkan_dispatch_table.cmd_bind_descriptor_buffers_ext              = sol_vkCmdBindDescriptorBuffersEXT(device);
            vulkan_dispatch_table.cmd_set_descriptor_buffer_offsets_ext        = sol_vkCmdSetDescriptorBufferOffsetsEXT(device);
            vulkan_dispatch_table.cmd_push_constants                           = sol_vkCmdPushConstants(device);
            vulkan_dispatch_table.cmd_draw_indexed                             = sol_vkCmdDrawIndexed(device);
            vulkan_dispatch_table.cmd_draw                                     = sol_vkCmdDraw(device);
            break;
        default:
            log_print_error("unrecognised vk dispatch table init stage");
            break;
    }
}

