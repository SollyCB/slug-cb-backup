#include "defs.h"
#include "gpu.h"
#include "sol_vulkan.h"
#include "vulkan_errors.h"
#include "glfw.h"
#include "file.h"
#include "image.h"
#include "gltf.h"
#include "spirv.h"
#include "thread.h"
#include "math.h"
#include "blend_types.h"

#if DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL gpu_debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ;//println("\nValidation Layer: %s", pCallbackData->pMessage);

    return VK_FALSE;
}

static void gpu_fill_vk_debug_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *ret)
{
    *ret = (VkDebugUtilsMessengerCreateInfoEXT){.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

    ret->messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ret->messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    // VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
    ret->pfnUserCallback = gpu_debug_messenger_callback;
}

static void gpu_create_debug_messenger(struct gpu *gpu)
{
    VkDebugUtilsMessengerCreateInfoEXT info;
    gpu_fill_vk_debug_messenger_info(&info);
    VkResult check = vk_create_debug_utils_messenger_ext(gpu->instance, &info, NULL, &gpu->dbg);
    DEBUG_VK_OBJ_CREATION(vkCreateDebugUtilsMessengerEXT, check)
}
#endif

#define VMA 0
#define SHADER_INIT_INFO 0
#define SHADER_HOTLOAD 0
#define HOTLOAD_PRINT_STATUS 1

#if HOTLOAD_PRINT_STATUS
#define hotload_status(...) println(__VA_ARGS__)
#else
#define hotload_status(...)
#endif

static void gpu_create_instance(struct gpu *gpu);
static void gpu_create_device_and_queues(struct gpu *gpu);
static void gpu_create_memory_resources(struct gpu *gpu);
static void gpu_destroy_memory_resources(struct gpu *gpu);
static void gpu_create_swapchain(struct gpu *gpu, struct window *glfw);
static void gpu_destroy_swapchain(struct gpu *gpu);
static void gpu_recreate_swapchain(struct gpu *gpu);
static void gpu_reset_viewport_and_scissor_to_window_extent(struct gpu *gpu);
static void gpu_store_pipeline_cache(struct gpu *gpu);
static void gpu_load_pipeline_cache(struct gpu *gpu);

void init_gpu(struct gpu *gpu, struct init_gpu_args *args) {
    gpu->flags = 0;
    gpu->hotload_flags = 0;

    init_vk_dispatch_table(SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_INSTANCE, NULL, NULL);
    gpu_create_instance(gpu);

    gpu->threads = args->threads;
    gpu->alloc_heap = args->alloc_heap;
    gpu->alloc_temp = args->alloc_temp;

    init_vk_dispatch_table(SOL_VK_DISPATCH_TABLE_INIT_STAGE_PRE_DEVICE, gpu->instance, NULL);

#if DEBUG
    gpu_create_debug_messenger(gpu);
#endif

    gpu_create_device_and_queues(gpu);
    init_vk_dispatch_table(SOL_VK_DISPATCH_TABLE_INIT_STAGE_POST_DEVICE, NULL, gpu->device);

    gpu_create_swapchain(gpu, args->glfw);
    gpu_reset_viewport_and_scissor_to_window_extent(gpu);

    gpu_create_memory_resources(gpu);

    gpu_load_pipeline_cache(gpu);

    gpu->defaults.dynamic_state_count = 0; // @Unused.
    gpu->defaults.dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
    gpu->defaults.dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;

    gpu->defaults.blend_constants[0] = 0;
    gpu->defaults.blend_constants[1] = 0;
    gpu->defaults.blend_constants[2] = 0;
    gpu->defaults.blend_constants[3] = 0;

    gpu->flags |= GPU_SAMPLER_ANISOTROPY_ENABLE_BIT;
    gpu->defaults.sampler_anisotropy = gpu->props.limits.maxSamplerAnisotropy; // @Whatever

    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkResult check = vk_create_sampler(gpu->device, &sampler_info, GAC, &gpu->defaults.sampler);
    DEBUG_VK_OBJ_CREATION(vkCreateSampler, check);

    // @Todo Make a sampler allocator
    gpu->sampler_count = 1;

    gpu->defaults.texture.image.image = load_image("models/default/texture.png");

    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.extent = (VkExtent3D){gpu->defaults.texture.image.image.x, gpu->defaults.texture.image.image.y, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    check = vk_create_image(gpu->device, &image_info, GAC, &gpu->defaults.texture.image.vkimage);
    DEBUG_VK_OBJ_CREATION(vkCreateImage, check);

    VkMemoryRequirements mr;
    vk_get_image_memory_requirements(gpu->device, gpu->defaults.texture.image.vkimage, &mr);
    gpu->defaults.texture.image.size = mr.size;

    assert(gpu->mem.texture_memory.used == 0); // should not need aligning
    vk_bind_image_memory(gpu->device, gpu->defaults.texture.image.vkimage, gpu->mem.texture_memory.mem, gpu->mem.texture_memory.used);

    gpu->mem.texture_memory.used += align(mr.size, mr.alignment);
    assert(gpu->mem.texture_memory.used < gpu->mem.texture_memory.size);

    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = gpu->defaults.texture.image.vkimage;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    check = vk_create_image_view(gpu->device, &view_info, GAC, &gpu->defaults.texture.image.view);
    DEBUG_VK_OBJ_CREATION(vkCreateImageView, check);

    VkDescriptorImageInfo img_desc_info = {gpu->defaults.sampler,gpu->defaults.texture.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkDescriptorDataEXT desc_data;
    desc_data.pCombinedImageSampler = &img_desc_info;

    VkDescriptorGetInfoEXT desc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    desc_info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    desc_info.data = desc_data;

    log_print_error_if(gpu->descriptors.props.combinedImageSamplerDescriptorSize > GPU_MAX_DESCRIPTOR_SIZE, "%u, %u",gpu->descriptors.props.combinedImageSamplerDescriptorSize, GPU_MAX_DESCRIPTOR_SIZE);
    vk_get_descriptor_ext(gpu->device, &desc_info, gpu->descriptors.props.combinedImageSamplerDescriptorSize, gpu->defaults.texture.descriptor);

    gpu->shader_dir = load_shader_dir(gpu, gpu->alloc_heap);

    gpu->settings.shadow_maps.width = 4096 / 2;
    gpu->settings.shadow_maps.height = 4096 / 2;
}

void shutdown_gpu(struct gpu *gpu) {
    gpu_store_pipeline_cache(gpu);

    free_image(&gpu->defaults.texture.image.image);
    vk_destroy_image(gpu->device, gpu->defaults.texture.image.vkimage, GAC);
    vk_destroy_image_view(gpu->device, gpu->defaults.texture.image.view, GAC);
    vk_destroy_sampler(gpu->device ,gpu->defaults.sampler, GAC);

    gpu_destroy_memory_resources(gpu);
    gpu_destroy_swapchain(gpu);
    vkDestroyDevice(gpu->device, GPU_ALLOCATION_CALLBACKS);

#if DEBUG
    vk_destroy_debug_utils_messenger_ext(gpu->instance, gpu->dbg, GPU_ALLOCATION_CALLBACKS);
#endif

    vk_destroy_instance(gpu->instance, GPU_ALLOCATION_CALLBACKS);
}

// @Update Was being used for shader hotloading, but I am only using auto shaders atm,
// which are checked and reloaded elsewhere.
void gpu_poll_hotloader(struct gpu *gpu)
{
}

static void gpu_create_instance(struct gpu *gpu)
{
    VkInstanceCreateInfo instance_create_info;
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.flags = 0x0;

    VkApplicationInfo application_info = (VkApplicationInfo){
        VK_STRUCTURE_TYPE_APPLICATION_INFO,  // sType
        NULL,                                // pNext
        "SlugApp",                           // pApplicationName
        VK_MAKE_VERSION(0,0,0),              // applicationVersion
        "SlugEngine",                        // pEngineName
        VK_MAKE_VERSION(0,0,0),              // engineVersion
        VK_API_VERSION_1_3,                  // apiVersion
    };
    instance_create_info.pApplicationInfo = &application_info;

#if DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dbg;
    gpu_fill_vk_debug_messenger_info(&dbg);
    instance_create_info.pNext = &dbg;

    uint enabled_layer_cnt = 1;
    const char *enabled_layer_names[] = {
        "VK_LAYER_KHRONOS_validation",
    };

    uint enabled_ext_cnt = 2;
    const char *enabled_ext_names[] = {
        "VK_EXT_validation_features",
        "VK_EXT_debug_utils" // deprecated in header 272, replacement: "VK_EXT_layer_settings",
    };
#else
    uint enabled_layer_cnt = 0;
    const char *enabled_layer_names[] = {};

    const uint enabled_ext_cnt = 0;
    const char *enabled_ext_names[] = {};
#endif

#if 0
    uint cnt;
    vk_enumerate_instance_extension_properties(NULL, &cnt, NULL);
    VkExtensionProperties props[cnt];
    vk_enumerate_instance_extension_properties(NULL, &cnt, props);
    for(uint j = 0; j < cnt; ++j)
        println("%s", props[j].extensionName);
    uint cnt;
    vkEnumerateInstanceLayerProperties(&cnt, NULL);
    VkLayerProperties props_l[cnt];
    vkEnumerateInstanceLayerProperties(&cnt, props_l);
    for(uint j = 0; j < cnt; ++j)
        println("%s", props_l[j].layerName);
#endif

    uint glfw_ext_cnt;
    const char **glfw_ext_names = glfwGetRequiredInstanceExtensions(&glfw_ext_cnt);
    assert(glfw_ext_cnt);

    const char *ext_names[20];
    uint i;
    for(i=0; i < glfw_ext_cnt; ++i)
        ext_names[i] = glfw_ext_names[i];
    for(;i < enabled_ext_cnt + glfw_ext_cnt; ++i)
        ext_names[i] = enabled_ext_names[i - glfw_ext_cnt];

    instance_create_info.enabledExtensionCount = i;
    instance_create_info.ppEnabledExtensionNames = ext_names;

    instance_create_info.enabledLayerCount = enabled_layer_cnt;
    instance_create_info.ppEnabledLayerNames = enabled_layer_names;

    VkResult check = vk_create_instance(&instance_create_info, GPU_ALLOCATION_CALLBACKS, &gpu->instance);
    DEBUG_VK_OBJ_CREATION(vkCreateInstance, check);
}

static void gpu_create_device_and_queues(struct gpu *gpu)
{
    uint32_t ext_count = 3;
    const char *ext_names[] = {
        "VK_KHR_swapchain",
        "VK_EXT_memory_priority",
        "VK_EXT_descriptor_buffer",
    };

    VkPhysicalDeviceFeatures vk1_features = {
        .samplerAnisotropy = VK_TRUE,
        .fillModeNonSolid = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .storagePushConstant16 = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vk11_features,
        .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
        .storagePushConstant8 = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = (void*)&vk12_features,
        .synchronization2 = VK_TRUE,
    };
    VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_priority = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
        .pNext = &vk13_features,
        .memoryPriority = VK_TRUE,
    };
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
        .pNext = &mem_priority,
        .descriptorBuffer = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features_full = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &descriptor_buffer_features,
        .features = vk1_features,
    };

    VkPhysicalDeviceFeatures vk1_features_unfilled = vk1_features;
    VkPhysicalDeviceVulkan12Features vk12_features_unfilled = vk12_features;

    VkPhysicalDeviceVulkan13Features vk13_features_unfilled = vk13_features;
    vk13_features_unfilled.pNext = &vk12_features_unfilled;

    VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_priority_empty =  mem_priority;
    mem_priority_empty.pNext = &vk13_features_unfilled;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_empty = descriptor_buffer_features;
    descriptor_buffer_empty.pNext = &mem_priority_empty;

    VkPhysicalDeviceFeatures2 features_full_unfilled = features_full;
    features_full_unfilled.pNext = &descriptor_buffer_empty;

    features_full_unfilled.features = vk1_features_unfilled;

    // choose physical device
    uint physical_device_count;
    vkEnumeratePhysicalDevices(gpu->instance, &physical_device_count, NULL);
    VkPhysicalDevice physical_devices[physical_device_count];
    vkEnumeratePhysicalDevices(gpu->instance, &physical_device_count, physical_devices);

    int graphics_queue_index;
    int transfer_queue_index;
    int presentation_queue_index;
    int physical_device_index = -1;

    int backup_graphics_queue_index = -1;
    int backup_presentation_queue_index = -1; // -Wunused
    int backup_physical_device_index = -1;

    // @Todo prefer certain gpus eg discrete
    for(uint i = 0; i < physical_device_count; ++i) {

        vkGetPhysicalDeviceFeatures2(physical_devices[i], &features_full);

        bool incompatible = false;
        if (mem_priority.memoryPriority == VK_FALSE) {
            println("Device Index %u does not support Memory Priority", i);
            incompatible = true;
        }
        if (descriptor_buffer_features.descriptorBuffer == VK_FALSE) {
            println("Device Index %u does not support Descriptor Buffer", i);
            incompatible = true;
        }

        if (incompatible)
            continue;

        uint queue_family_count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties queue_family_props[queue_family_count];
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_family_props);

        graphics_queue_index = -1;
        transfer_queue_index = -1;
        presentation_queue_index = -1;

        bool break_outer = false;
        for(uint j = 0; j < queue_family_count;++j) {
            if (glfwGetPhysicalDevicePresentationSupport(gpu->instance, physical_devices[i], j) &&
                presentation_queue_index == -1)
            {
                presentation_queue_index = j;
            }
            if (queue_family_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                queue_family_props[j].queueFlags & VK_QUEUE_TRANSFER_BIT &&
                graphics_queue_index == -1)
            {
                graphics_queue_index = j;
            }
            if (queue_family_props[j].queueFlags & VK_QUEUE_TRANSFER_BIT    &&
                !(queue_family_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                transfer_queue_index == -1)
            {
                transfer_queue_index = j;
            }
            if (transfer_queue_index != -1 && graphics_queue_index != -1 &&
                presentation_queue_index != -1)
            {
                physical_device_index = i;
                break_outer = true;
                break;
            }
        }

        if (break_outer)
            break;

        if (backup_physical_device_index == -1 && graphics_queue_index != -1 && presentation_queue_index != -1) {
            backup_presentation_queue_index = presentation_queue_index;
            backup_graphics_queue_index = graphics_queue_index;
            backup_physical_device_index = i;
        }

        continue;
    }

    if (physical_device_index == -1) {
        if (backup_physical_device_index == -1) {
            log_print_error("Failed to find suitable device");
            return;
        }
        log_print_error_if(backup_physical_device_index == -1, "Having to use backup device");
        physical_device_index = backup_physical_device_index;
        graphics_queue_index = backup_graphics_queue_index;
        presentation_queue_index = backup_presentation_queue_index;
        transfer_queue_index = graphics_queue_index;
    }

    gpu->descriptors.props = (VkPhysicalDeviceDescriptorBufferPropertiesEXT){VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &gpu->descriptors.props;
    vkGetPhysicalDeviceProperties2(physical_devices[physical_device_index], &props2);
    gpu->props = props2.properties;

    VkDeviceQueueCreateInfo graphics_queue_create_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    graphics_queue_create_info.queueFamilyIndex = graphics_queue_index;

    // AMD article about multiple queues. Seems that one graphics queue and one
    // async compute is a fine idea. Using too many queues apparently sucks
    // resources... This is smtg to come back to maybe. This article is 2016...
    // https://gpuopen.com/learn/concurrent-execution-asynchronous-queues/
    graphics_queue_create_info.queueCount = 1;

    float queue_priorities = 1.0f;
    graphics_queue_create_info.pQueuePriorities = &queue_priorities;

    uint queue_info_count = 1;
    VkDeviceQueueCreateInfo transfer_queue_create_info;

    if (transfer_queue_index != graphics_queue_index) {
        gpu->flags |= GPU_DISCRETE_TRANSFER_BIT;
        println("Selected Device (Primary Choice) %s", gpu->props.deviceName);

        queue_info_count++;
        transfer_queue_create_info = graphics_queue_create_info;
        transfer_queue_create_info.queueFamilyIndex = transfer_queue_index;
    } else {
        println("Selected Device (Backup) %s", gpu->props.deviceName);
    }

    VkDeviceQueueCreateInfo queue_infos[2];
    queue_infos[0] = graphics_queue_create_info;
    queue_infos[1] = transfer_queue_create_info;

    VkDeviceCreateInfo device_create_info      = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.pNext                   = &features_full_unfilled;
    device_create_info.queueCreateInfoCount    = queue_info_count;
    device_create_info.pQueueCreateInfos       = queue_infos;
    device_create_info.enabledExtensionCount   = ext_count;
    device_create_info.ppEnabledExtensionNames = ext_names;
    device_create_info.pEnabledFeatures        = NULL;

    VkResult check = vkCreateDevice(physical_devices[physical_device_index], &device_create_info, GPU_ALLOCATION_CALLBACKS, &gpu->device);
    DEBUG_VK_OBJ_CREATION(vkCreateDevice, check);

    gpu->physical_device = physical_devices[physical_device_index];
    gpu->graphics_queue_index = graphics_queue_index;
    gpu->present_queue_index  = presentation_queue_index;
    gpu->transfer_queue_index = transfer_queue_index;

    VkDevice device = gpu->device;
    vkGetDeviceQueue(device, graphics_queue_index, 0, &gpu->graphics_queue);

    // if queue indices are equivalent, dont get twice
    if (presentation_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(device, presentation_queue_index, 0, &gpu->present_queue);
    } else {
        gpu->present_queue = gpu->graphics_queue;
    }

    // if queue indices are equivalent, dont get twice
    if (transfer_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(device, transfer_queue_index, 0, &gpu->transfer_queue);
        gpu->flags |= GPU_DISCRETE_TRANSFER_BIT;
    } else {
        gpu->transfer_queue = gpu->graphics_queue;
    }

    if (gpu->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        gpu->flags |= GPU_UMA_BIT;

    // checks
    log_print_error_if(
                       SHADER_MAX_DESCRIPTOR_SET_COUNT > gpu->props.limits.maxBoundDescriptorSets,
                       "Hardware cannot support the application maximum for bound descriptor sets, \
                       application max = %u, hardward max = %u",
                       SHADER_MAX_DESCRIPTOR_SET_COUNT, gpu->props.limits.maxBoundDescriptorSets);
} // func create_device()

#define GPU_SHADOW_ATTACHMENT_WIDTH 640
#define GPU_SHADOW_ATTACHMENT_HEIGHT 480
#define GPU_BIND_BUFFER_SIZE 1048576 // align(1000000, 16)
#define GPU_TRANSFER_BUFFER_SIZE align(32000000, 16)
#define GPU_TEXTURE_MEMORY_SIZE align(64000000, 16)
#define GPU_DESCRIPTOR_BUFFER_SIZE_RESOURCE 1048576 / 2 // align(100000, 16)
#define GPU_DESCRIPTOR_BUFFER_SIZE_SAMPLER 1048576 / 2 // align(100000, 16)

#define GPU_DEPTH_ATTACHMENT_FORMAT  VK_FORMAT_D16_UNORM
#define GPU_SHADOW_ATTACHMENT_FORMAT VK_FORMAT_D16_UNORM

#define GPU_HDR_COLOR_ATTACHMENT_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

static uint gpu_get_memory_type(uint mask, bool prefer_device, bool prefer_host, uint32 gpu_flags, VkPhysicalDeviceMemoryProperties *pd)
{
    uint32 req = 0;
    req |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT & max32_if_true(prefer_device);
    req |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT & max32_if_true(prefer_host);
    req |= (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        & max32_if_true((gpu_flags & GPU_UMA_BIT) && !(gpu_flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT));

    size_t s = 0;
    uint pc = pop_count32(mask);
    uint i,tz;
    uint ret = Max_u32;
    for(i = 0; i < pc; ++i) {
        tz = ctz32(mask);
        mask &= ~(1 << tz);
        if ((pd->memoryTypes[tz].propertyFlags & req) == req &&
            pd->memoryHeaps[pd->memoryTypes[tz].heapIndex].size > s &&
            !(pd->memoryTypes[tz].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD))
        {
            ret = tz;
            s = pd->memoryHeaps[tz].size;
        }
    }
    return ret;
}

#define GPU_RESOURCE_DESCRIPTOR_BUFFER_BIND_INDEX 0
#define GPU_SAMPLER_DESCRIPTOR_BUFFER_BIND_INDEX 1

#define GPU_MATERIAL_TEXTURES_SET_INDEX GPU_SAMPLER_DESCRIPTOR_BUFFER_BIND_INDEX
#define GPU_MATERIAL_UNIFORMS_SET_INDEX GPU_RESOURCE_DESCRIPTOR_BUFFER_BIND_INDEX

static void gpu_create_memory_resources(struct gpu *gpu)
{
    VkDevice d = gpu->device;
    VkResult check;
    bool prefer_device = true;
    bool prefer_host = true;

    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkPhysicalDeviceMemoryProperties pd_props;
    VkMemoryRequirements mr;

    VkMemoryAllocateFlagsInfo allocate_flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    allocate_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    uint i, t_idx;

    { // Depth Attachments
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.extent = (VkExtent3D){gpu->swapchain.info.imageExtent.width, gpu->swapchain.info.imageExtent.height, 1};
        image_info.format = GPU_DEPTH_ATTACHMENT_FORMAT;

        for(i = 0; i < GPU_DEPTH_ATTACHMENT_COUNT; ++i) {
            check = vk_create_image(d, &image_info, GAC, &gpu->mem.depth_attachments[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
        }
        // @Todo shadow

        vkGetPhysicalDeviceMemoryProperties(gpu->physical_device, &pd_props);
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(d, gpu->mem.depth_attachments[0], &mr);

        t_idx = gpu_get_memory_type(mr.memoryTypeBits, prefer_device, !prefer_host, gpu->flags, &pd_props);

        alloc_info.pNext = &allocate_flags;
        alloc_info.allocationSize = mr.size;
        alloc_info.memoryTypeIndex = t_idx;

        for(i = 0; i < GPU_DEPTH_ATTACHMENT_COUNT; ++i) {
            check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.depth_mems[i]);
            DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);
            vkBindImageMemory(d, gpu->mem.depth_attachments[i], gpu->mem.depth_mems[i], 0);
            DEBUG_VK_OBJ_CREATION(vkBindImageMemory, check);
        }

        VkImageSubresourceRange subresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = GPU_DEPTH_ATTACHMENT_FORMAT;
        view_info.subresourceRange = subresource;

        for(i=0;i<GPU_DEPTH_ATTACHMENT_COUNT;++i) {
            view_info.image = gpu->mem.depth_attachments[i];
            check = vk_create_image_view(d, &view_info, GAC, &gpu->mem.depth_views[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
        }
    } // Depth Attachments

    { // HDR Color Attachments
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.extent = (VkExtent3D){gpu->swapchain.info.imageExtent.width, gpu->swapchain.info.imageExtent.height, 1};
        image_info.format = GPU_HDR_COLOR_ATTACHMENT_FORMAT;

        uint i;
        for(i = 0; i < GPU_HDR_COLOR_ATTACHMENT_COUNT; ++i) {
            check = vk_create_image(d, &image_info, GAC, &gpu->mem.hdr_color_attachments[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
        }

        vkGetPhysicalDeviceMemoryProperties(gpu->physical_device, &pd_props);
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(d, gpu->mem.hdr_color_attachments[0], &mr);

        uint t_idx = gpu_get_memory_type(mr.memoryTypeBits, prefer_device, !prefer_host, gpu->flags, &pd_props);

        alloc_info.pNext = &allocate_flags;
        alloc_info.allocationSize = mr.size;
        alloc_info.memoryTypeIndex = t_idx;

        for(i = 0; i < GPU_HDR_COLOR_ATTACHMENT_COUNT; ++i) {
            check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.hdr_color_mems[i]);
            DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);
            vkBindImageMemory(d, gpu->mem.hdr_color_attachments[i], gpu->mem.hdr_color_mems[i], 0);
            DEBUG_VK_OBJ_CREATION(vkBindImageMemory, check);
        }

        VkImageSubresourceRange subresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = GPU_HDR_COLOR_ATTACHMENT_FORMAT;
        view_info.subresourceRange = subresource;

        for(i=0;i<GPU_HDR_COLOR_ATTACHMENT_COUNT;++i) {
            view_info.image = gpu->mem.hdr_color_attachments[i];
            check = vk_create_image_view(d, &view_info, GAC, &gpu->mem.hdr_color_views[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
        }
    } // HDR Color Attachments

    // Texture Memory
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;

    VkImage tmp;
    check = vk_create_image(d, &image_info, GAC, &tmp);
    DEBUG_VK_OBJ_CREATION(vkCreateImage, check);

    vkGetImageMemoryRequirements(d, tmp, &mr);
    t_idx = gpu_get_memory_type(mr.memoryTypeBits, prefer_device, !prefer_host, gpu->flags, &pd_props);

    alloc_info.allocationSize = GPU_TEXTURE_MEMORY_SIZE;
    alloc_info.memoryTypeIndex = t_idx;
    check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.texture_memory.mem);
    DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);

    gpu->mem.texture_memory.size = GPU_TEXTURE_MEMORY_SIZE;
    gpu->mem.texture_memory.used = 0;
    vk_destroy_image(d, tmp, GAC);

    // Bind Buffer
    VkBufferCreateInfo buf_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buf_info.size = GPU_BIND_BUFFER_SIZE;
    buf_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buf_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT & max32_if_false(gpu->flags & GPU_UMA_BIT);
    check = vk_create_buffer(d, &buf_info, GAC, &gpu->mem.bind_buffer.buf);
    DEBUG_VK_OBJ_CREATION(vkCreateBuffer, check);

    gpu->mem.bind_buffer.size = GPU_BIND_BUFFER_SIZE;
    gpu->mem.bind_buffer.used = 0;
    gpu->mem.bind_buffer.usage = buf_info.usage;

    vkGetBufferMemoryRequirements(d, gpu->mem.bind_buffer.buf, &mr);
    t_idx = gpu_get_memory_type(mr.memoryTypeBits, prefer_device, !prefer_host, gpu->flags, &pd_props);

    alloc_info.allocationSize = mr.size;
    alloc_info.memoryTypeIndex = t_idx;

    check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.bind_mem);
    DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);

    check = vkBindBufferMemory(d, gpu->mem.bind_buffer.buf, gpu->mem.bind_mem, 0);
    DEBUG_VK_OBJ_CREATION(vkBindBufferMemory, check);

    if (gpu->flags & GPU_UMA_BIT) {
        check = vkMapMemory(d, gpu->mem.bind_mem, 0, VK_WHOLE_SIZE, 0x0, (void**)&gpu->mem.bind_buffer.data);
        DEBUG_VK_OBJ_CREATION(vkMapMemory, check);
    }

    VkBufferDeviceAddressInfo addr = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addr.buffer = gpu->mem.bind_buffer.buf;
    gpu->mem.bind_buffer.address = vkGetBufferDeviceAddress(d, &addr);

    // Transfer Buffer
    buf_info.size = GPU_TRANSFER_BUFFER_SIZE;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check = vk_create_buffer(d, &buf_info, GAC, &gpu->mem.transfer_buffer.buf);
    DEBUG_VK_OBJ_CREATION(vkCreateBuffer, check);

    gpu->mem.transfer_buffer.size = GPU_TRANSFER_BUFFER_SIZE;
    gpu->mem.transfer_buffer.used = 0;
    gpu->mem.transfer_buffer.usage = buf_info.usage;

    vkGetBufferMemoryRequirements(d, gpu->mem.transfer_buffer.buf, &mr);
    t_idx = gpu_get_memory_type(mr.memoryTypeBits, !prefer_device, prefer_host, gpu->flags, &pd_props);

    alloc_info.allocationSize = mr.size;
    alloc_info.memoryTypeIndex = t_idx;

    check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.transfer_mem);
    DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);

    check = vkBindBufferMemory(d, gpu->mem.transfer_buffer.buf, gpu->mem.transfer_mem, 0);
    DEBUG_VK_OBJ_CREATION(vkBindBufferMemory, check);

    check = vkMapMemory(d, gpu->mem.transfer_mem, 0, VK_WHOLE_SIZE, 0x0, (void**)&gpu->mem.transfer_buffer.data);
    DEBUG_VK_OBJ_CREATION(vkMapMemory, check);

    // Descriptor Buffer
    //
    // @Test I put the transfer bit in the usage flags, but this is not necessary if the descriptor buffer is
    // host visible, but in order to find that out I have to create the buffer. Idk if it actually introduces
    // any inefficiencies (maybe the flag is ignored if the buffer is host visible, for instance). So really
    // optimal code (I if there are inefficiencies) would create the buffer, get the requirements, then destroy
    // it if the flag is unnecessary.
    //
    buf_info.size = GPU_DESCRIPTOR_BUFFER_SIZE_RESOURCE;
    buf_info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check = vk_create_buffer(d, &buf_info, GAC, &gpu->mem.descriptor_buffer_resource.buf);
    DEBUG_VK_OBJ_CREATION(vkCreateBuffer, check);

    gpu->mem.descriptor_buffer_resource.size = GPU_DESCRIPTOR_BUFFER_SIZE_RESOURCE;
    gpu->mem.descriptor_buffer_resource.used = 0;
    gpu->mem.descriptor_buffer_resource.usage = buf_info.usage;

    buf_info.size = GPU_DESCRIPTOR_BUFFER_SIZE_SAMPLER;
    buf_info.usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check = vk_create_buffer(d, &buf_info, GAC, &gpu->mem.descriptor_buffer_sampler.buf);
    DEBUG_VK_OBJ_CREATION(vkCreateBuffer, check);

    gpu->mem.descriptor_buffer_sampler.size = GPU_DESCRIPTOR_BUFFER_SIZE_SAMPLER;
    gpu->mem.descriptor_buffer_sampler.used = 0;
    gpu->mem.descriptor_buffer_sampler.usage = buf_info.usage;

    vkGetBufferMemoryRequirements(d, gpu->mem.descriptor_buffer_resource.buf, &mr);
    VkMemoryRequirements mr2;
    vkGetBufferMemoryRequirements(d, gpu->mem.descriptor_buffer_sampler.buf, &mr2);

    t_idx = gpu_get_memory_type(mr.memoryTypeBits & mr2.memoryTypeBits, prefer_device, prefer_host, gpu->flags, &pd_props);
    if (t_idx == Max_u32) {
        gpu->flags |= GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT;
        t_idx = gpu_get_memory_type(mr.memoryTypeBits & mr2.memoryTypeBits, prefer_device, !prefer_host, gpu->flags, &pd_props);
    }

    alloc_info.allocationSize = align(mr.size, mr2.alignment) + mr2.size;
    alloc_info.memoryTypeIndex = t_idx;
    check = vkAllocateMemory(d, &alloc_info, GAC, &gpu->mem.descriptor_mem);
    DEBUG_VK_OBJ_CREATION(vkAllocateMemory, check);

    check = vkBindBufferMemory(d, gpu->mem.descriptor_buffer_resource.buf, gpu->mem.descriptor_mem, 0);
    DEBUG_VK_OBJ_CREATION(vkBindBufferMemory, check);
    check = vkBindBufferMemory(d, gpu->mem.descriptor_buffer_sampler.buf, gpu->mem.descriptor_mem, align(mr.size, mr2.alignment));
    DEBUG_VK_OBJ_CREATION(vkBindBufferMemory, check);

    if (!flag_check(gpu->flags, GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT)) {
        check = vkMapMemory(d, gpu->mem.descriptor_mem, 0, VK_WHOLE_SIZE, 0x0, (void**)&gpu->mem.descriptor_buffer_resource.data);
        DEBUG_VK_OBJ_CREATION(vkMapMemory, check);

        gpu->mem.descriptor_buffer_sampler.data = gpu->mem.descriptor_buffer_resource.data + align(mr.size, mr2.alignment);
    } else {
        gpu->mem.descriptor_buffer_resource.data = NULL;
        gpu->mem.descriptor_buffer_sampler.data = NULL;
    }

    addr.buffer = gpu->mem.descriptor_buffer_resource.buf;
    gpu->mem.descriptor_buffer_resource.address = vkGetBufferDeviceAddress(d, &addr);

    addr.buffer = gpu->mem.descriptor_buffer_sampler.buf;
    gpu->mem.descriptor_buffer_sampler.address = vkGetBufferDeviceAddress(d, &addr);

    assert(gpu->descriptors.props.maxSamplerDescriptorBufferRange > gpu->mem.descriptor_buffer_resource.size);
    assert(gpu->descriptors.props.maxResourceDescriptorBufferRange > gpu->mem.descriptor_buffer_sampler.size);
    // @Note I am not sure how to use the address space size variables.
    // assert(gpu->descriptors.props.resourceDescriptorBufferAddressSpaceSize > gpu->mem.descriptor_buffer_resource.address + gpu->mem.descriptor_buffer_resource.size);
    // assert(gpu->descriptors.props.samplerDescriptorBufferAddressSpaceSize > gpu->mem.descriptor_buffer_sampler.address + gpu->mem.descriptor_buffer_sampler.size);
}

static void gpu_destroy_memory_resources(struct gpu *gpu)
{
    VkDevice d = gpu->device;

    for(uint i = 0; i < GPU_DEPTH_ATTACHMENT_COUNT; ++i) {
        vk_destroy_image(d, gpu->mem.depth_attachments[i], GAC);
        vk_destroy_image_view(d, gpu->mem.depth_views[i], GAC);
        vkFreeMemory(d, gpu->mem.depth_mems[i], GAC);
    }
    for(uint i = 0; i < GPU_HDR_COLOR_ATTACHMENT_COUNT; ++i) {
        vk_destroy_image(d, gpu->mem.hdr_color_attachments[i], GAC);
        vk_destroy_image_view(d, gpu->mem.hdr_color_views[i], GAC);
        vkFreeMemory(d, gpu->mem.hdr_color_mems[i], GAC);
    }

    vk_destroy_buffer(d, gpu->mem.bind_buffer.buf, GAC);
    vkFreeMemory(d, gpu->mem.bind_mem, GAC);

    vk_destroy_buffer(d, gpu->mem.transfer_buffer.buf, GAC);
    vkFreeMemory(d, gpu->mem.transfer_mem, GAC);

    vkFreeMemory(d, gpu->mem.texture_memory.mem, GAC);

    vk_destroy_buffer(d, gpu->mem.descriptor_buffer_resource.buf, GAC);
    vk_destroy_buffer(d, gpu->mem.descriptor_buffer_sampler.buf, GAC);
    vkFreeMemory(d, gpu->mem.descriptor_mem, GAC);
}

static void gpu_create_surface(struct gpu *gpu, struct window *glfw)
{
    VkResult check = glfwCreateWindowSurface(gpu->instance, glfw->window, GPU_ALLOCATION_CALLBACKS, &gpu->swapchain.surface);
    DEBUG_VK_OBJ_CREATION(glfwCreateWindowSurface, check);
}

static void gpu_destroy_surface(struct gpu *gpu)
{
    vkDestroySurfaceKHR(gpu->instance, gpu->swapchain.surface, GPU_ALLOCATION_CALLBACKS);
}

static void gpu_recreate_swapchain(struct gpu *gpu)
{
    for(uint i = 0; i < gpu->swapchain.image_count; ++i)
        vk_destroy_image_view(gpu->device, gpu->swapchain.image_views[i], GPU_ALLOCATION_CALLBACKS);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->physical_device, gpu->swapchain.surface, &surface_capabilities);

    gpu->swapchain.info.imageExtent  = surface_capabilities.currentExtent;
    gpu->swapchain.info.preTransform = surface_capabilities.currentTransform;

    //
    // This might error with some stuff in the createinfo not properly define,
    // I made the refactor while sleepy!
    //
    VkResult check = vk_create_swapchain_khr(gpu->device, &gpu->swapchain.info, GPU_ALLOCATION_CALLBACKS, &gpu->swapchain.swapchain);

    DEBUG_VK_OBJ_CREATION(vk_create_swapchain_khr, check);
    gpu->swapchain.info.oldSwapchain = gpu->swapchain.swapchain;

    // Image setup
    check = vk_get_swapchain_images_khr(gpu->device, gpu->swapchain.swapchain, &gpu->swapchain.image_count, gpu->swapchain.images);
    DEBUG_VK_OBJ_CREATION(vk_get_swapchain_images_khr, check);

    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = gpu->swapchain.info.imageFormat;

    view_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // base mip level
        1, // mip level count
        0, // base array layer
        1, // array layer count
    };

    for(uint i = 0; i < gpu->swapchain.image_count; ++i) {
        view_info.image = gpu->swapchain.images[i];
        check = vk_create_image_view(gpu->device, &view_info, GPU_ALLOCATION_CALLBACKS, &gpu->swapchain.image_views[i]);
        DEBUG_VK_OBJ_CREATION(vk_create_image_view, check);
    }

    gpu_reset_viewport_and_scissor_to_window_extent(gpu);
}

static void gpu_create_swapchain(struct gpu *gpu, struct window *glfw)
{
    gpu_create_surface(gpu, glfw);
    VkSurfaceKHR surface = gpu->swapchain.surface;
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->physical_device, surface, &surface_capabilities);

    VkSwapchainCreateInfoKHR swapchain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface      = surface;
    swapchain_info.imageExtent  = surface_capabilities.currentExtent;
    swapchain_info.preTransform = surface_capabilities.currentTransform;

    uint format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->physical_device, swapchain_info.surface, &format_count, NULL);
    VkSurfaceFormatKHR formats[format_count];
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->physical_device, swapchain_info.surface, &format_count, formats);

    swapchain_info.imageFormat     = formats[0].format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;

    uint present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->physical_device, swapchain_info.surface, &present_mode_count, NULL);
    VkPresentModeKHR present_modes[present_mode_count];
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->physical_device, swapchain_info.surface, &present_mode_count, present_modes);

    for(uint i = 0; i < present_mode_count; ++i) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            // @Todo immediate presentation
            println("Mailbox Presentation Supported, but using FIFO (@Todo)...");
    }
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    gpu->swapchain.image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;
    log_print_error_if(gpu->swapchain.image_count > GPU_SWAPCHAIN_MAX_IMAGE_COUNT, "too many swapchain images for static array gpu.swapchain.images");

    swapchain_info.minImageCount    = gpu->swapchain.image_count;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.clipped        = VK_TRUE;

    swapchain_info.queueFamilyIndexCount = 1;
    swapchain_info.pQueueFamilyIndices   = &gpu->present_queue_index;

    gpu->swapchain.info = swapchain_info;

    VkResult check = vk_create_swapchain_khr(gpu->device, &swapchain_info, GPU_ALLOCATION_CALLBACKS, &gpu->swapchain.swapchain);
    DEBUG_VK_OBJ_CREATION(vk_create_swapchain_khr, check);

    // Image setup
    uint image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;

    uint image_count_check;
    vk_get_swapchain_images_khr(gpu->device, gpu->swapchain.swapchain, &image_count_check, NULL);
    log_print_error_if(image_count_check != image_count, "Incorrect return value from _get_swapchain_images_");

    check = vk_get_swapchain_images_khr(gpu->device, gpu->swapchain.swapchain, &image_count, gpu->swapchain.images);
    DEBUG_VK_OBJ_CREATION(vk_get_swapchain_images_khr, check);

    // Create Views
    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = swapchain_info.imageFormat;
    // @Todo Properly choose any gamma corrected format. Currently I just choose the very first one.
    // Luckily this is B8G8R8A8_SRGB
    // println("Swapchain Image Format: %u", view_info.format);

    view_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // base mip level
        1, // mip level count
        0, // base array layer
        1, // array layer count
    };

    for(uint i = 0; i < gpu->swapchain.image_count; ++i) {
        view_info.image = gpu->swapchain.images[i];
        check = vk_create_image_view(gpu->device, &view_info, GPU_ALLOCATION_CALLBACKS, &gpu->swapchain.image_views[i]);
        DEBUG_VK_OBJ_CREATION(vk_create_image_view, check);
    }
}

static void gpu_destroy_swapchain(struct gpu *gpu)
{
    for(uint i = 0; i < gpu->swapchain.image_count; ++i)
        vk_destroy_image_view(gpu->device, gpu->swapchain.image_views[i], GPU_ALLOCATION_CALLBACKS);

    vk_destroy_swapchain_khr(gpu->device, gpu->swapchain.swapchain, GPU_ALLOCATION_CALLBACKS);
    vkDestroySurfaceKHR(gpu->instance, gpu->swapchain.surface, GPU_ALLOCATION_CALLBACKS);
}

#if PRINT_GPU_VIEWPORT_STATUS
#define gpu_viewport_status(...) println(__VA_ARGS__)
#else
#define gpu_viewport_status(...)
#endif

static void gpu_reset_viewport_and_scissor_to_window_extent(struct gpu *gpu)
{
    VkViewport viewport = {0};
    viewport.x          = 0;
    viewport.y          = 0;
    viewport.width      = gpu->swapchain.info.imageExtent.width;
    viewport.height     = gpu->swapchain.info.imageExtent.height;
    viewport.minDepth   = 0.0;
    viewport.maxDepth   = 1.0;

    VkRect2D rect = {
        .offset = {0, 0},
        .extent = gpu->swapchain.info.imageExtent,
    };

    gpu_viewport_status("Gpu Viewport Status: viewport and scissor xy set to %u,%u",
                        gpu->swapchain.info.imageExtent.width, gpu->swapchain.info.imageExtent.height);

    gpu->settings.viewport = viewport;
    gpu->settings.scissor  = rect;
}

// @Todo This only works for UMA and DESCRIPTOR_BUFFER_HOST_VISIBLE
struct vs_info* init_vs_info(struct gpu *gpu, vector pos, vector fwd, struct vs_info_descriptor *ret)
{
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VkResult check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC, &ret->dsl);
    DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

    size_t sz;
    vk_get_descriptor_set_layout_size_ext(gpu->device, ret->dsl, &sz);

    size_t bb_ofs = gpu_buffer_allocate(gpu, &gpu->mem.bind_buffer, sizeof(Vertex_Info));
    ret->bb_offset = bb_ofs;

    VkDescriptorAddressInfoEXT ub = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
        .address = gpu->mem.bind_buffer.address + bb_ofs,
        .range = sizeof(Vertex_Info),
    };

    VkDescriptorGetInfoEXT gi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .data.pUniformBuffer = &ub,
    };

    ret->db_offset = gpu_buffer_allocate(gpu, &gpu->mem.descriptor_buffer_resource, sz);
    vk_get_descriptor_ext(gpu->device, &gi, gpu->descriptors.props.uniformBufferDescriptorSize,
                          gpu->mem.descriptor_buffer_resource.data + ret->db_offset);

    size_t used;
    atomic_load(&gpu->mem.descriptor_buffer_resource.used, &used);
    if (used > GPU_DESCRIPTOR_BUFFER_RESOURCE_RESERVED_SIZE)
        log_print_error("initializing vs_info overflows descriptor buffer resource's reserved size");

    atomic_load(&gpu->mem.bind_buffer.used, &used);
    if (used > GPU_BIND_BUFFER_RESERVED_SIZE)
        log_print_error("initializing vs_info overflows bind buffer's reserved size");

    Vertex_Info *vs = (Vertex_Info*)(gpu->mem.bind_buffer.data + bb_ofs);

    vs->dir_light_count = 1;
    vs->dir_lights[0].position = vector4(2, 15, 15,  1);
    vs->dir_lights[0].color    = scale_vector(vector4(10.0, 10.0, 10.0, 0), 0.5);

    vs->ambient = scale_vector(vector3(1, 1, 1), 2.3);

    matrix model;
    identity_matrix(&model);

    matrix proj;
    perspective_matrix(FOV, ASPECT_RATIO, PERSPECTIVE_NEAR, PERSPECTIVE_FAR, &proj);

    memcpy(&vs->model, &model, sizeof(model));
    memcpy(&vs->proj, &proj, sizeof(proj));

    return vs;
}

#ifdef _WIN32 // Different drivers for different OSs, so I assume cache impl would be different
#define PL_CACHE_FILE_NAME "pl-caches/window.bin"
#else
#define PL_CACHE_FILE_NAME "pl-caches/ubuntu.bin"
#endif

// Pipeline Cache
static void gpu_load_pipeline_cache(struct gpu *gpu)
{
    size_t m = allocator_used(gpu->alloc_temp);
    struct file f = file_read_bin_all(PL_CACHE_FILE_NAME, gpu->alloc_temp);

    if (f.size) {
        VkPipelineCacheHeaderVersionOne *header = (VkPipelineCacheHeaderVersionOne*)f.data;

        log_print_error_if(header->headerVersion != 1, "Invalid pipeline header version");
        log_print_error_if(header->vendorID != gpu->props.vendorID, "Invalid pipeline cache vendor id");
        log_print_error_if(header->deviceID != gpu->props.deviceID, "Pipeline Cache device id does not match");

        if (memcmp(header->pipelineCacheUUID, gpu->props.pipelineCacheUUID, VK_UUID_SIZE)) {
            print("Pipeline cache seems to be invalid, gpu cache uuid != cache file uuid, recreating from scratch...");
            f.size = 0;
            f.data = NULL;
        }
    }

    VkPipelineCacheCreateInfo create_info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    create_info.initialDataSize = f.size;
    create_info.pInitialData    = f.data;

    VkResult check = vkCreatePipelineCache(gpu->device, &create_info, GPU_ALLOCATION_CALLBACKS, &gpu->pipeline_cache);
    DEBUG_VK_OBJ_CREATION(vkCreatePipelineCache, check);

    allocator_reset_linear_to(gpu->alloc_temp, m);
}

static void gpu_store_pipeline_cache(struct gpu *gpu)
{
    size_t size;
    VkResult check = vkGetPipelineCacheData(gpu->device, gpu->pipeline_cache, &size, NULL);
    DEBUG_VK_OBJ_CREATION(vkGetPipelineCacheData, check);

    void *cache_data = allocate(gpu->alloc_temp, size);
    vkGetPipelineCacheData(gpu->device, gpu->pipeline_cache, &size, cache_data);

    file_write_bin(PL_CACHE_FILE_NAME, size, cache_data);

    vkDestroyPipelineCache(gpu->device, gpu->pipeline_cache, GPU_ALLOCATION_CALLBACKS);
}

void gpu_upload_bind_buffer(
    struct gpu      *gpu,
    uint             count,
    VkBufferCopy    *regions,
    struct range    *range,
    VkCommandBuffer  transfer,
    VkCommandBuffer  graphics)
{
    if (gpu->flags & GPU_UMA_BIT)
        return;

    VkResult check;
    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        vk_cmd_copy_buffer(transfer, gpu->mem.transfer_buffer.buf, gpu->mem.bind_buffer.buf, count, regions);

        VkBufferMemoryBarrier2 barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barr.srcQueueFamilyIndex = gpu->transfer_queue_index;
        barr.dstQueueFamilyIndex = gpu->graphics_queue_index;
        barr.buffer = gpu->mem.bind_buffer.buf;
        barr.offset = range->offset;
        barr.size = range->size;

        VkDependencyInfoKHR dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barr;

        vk_cmd_pipeline_barrier2(transfer, &dep);

        check = vk_end_command_buffer(transfer);
        DEBUG_VK_OBJ_CREATION(vk_end_command_buffer, check);

        barr.srcStageMask = 0x0,
        barr.srcAccessMask = 0x0,
        barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR,
        barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR,

        vk_cmd_pipeline_barrier2(graphics, &dep);
        check = vk_end_command_buffer(graphics);
        DEBUG_VK_OBJ_CREATION(vk_end_command_buffer, check);
    } else {
        vk_cmd_copy_buffer(graphics, gpu->mem.transfer_buffer.buf, gpu->mem.bind_buffer.buf, count, regions);

        VkMemoryBarrier2 barr = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

        VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barr;

        vk_cmd_pipeline_barrier2(graphics, &dep);

        check = vk_end_command_buffer(graphics);
        DEBUG_VK_OBJ_CREATION(vk_end_command_buffer, check);
    }
}

void gpu_upload_descriptor_buffer(struct gpu *gpu, uint count, VkBufferCopy *regions, struct range *range, bool resource, VkCommandBuffer transfer, VkCommandBuffer graphics)
{
    if (!(gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT))
        return;

    VkBuffer to = resource ? gpu->mem.descriptor_buffer_resource.buf : gpu->mem.descriptor_buffer_sampler.buf;

    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        vk_cmd_copy_buffer(transfer, gpu->mem.transfer_buffer.buf, to, count, regions);

        VkBufferMemoryBarrier2 barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barr.srcQueueFamilyIndex = gpu->transfer_queue_index;
        barr.dstQueueFamilyIndex = gpu->graphics_queue_index;
        barr.buffer = gpu->mem.bind_buffer.buf;
        barr.offset = range->offset;
        barr.size = range->size;

        VkDependencyInfoKHR dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barr;

        vk_cmd_pipeline_barrier2(transfer, &dep);

        barr.srcStageMask = 0x0,
        barr.srcAccessMask = 0x0,
        barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR,
        barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR,

        vk_cmd_pipeline_barrier2(graphics, &dep);
    } else {
        vk_cmd_copy_buffer(graphics, gpu->mem.transfer_buffer.buf, to, count, regions);

        VkMemoryBarrier2 barr = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

        VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barr;

        vk_cmd_pipeline_barrier2(graphics, &dep);
    }
}

#define GPU_MAX_IMAGES_PER_UPLOAD 32

static void gpu_upload_images(
    struct gpu       *gpu,
    uint              count,
    struct gpu_texture *images,
    size_t           *offsets,
    VkCommandBuffer   transfer,
    VkCommandBuffer   graphics)
{
    assert(count < GPU_MAX_IMAGES_PER_UPLOAD && "Increase me");

    VkImageMemoryBarrier2 barrs0[GPU_MAX_IMAGES_PER_UPLOAD];
    VkImageMemoryBarrier2 barrs1[GPU_MAX_IMAGES_PER_UPLOAD];
    VkImageMemoryBarrier2 barrs2[GPU_MAX_IMAGES_PER_UPLOAD];
    uint i;
    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        for(i = 0; i < count; ++i) {
            barrs0[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs0[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs0[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs0[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrs0[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs0[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].image = images[i].vkimage;
            barrs0[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };

            barrs1[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs1[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs1[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs1[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs1[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs1[i].srcQueueFamilyIndex = gpu->transfer_queue_index;
            barrs1[i].dstQueueFamilyIndex = gpu->graphics_queue_index;
            barrs1[i].image = images[i].vkimage;
            barrs1[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };

            barrs2[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs2[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs2[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs2[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs2[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs2[i].srcQueueFamilyIndex = gpu->transfer_queue_index;
            barrs2[i].dstQueueFamilyIndex = gpu->graphics_queue_index;
            barrs2[i].image = images[i].vkimage;
            barrs2[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };
        }
    } else {
        for(i = 0; i < count; ++i) {
            barrs0[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs0[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs0[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs0[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrs0[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs0[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].image = images[i].vkimage;
            barrs0[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };
        }
    }

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = count;
    dep.pImageMemoryBarriers = barrs0;

    VkBufferImageCopy region = {};
    region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        vk_cmd_pipeline_barrier2(transfer, &dep);
        for(i = 0; i < count; ++i) {
            region.bufferOffset = offsets[i];
            region.imageExtent = (VkExtent3D){images[i].image.x, images[i].image.y, 1};
            vk_cmd_copy_buffer_to_image(transfer, gpu->mem.transfer_buffer.buf,
                    images[i].vkimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &region);
        }
        dep.pImageMemoryBarriers = barrs1;
        vk_cmd_pipeline_barrier2(transfer, &dep);

        dep.pImageMemoryBarriers = barrs2;
        vk_cmd_pipeline_barrier2(graphics, &dep);
    } else {
        vk_cmd_pipeline_barrier2(graphics, &dep);
        for(i = 0; i < count; ++i) {
            region.bufferOffset = offsets[i];
            region.imageExtent = (VkExtent3D){images[i].image.x, images[i].image.y, 1};
            vk_cmd_copy_buffer_to_image(graphics, gpu->mem.transfer_buffer.buf,
                    images[i].vkimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &region);
        }
    }
}

void gpu_upload_images_with_base_offset(
    struct gpu       *gpu,
    uint              count,
    struct gpu_texture *images,
    size_t            base_offset,
    uint             *offsets,
    VkCommandBuffer   transfer,
    VkCommandBuffer   graphics)
{
    // I am sure that these are fine on the stack, this function
    // is basically the bottom of the stack.
    assert(count < GPU_MAX_IMAGES_PER_UPLOAD && "Increase me");
    VkImageMemoryBarrier2 barrs0[GPU_MAX_IMAGES_PER_UPLOAD];
    VkImageMemoryBarrier2 barrs1[GPU_MAX_IMAGES_PER_UPLOAD];
    VkImageMemoryBarrier2 barrs2[GPU_MAX_IMAGES_PER_UPLOAD];

    uint i;
    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        for(i = 0; i < count; ++i) {
            barrs0[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs0[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs0[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs0[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrs0[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs0[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].image = images[i].vkimage;
            barrs0[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };

            barrs1[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs1[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs1[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs1[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs1[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs1[i].srcQueueFamilyIndex = gpu->transfer_queue_index;
            barrs1[i].dstQueueFamilyIndex = gpu->graphics_queue_index;
            barrs1[i].image = images[i].vkimage;
            barrs1[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };

            barrs2[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs2[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs2[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs2[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs2[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs2[i].srcQueueFamilyIndex = gpu->transfer_queue_index;
            barrs2[i].dstQueueFamilyIndex = gpu->graphics_queue_index;
            barrs2[i].image = images[i].vkimage;
            barrs2[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };
        }
    } else {
        for(i = 0; i < count; ++i) {
            barrs0[i] = (VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrs0[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
            barrs0[i].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
            barrs0[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrs0[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs0[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrs0[i].image = images[i].vkimage;
            barrs0[i].subresourceRange = (VkImageSubresourceRange) {
                VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i].image.miplevels, 0, 1
            };
        }
    }

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = count;
    dep.pImageMemoryBarriers = barrs0;

    VkBufferImageCopy region = {};
    region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

    if (gpu->flags & GPU_DISCRETE_TRANSFER_BIT) {
        vk_cmd_pipeline_barrier2(transfer, &dep);
        for(i = 0; i < count; ++i) {
            region.bufferOffset = base_offset + offsets[i];
            region.imageExtent = (VkExtent3D){images[i].image.x, images[i].image.y, 1};
            vk_cmd_copy_buffer_to_image(transfer, gpu->mem.transfer_buffer.buf,
                    images[i].vkimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &region);
        }
        dep.pImageMemoryBarriers = barrs1;
        vk_cmd_pipeline_barrier2(transfer, &dep);

        dep.pImageMemoryBarriers = barrs2;
        vk_cmd_pipeline_barrier2(graphics, &dep);
    } else {
        vk_cmd_pipeline_barrier2(graphics, &dep);
        for(i = 0; i < count; ++i) {
            region.bufferOffset = base_offset + offsets[i];
            region.imageExtent = (VkExtent3D){images[i].image.x, images[i].image.y, 1};
            vk_cmd_copy_buffer_to_image(graphics, gpu->mem.transfer_buffer.buf,
                    images[i].vkimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &region);
        }
    }
}

void gpu_blit_gltf_texture_mipmaps(gltf *model, struct gpu_texture *images, VkCommandBuffer graphics)
{
    VkImageMemoryBarrier2 barr = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barr.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barr.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
    barr.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
    barr.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
    barr.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier2 barr2 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barr2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barr2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barr2.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    barr2.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barr2.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barr2.dstAccessMask = VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    barr2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr2.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barr;

    VkImageBlit2 blit = {VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

    VkBlitImageInfo2 blit_info = {VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit_info.regionCount = 1;
    blit_info.pRegions = &blit;

    uint i,j,x,y,image;
    for(i = 0; i < model->image_count; ++i) {
        image = model->textures[i].source;
        x = images[image].image.x;
        y = images[image].image.y;
        barr.image = images[image].vkimage;

        blit_info.srcImage = images[image].vkimage;
        blit_info.dstImage = images[image].vkimage;
        blit_info.filter = (VkFilter)model->samplers[model->textures[i].sampler].mag_filter; // lame?

        dep.pImageMemoryBarriers = &barr;

        barr.subresourceRange.baseMipLevel = 0;
        barr.subresourceRange.levelCount = 1;
        vk_cmd_pipeline_barrier2(graphics, &dep);

        for(j = 1; j < images[image].image.miplevels; ++j) {
            x = x > 1 ? x / 2 : 1;
            y = y > 1 ? y / 2 : 1;

            blit.srcSubresource.mipLevel = j - 1;
            blit.dstSubresource.mipLevel = j;

            blit.srcOffsets[1] = (VkOffset3D){x,y,1};
            blit.dstOffsets[1] = (VkOffset3D){x, y, 1};

            vk_cmd_blit_image_2(graphics, &blit_info);

            barr.subresourceRange.baseMipLevel = j;
            barr.subresourceRange.levelCount = 1;
            vk_cmd_pipeline_barrier2(graphics, &dep);
        }

        dep.pImageMemoryBarriers = &barr2;
        barr2.image = images[image].vkimage;
        barr2.subresourceRange.levelCount = images[image].image.miplevels;

        vk_cmd_pipeline_barrier2(graphics, &dep); // transition whole image
    }
}

size_t gpu_buffer_allocate(struct gpu *gpu, struct gpu_buffer *buf, size_t size)
{
    size = gpu_buffer_align(gpu, size);
    size_t s = atomic_add(&buf->used, size);
    if (s + size > buf->size)
        return GPU_BUF_ALLOC_FAIL;
    return s;
}

static inline VkSampler create_sampler(struct gpu *gpu, VkSamplerCreateInfo *ci)
{
    if (atomic_add(&gpu->sampler_count, 1) >= gpu->props.limits.maxSamplerAllocationCount) {
        log_print_error("sampler creation would exceed max sampler allocation count (%u)",
                gpu->props.limits.maxSamplerAllocationCount);
        return NULL;
    }

    VkSampler ret;
    VkResult check = vk_create_sampler(gpu->device, ci, GAC, &ret);
    DEBUG_VK_OBJ_CREATION(vkCreateSampler, check);
    return ret;
}

void gpu_create_texture(struct gpu *gpu, struct image *image, struct gpu_texture *ret)
{
    ret->image = *image;

    VkImageCreateInfo img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.extent = (VkExtent3D){image->x,image->y,1};
    img_info.mipLevels = image->miplevels;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT; // gpu->settings.texture_sample_count;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult check = vk_create_image(gpu->device, &img_info, GAC, &ret->vkimage);
    DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
}

bool create_shadow_maps(struct gpu *gpu, VkCommandBuffer transfer_cmd, VkCommandBuffer graphics_cmd, struct shadow_maps *maps)
{
    // @TODO @CurrentTask
    // shadow maps need to be created as arrayed images; then views need to be created which access each layer;
    // then the depth renderpass needs to have a subpass for every array layer view in every shadow map.
    
    maps->images = allocate(gpu->alloc_heap,
                            sizeof(*maps->images) * maps->count + // NOLINT sizeof(vulkan_handle)
                            sizeof(*maps->views)  * maps->count); // NOLINT sizeof(vulkan_handle)
    maps->views = (VkImageView*)(maps->images + maps->count);

    VkMemoryRequirements *mr = sallocate(gpu->alloc_temp, *mr, maps->count);

    {
        VkImageCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = GPU_SHADOW_ATTACHMENT_FORMAT,
            .extent = (VkExtent3D){gpu->settings.shadow_maps.width,gpu->settings.shadow_maps.height,1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, // gpu->settings.texture_sample_count;
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        for(uint i=0; i < maps->count; ++i) {
            VkResult check = vk_create_image(gpu->device, &ci, GAC, &maps->images[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImage, check);
        }
    }

    uint img_sz = 0;
    for(uint i=0; i < maps->count; ++i) {
        vk_get_image_memory_requirements(gpu->device, maps->images[i], &mr[i]);
        img_sz += mr[i].size + mr[i].alignment;
    }

    size_t dsl_sz = 0;
    {
        VkDescriptorSetLayoutBinding b = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = maps->count,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        VkDescriptorSetLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .bindingCount = 1,
            .pBindings = &b,
        };

        VkResult check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC, &maps->dsl);
        DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

        vk_get_descriptor_set_layout_size_ext(gpu->device, maps->dsl, &dsl_sz);
        dsl_sz = align(dsl_sz, gpu->descriptors.props.descriptorBufferOffsetAlignment);
    }

    size_t img_ofs = gpu_allocate_image_memory(gpu, img_sz);
    if (img_ofs == GPU_BUF_ALLOC_FAIL) {
        log_print_error("failed to allocate shadow map image memory");
        return false;
    }

    maps->db_offset = gpu_buffer_allocate(gpu, &gpu->mem.descriptor_buffer_sampler, dsl_sz + gpu->descriptors.props.descriptorBufferOffsetAlignment);
    if (maps->db_offset == GPU_BUF_ALLOC_FAIL) {
        log_print_error("failed to allocate shadow map descriptor memory");
        return false;
    } else {
        maps->db_offset = align(maps->db_offset, gpu->descriptors.props.descriptorBufferOffsetAlignment);
    }

    uchar *dsl_data;
    if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {

        size_t ofs = gpu_buffer_allocate(gpu, &gpu->mem.transfer_buffer, dsl_sz);
        if (ofs == GPU_BUF_ALLOC_FAIL) {
            log_print_error("failed to allocate shadow map transfer memory");
            return false;
        }

        VkBufferCopy bc = {
            .srcOffset = ofs,
            .dstOffset = maps->db_offset,
            .size = dsl_sz,
        };

        struct range r = {.offset = maps->db_offset, .size = dsl_sz};
        gpu_upload_descriptor_buffer_sampler(gpu, 1, &bc, &r, transfer_cmd, graphics_cmd);

        dsl_data = gpu->mem.transfer_buffer.data + ofs;
    } else {
        dsl_data = gpu->mem.descriptor_buffer_sampler.data + maps->db_offset;
    }

    for(uint i=0; i < maps->count; ++i) {
        img_ofs = align(img_ofs, mr[i].alignment);
        gpu_bind_image(gpu, maps->images[i], img_ofs);
        img_ofs += mr[i].size;
    }

    {
        VkImageViewCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = GPU_SHADOW_ATTACHMENT_FORMAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        for(uint i=0; i < maps->count; ++i) {
            ci.image = maps->images[i];
            VkResult check = vk_create_image_view(gpu->device, &ci, GAC, &maps->views[i]);
            DEBUG_VK_OBJ_CREATION(vkCreateImageView, check);
        }
    }

    {
        VkSamplerCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,

            // @Todo I do not really know what a lot of this stuff really does,
            // especially with regard to shadow mapping.
            .minLod = 0,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
            .anisotropyEnable = flag_check(gpu->flags, GPU_SAMPLER_ANISOTROPY_ENABLE_BIT),
            .maxAnisotropy = gpu->defaults.sampler_anisotropy,

            .compareEnable = VK_TRUE,
            .compareOp = VK_COMPARE_OP_LESS,
        };

        maps->sampler = create_sampler(gpu, &ci);
    }

    for(uint i=0; i < maps->count; ++i) {
        VkDescriptorImageInfo ii = {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = maps->views[i],
            .sampler = maps->sampler,
        };

        VkDescriptorGetInfoEXT gi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .data.pCombinedImageSampler = &ii,
        };

        uint stride = gpu->descriptors.props.combinedImageSamplerDescriptorSize;
        assert(stride <= 512);
        uchar d[512];
        vk_get_descriptor_ext(gpu->device, &gi, stride, d);

        ds_cis_arrcpy(gpu, dsl_data, i, maps->count, d);
    }

    return true;
}

void free_shadow_maps(struct gpu *gpu, struct shadow_maps *maps)
{
    for(uint i=0; i < maps->count; ++i) {
        vk_destroy_image(gpu->device, maps->images[i], GAC);
        vk_destroy_image_view(gpu->device, maps->views[i], GAC);
    }
    vk_destroy_sampler(gpu->device, maps->sampler, GAC);
    vk_destroy_descriptor_set_layout(gpu->device, maps->dsl, GAC);
    deallocate(gpu->alloc_heap, maps->images);
}

void gpu_destroy_image(struct gpu *gpu, struct gpu_texture *image)
{
    vk_destroy_image(gpu->device, image->vkimage, GAC);
    free_image(&image->image);
}

void gpu_destroy_image_and_view(struct gpu *gpu, struct gpu_texture *image)
{
    vk_destroy_image(gpu->device, image->vkimage, GAC);
    vk_destroy_image_view(gpu->device, image->view, GAC);
    free_image(&image->image);
}

struct memreq gpu_texture_memreq(struct gpu *gpu, struct gpu_texture *image)
{
    VkMemoryRequirements mr;
    vk_get_image_memory_requirements(gpu->device, image->vkimage, &mr);
    return (struct memreq){mr.size,mr.alignment};
}

size_t gpu_allocate_image_memory(struct gpu *gpu, size_t size)
{
    size_t ofs = atomic_add(&gpu->mem.texture_memory.used, size);
    if (ofs + size > gpu->mem.texture_memory.size)
        return Max_u64;
    return ofs;
}

void gpu_bind_image(struct gpu *gpu, VkImage image, size_t ofs)
{
    VkResult check = vk_bind_image_memory(gpu->device, image, gpu->mem.texture_memory.mem, ofs);
    DEBUG_VK_OBJ_CREATION(vkBindImageMemory, check);
}

void gpu_create_texture_view(struct gpu *gpu, struct gpu_texture *image)
{
    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image->vkimage;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, image->image.miplevels, 0, 1};

    VkResult check = vk_create_image_view(gpu->device, &view_info, GAC, &image->view);
    DEBUG_VK_OBJ_CREATION(vkCreateImageView, check);
}

VkSampler gpu_create_gltf_sampler(struct gpu *gpu, gltf_sampler *info)
{
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = (VkFilter)info->mag_filter;
    ci.minFilter = (VkFilter)info->min_filter;
    ci.mipmapMode = (VkSamplerMipmapMode)info->mipmap_mode;
    ci.addressModeU = (VkSamplerAddressMode)info->wrap_u;
    ci.addressModeV = (VkSamplerAddressMode)info->wrap_v;

    // @Todo I do not really know what a lot of this stuff really does...
    ci.minLod = 0;
    ci.maxLod = VK_LOD_CLAMP_NONE;
    ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.anisotropyEnable = flag_check(gpu->flags, GPU_SAMPLER_ANISOTROPY_ENABLE_BIT);
    ci.maxAnisotropy = gpu->defaults.sampler_anisotropy;
    ci.compareEnable = 0; // This I especially do not know its function.

    return create_sampler(gpu, &ci);
}

void gpu_destroy_sampler(struct gpu *gpu, VkSampler sampler)
{
    atomic_sub(&gpu->sampler_count, 1);
    vk_destroy_sampler(gpu->device, sampler, GAC);
}

void create_color_renderpass(struct gpu *gpu, struct renderpass *rp)
{
    VkAttachmentDescription attachment_descs[] = {
        { // Depth
            .format = VK_FORMAT_D16_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
        { // HDR
            .format = GPU_HDR_COLOR_ATTACHMENT_FORMAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        { // Present
            .format = gpu->swapchain.info.imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        }
    };

    VkAttachmentReference depth_ref     = {0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference hdr_ref       = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference hdr_input_ref = {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkAttachmentReference present_ref   = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass_descriptions[] = {
        {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &hdr_ref,
            .pDepthStencilAttachment = &depth_ref,
        },
        {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 1,
            .pInputAttachments    = &hdr_input_ref,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &present_ref,
        }
    };

    VkSubpassDependency subpass_deps[] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = 1,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = 1,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
    };

    {
        VkRenderPassCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = carrlen(attachment_descs),
            .pAttachments = attachment_descs,
            .subpassCount = carrlen(subpass_descriptions),
            .pSubpasses = subpass_descriptions,
            .dependencyCount = carrlen(subpass_deps),
            .pDependencies = subpass_deps,
        };

        VkResult check = vk_create_renderpass(gpu->device, &ci, GAC, &rp->rp);
        DEBUG_VK_OBJ_CREATION(vkCreateRenderpass, check);
    }

    VkImageView views[] = {
        gpu->mem.depth_views[FRAME_I],
        gpu->mem.hdr_color_views[FRAME_I],
        gpu->swapchain.image_views[gpu->swapchain.i],
    };

    VkFramebufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp->rp,
        .attachmentCount = carrlen(views),
        .pAttachments = views,
        .width = gpu->swapchain.info.imageExtent.width,
        .height = gpu->swapchain.info.imageExtent.height,
        .layers = 1,
    };
    VkResult check = vk_create_framebuffer(gpu->device, &ci, GAC, &rp->fb);
    DEBUG_VK_OBJ_CREATION(vkCreateFrameBuffer, check);
}


// @Optimise Move to thread fn
void create_shadow_renderpass(struct gpu *gpu, uint shadow_map_count,
                              struct shadow_maps *shadow_maps, struct renderpass *rp)
{
    VkAttachmentReference *ar;
    VkSubpassDescription *ds;
    VkSubpassDependency *dp;

    VkAttachmentDescription *ad = allocate(gpu->alloc_temp,
            sizeof(*ad) * shadow_map_count +
            sizeof(*ar) * shadow_map_count +
            sizeof(*ds) * shadow_map_count +
            sizeof(*dp) * shadow_map_count);

    ar = (VkAttachmentReference*)(ad + shadow_map_count);
    ds =  (VkSubpassDescription*)(ar + shadow_map_count);
    dp =   (VkSubpassDependency*)(ds + shadow_map_count);

    for(uint i=0; i < shadow_map_count; ++i) {
        ad[i] = (VkAttachmentDescription) {
            .format         = GPU_SHADOW_ATTACHMENT_FORMAT,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    for(uint i=0; i < shadow_map_count; ++i) {
        ar[i] = (VkAttachmentReference) {
            .attachment = i,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
    }

    for(uint i=0; i < shadow_map_count; ++i) {
        ds[i] = (VkSubpassDescription) {
            .pDepthStencilAttachment = &ar[i],
        };
    }

    for(uint i=0; i < shadow_map_count; ++i) {
        dp[i] = (VkSubpassDependency) {
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = i,
            .srcStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask   = VK_ACCESS_NONE,
            .dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        };
    }

    VkRenderPassCreateInfo rpci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = shadow_map_count,
        .pAttachments    = ad,
        .subpassCount    = shadow_map_count,
        .pSubpasses      = ds,
        .dependencyCount = shadow_map_count,
        .pDependencies   = dp,
    };
    {
        VkResult check = vk_create_renderpass(gpu->device, &rpci, GAC, &rp->rp);
        DEBUG_VK_OBJ_CREATION(vkCreateRenderpass, check);
    }

    VkFramebufferCreateInfo fbci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = rp->rp,
        .attachmentCount = shadow_map_count,
        .pAttachments    = shadow_maps->views,
        .width           = gpu->settings.shadow_maps.width,
        .height          = gpu->settings.shadow_maps.height,
        .layers          = 1,
    };
    {
        VkResult check = vk_create_framebuffer(gpu->device, &fbci, GAC, &rp->fb);
        DEBUG_VK_OBJ_CREATION(vkCreateFramebuffer, check);
    }
}

void begin_color_renderpass(VkCommandBuffer cmd, struct renderpass *rp, VkRect2D area)
{
    VkClearValue clears[] = {
        (VkClearValue) {
            .depthStencil = (VkClearDepthStencilValue) {
                .depth = 1,
                .stencil = 0,
            }
        },
        (VkClearValue) {
            .color = (VkClearColorValue) {
                .float32[0] = 0.0,
                .float32[1] = 0.0,
                .float32[2] = 0.0,
                .float32[3] = 0.0,
            }
        },
        (VkClearValue) {
            .color = (VkClearColorValue) {
                .float32[0] = 0.0,
                .float32[1] = 0.0,
                .float32[2] = 0.0,
                .float32[3] = 0.0,
            }
        },
    };
    VkRenderPassBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp->rp,
        .framebuffer = rp->fb,
        .renderArea = area,
        .clearValueCount = carrlen(clears),
        .pClearValues = clears,
    };
    vk_cmd_begin_renderpass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void begin_shadow_renderpass(VkCommandBuffer cmd, struct renderpass *rp, struct gpu *gpu, uint count)
{
    assert(gpu->settings.shadow_maps.width && gpu->settings.shadow_maps.height);

    VkClearValue *clears = sallocate(gpu->alloc_temp, *clears, count);

    for(uint i=0; i < count; ++i)
        clears[i] = (VkClearValue) {
            .depthStencil = (VkClearDepthStencilValue) {
                .depth = 1,
                .stencil = 0,
            }
        };

    VkRenderPassBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp->rp,
        .framebuffer = rp->fb,
        .clearValueCount = count,
        .pClearValues = clears,
        .renderArea.extent = (VkExtent2D) {.width  = gpu->settings.shadow_maps.width,
                                           .height = gpu->settings.shadow_maps.height},
    };
    vk_cmd_begin_renderpass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

VkCommandPool create_commandpool(VkDevice device, uint queue_family_index, uint flags)
{
    VkCommandPoolCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = flags,
        .queueFamilyIndex = queue_family_index,
    };
    VkCommandPool pool;
    VkResult check = vk_create_command_pool(device, &ci, GAC, &pool);
    DEBUG_VK_OBJ_CREATION(vkCreateCommandPool, check);
    return pool;
}

VkSemaphore create_semaphore(VkDevice device, VkSemaphoreType type, uint64 initial_value)
{
    VkSemaphoreTypeCreateInfo st = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = type,
        .initialValue = initial_value,
    };
    VkSemaphoreCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &st,
    };
    VkSemaphore ret;
    VkResult check = vk_create_semaphore(device, &ci, GAC, &ret);
    DEBUG_VK_OBJ_CREATION(vkCreateSemaphore, check);
    return ret;
}

void queue_submit_info(
    uint                       cmd_count,
    VkCommandBufferSubmitInfo *cmd_infos,
    uint                       wait_count,
    VkSemaphoreSubmitInfo     *wait_infos,
    uint                       signal_count,
    VkSemaphoreSubmitInfo     *signal_infos,
    VkSubmitInfo2             *ret)
{
    *ret = (VkSubmitInfo2) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = cmd_count,
        .pCommandBufferInfos = cmd_infos,
        .waitSemaphoreInfoCount = wait_count,
        .pWaitSemaphoreInfos = wait_infos,
        .signalSemaphoreInfoCount = signal_count,
        .pSignalSemaphoreInfos = signal_infos,
    };
}

bool htp_allocate_resources(
    struct gpu        *gpu,
    struct renderpass *rp,
    uint               subpass,
    VkCommandBuffer    transfer_cmd,
    VkCommandBuffer    graphics_cmd,
    struct htp_rsc    *rsc)
{
    memset(rsc, 0, sizeof(*rsc));
    VkResult check;
    {
        VkDescriptorSetLayoutBinding b = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .bindingCount = 1,
            .pBindings = &b,
        };
        check = vk_create_descriptor_set_layout(gpu->device, &ci, GAC, &rsc->dsl);
        DEBUG_VK_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

        size_t dsl_sz;
        vk_get_descriptor_set_layout_size_ext(gpu->device, rsc->dsl, &dsl_sz);
        dsl_sz = align(dsl_sz, gpu->descriptors.props.descriptorBufferOffsetAlignment);

        float vertices[] = {
        // Triangle 1
            -1, -1,
            -1,  1,
             1,  1,

         // Triangle 2
             1,  1,
             1, -1,
            -1, -1,
        };
        size_t vert_sz = sizeof(vertices);

        size_t stage_sz = 0;
        if (!(gpu->flags & GPU_UMA_BIT))
            stage_sz = vert_sz;

        size_t db_src_ofs;
        if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
            // descriptor will be buffer copied from the offset defined by vert size
            stage_sz = gpu_buffer_align(gpu, stage_sz);
            db_src_ofs = stage_sz;
            stage_sz += dsl_sz;
        }

        size_t vert_src_ofs;
        if (stage_sz) {
            vert_src_ofs = gpu_buffer_allocate(gpu, &gpu->mem.transfer_buffer, stage_sz);
            if (vert_src_ofs == GPU_BUF_ALLOC_FAIL) {
                log_print_error("Failed to allocate staging memory for hdr resources");
                goto fail;
            }
            db_src_ofs += vert_src_ofs;
        }

        rsc->vertex_offset = gpu_buffer_allocate(gpu, &gpu->mem.bind_buffer, vert_sz);
        if (rsc->vertex_offset == GPU_BUF_ALLOC_FAIL) {
            log_print_error("Failed to allocate bind memory for hdr to present vertices");
            goto fail;
        }

        rsc->db_offset = gpu_buffer_allocate(gpu, &gpu->mem.descriptor_buffer_sampler, dsl_sz);
        if (rsc->db_offset == GPU_BUF_ALLOC_FAIL) {
            log_print_error("Failed to allocate hdr to present descriptors.");
            goto fail;
        }

        if (gpu->flags & GPU_UMA_BIT) {
            memcpy(gpu->mem.bind_buffer.data + rsc->vertex_offset, vertices, sizeof(vertices));
        } else {
            memcpy(gpu->mem.transfer_buffer.data + vert_src_ofs, vertices, sizeof(vertices));

            VkBufferCopy bc = {
                .size = sizeof(vertices),
                .srcOffset = vert_src_ofs,
                .dstOffset = rsc->vertex_offset,
            };
            struct range r = {.offset = rsc->vertex_offset, .size = bc.size};
            gpu_upload_bind_buffer(gpu, 1, &bc, &r, transfer_cmd, graphics_cmd);
        }

        VkDescriptorImageInfo ii = {
            .imageView = gpu->mem.hdr_color_views[FRAME_I],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorGetInfoEXT gi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
            .data.pInputAttachmentImage = &ii,
        };

        if (gpu->flags & GPU_DESCRIPTOR_BUFFER_NOT_HOST_VISIBLE_BIT) {
            VkBufferCopy bc = {
                .size = dsl_sz,
                .srcOffset = db_src_ofs,
                .dstOffset = rsc->db_offset,
            };
            vk_get_descriptor_ext(gpu->device, &gi,
                                  gpu->descriptors.props.inputAttachmentDescriptorSize,
                                  gpu->mem.transfer_buffer.data + bc.srcOffset);
            struct range r = {.offset = rsc->db_offset, .size = dsl_sz};
            gpu_upload_descriptor_buffer_sampler(gpu, 1, &bc, &r, transfer_cmd, graphics_cmd);
        } else {
            vk_get_descriptor_ext(gpu->device,
                                  &gi,
                                  gpu->descriptors.props.inputAttachmentDescriptorSize,
                                  gpu->mem.descriptor_buffer_sampler.data + rsc->db_offset);
        }
    }
    {
        VkPushConstantRange pc = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = 16,
        };
        VkPipelineLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &rsc->dsl,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pc,
        };
        check = vk_create_pipeline_layout(gpu->device, &ci, GAC, &rsc->pipeline_layout);
        DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);
    }
    {
        struct file f;
        VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

        f = file_read_bin_all("shaders/htp.vert.spv", gpu->alloc_temp);
        ci.codeSize = f.size,
        ci.pCode = (uint32*)f.data,
        check = vk_create_shader_module(gpu->device, &ci, GAC, &rsc->shader_modules[0]);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);

        f = file_read_bin_all("shaders/htp.frag.spv", gpu->alloc_temp);
        ci.codeSize = f.size,
        ci.pCode = (uint32*)f.data,
        check = vk_create_shader_module(gpu->device, &ci, GAC, &rsc->shader_modules[1]);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule, check);
    }
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = rsc->shader_modules[0],
            .pName = SHADER_ENTRY_POINT,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = rsc->shader_modules[1],
            .pName = SHADER_ENTRY_POINT,
        },
    };
    VkVertexInputBindingDescription bd[] = {
        {
            .binding = 0,
            .stride = 8,
        },
    };
    VkVertexInputAttributeDescription ad[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .vertexAttributeDescriptionCount = 1,
        .pVertexBindingDescriptions = bd,
        .pVertexAttributeDescriptions = ad,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
        .pViewports = &gpu->settings.viewport,
        .pScissors = &gpu->settings.scissor,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &COLOR_BLEND_NONE,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };
    {
        VkGraphicsPipelineCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .stageCount = 2,
            .pStages = shader_stages,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depth_stencil,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = rsc->pipeline_layout,
            .renderPass = rp->rp,
            .subpass = subpass,
        };
        check = vk_create_graphics_pipelines(gpu->device, gpu->pipeline_cache, 1, &ci, GAC, &rsc->pipeline);
        DEBUG_VK_OBJ_CREATION(vkCreateGraphicsPipelines, check);
    }
    return true;
fail:
    htp_free_resources(gpu, rsc);
    return false;
}

void htp_free_resources(struct gpu *gpu, struct htp_rsc *rsc)
{
    for(uint i=0; i < carrlen(rsc->shader_modules); ++i)
        if (rsc->shader_modules[i])
            vk_destroy_shader_module(gpu->device, rsc->shader_modules[i], GAC);
    if (rsc->dsl)
        vk_destroy_descriptor_set_layout(gpu->device, rsc->dsl, GAC);
    if (rsc->pipeline_layout)
        vk_destroy_pipeline_layout(gpu->device, rsc->pipeline_layout, GAC);
    if (rsc->pipeline)
        vk_destroy_pipeline(gpu->device, rsc->pipeline, GAC);
}

void htp_commands(VkCommandBuffer cmd, struct gpu *gpu, struct htp_rsc *rsc)
{
    uint db_i = DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX;
    vector exposure = get_vector(1, 0, 0, 0); // so dumb that I have to do it like this...
    vk_cmd_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rsc->pipeline);
    vk_cmd_bind_vertex_buffers(cmd, 0, 1, &gpu->mem.bind_buffer.buf, &rsc->vertex_offset);
    vk_cmd_set_descriptor_buffer_offsets_ext(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rsc->pipeline_layout,
                                             0, 1, &db_i, &rsc->db_offset);
    vk_cmd_push_constants(cmd, rsc->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, &exposure);
    vk_cmd_draw(cmd, 6, 1, 0, 0);
}

static VkShaderModule create_shader_module(VkDevice d, const char *file_name, allocator *alloc);

// @Todo Only works on UMA
void draw_box(VkCommandBuffer cmd, struct gpu *gpu, struct box *box, bool wireframe,
              VkRenderPass rp, uint subpass, struct draw_box_rsc *rsc, matrix *space, vector color)
{
    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = create_shader_module(gpu->device, "shaders/box.vert.spv", gpu->alloc_temp),
            .pName = SHADER_ENTRY_POINT,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = create_shader_module(gpu->device, "shaders/box.frag.spv", gpu->alloc_temp),
            .pName = SHADER_ENTRY_POINT,
        },
    };
    rsc->modules[0] = stages[0].module;
    rsc->modules[1] = stages[1].module;

    VkVertexInputBindingDescription bd = {
        .binding = 0,
        .stride  = 16,
    };

    VkVertexInputAttributeDescription ad = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
    };

    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .vertexAttributeDescriptionCount = 1,
        .pVertexBindingDescriptions = &bd,
        .pVertexAttributeDescriptions = &ad,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
        .pViewports = &gpu->settings.viewport,
        .pScissors = &gpu->settings.scissor,
    };

    VkPipelineRasterizationStateCreateInfo ra = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .lineWidth = 1,
    };

    VkPipelineMultisampleStateCreateInfo mu = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = !wireframe,
        .depthWriteEnable = !wireframe,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .minDepthBounds = 0,
        .maxDepthBounds = 1,
    };

    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &COLOR_BLEND_NONE,
        .blendConstants = {1,1,1,1},
    };

    VkPipelineDynamicStateCreateInfo dn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    struct {
        matrix m;
        vector c;
    } pc;

    VkPipelineLayout layout;
    {
        VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(pc),
        };

        VkPipelineLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
        };

        VkResult check = vk_create_pipeline_layout(gpu->device, &ci, GAC, &layout);
        DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);

        rsc->layout = layout;
    }

    VkPipeline pl;
    {
        VkGraphicsPipelineCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vi,
            .pInputAssemblyState = &ia,
            .pViewportState = &vp,
            .pRasterizationState = &ra,
            .pMultisampleState = &mu,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dn,
            .layout = layout,
            .renderPass = rp,
            .subpass = subpass,
        };

        VkResult check = vk_create_graphics_pipelines(gpu->device, gpu->pipeline_cache, 1, &ci, GAC, &pl);
        DEBUG_VK_OBJ_CREATION(vkCreateGraphicsPipelines, check);

        rsc->pipeline = pl;
    }

    uint indices[] = { // Idk if this is best statically initialized, but probably.
        0,1,2,  2,3,0,
        0,1,5,  0,5,4,
        0,4,3,  3,7,4,
        1,5,2,  2,6,5,
        3,7,6,  6,2,3,
        4,5,6,  6,7,4,
    };

    uint sz = sizeof(indices) + sizeof(*box);
    size_t ofs = gpu_buffer_allocate(gpu, &gpu->mem.bind_buffer, sz); // @Todo check return value

    size_t index_offset = ofs;
    size_t vert_offset = ofs + sizeof(indices);

    memcpy(gpu->mem.bind_buffer.data + index_offset, indices, sizeof(indices));
    memcpy(gpu->mem.bind_buffer.data + vert_offset,  box,     sizeof(*box));

    vk_cmd_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);

    pc.m = *space;
    pc.c =  color;

    vk_cmd_push_constants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    vk_cmd_bind_index_buffer(cmd, gpu->mem.bind_buffer.buf, index_offset, VK_INDEX_TYPE_UINT32);

    vk_cmd_bind_vertex_buffers(cmd, 0, 1, &gpu->mem.bind_buffer.buf, &vert_offset);

    vk_cmd_draw_indexed(cmd, 36, 1, 0, 0, 0);
}

void draw_box_cleanup(struct gpu *gpu, struct draw_box_rsc *rsc)
{
    for(uint i=0; i < carrlen(rsc->modules); ++i)
        vk_destroy_shader_module(gpu->device, rsc->modules[i], GAC);
    vk_destroy_pipeline(gpu->device, rsc->pipeline, GAC);
    vk_destroy_pipeline_layout(gpu->device, rsc->layout, GAC);
}

static VkShaderModule create_shader_module(VkDevice d, const char *file_name, allocator *alloc)
{
    struct file f = file_read_bin_all(file_name, alloc);

    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = f.size,
        .pCode = (const uint32*)f.data,
    };

    VkShaderModule mod;
    vk_create_shader_module(d, &ci, GAC, &mod);

    return mod;
}

void draw_floor(VkCommandBuffer cmd, struct gpu *gpu, VkRenderPass rp, uint subpass,
                VkDescriptorSetLayout dsls[2], uint db_indices[2], size_t db_offsets[2],
                struct draw_floor_rsc *rsc)
{
    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = create_shader_module(gpu->device, "shaders/floor.vert.spv", gpu->alloc_temp),
            .pName = SHADER_ENTRY_POINT,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = create_shader_module(gpu->device, "shaders/floor.frag.spv", gpu->alloc_temp),
            .pName = SHADER_ENTRY_POINT,
        },
    };
    rsc->modules[0] = stages[0].module;
    rsc->modules[1] = stages[1].module;

    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
        .pViewports = &gpu->settings.viewport,
        .pScissors = &gpu->settings.scissor,
    };

    VkPipelineRasterizationStateCreateInfo ra = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1,
    };

    VkPipelineMultisampleStateCreateInfo mu = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .minDepthBounds = 0,
        .maxDepthBounds = 1,
    };

    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &COLOR_BLEND_NONE,
        .blendConstants = {1,1,1,1},
    };

    VkPipelineDynamicStateCreateInfo dn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    VkPipelineLayout layout;
    {
        VkPipelineLayoutCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2,
            .pSetLayouts = dsls,
        };

        VkResult check = vk_create_pipeline_layout(gpu->device, &ci, GAC, &layout);
        DEBUG_VK_OBJ_CREATION(vkCreatePipelineLayout, check);

        rsc->layout = layout;
    }

    VkPipeline pl;
    {
        VkGraphicsPipelineCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vi,
            .pInputAssemblyState = &ia,
            .pViewportState = &vp,
            .pRasterizationState = &ra,
            .pMultisampleState = &mu,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dn,
            .layout = layout,
            .renderPass = rp,
            .subpass = subpass,
        };

        VkResult check = vk_create_graphics_pipelines(gpu->device, gpu->pipeline_cache, 1, &ci, GAC, &pl);
        DEBUG_VK_OBJ_CREATION(vkCreateGraphicsPipelines, check);

        rsc->pipeline = pl;
    }

    vk_cmd_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);

    vk_cmd_set_descriptor_buffer_offsets_ext(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                             0, 2, db_indices, db_offsets);
    vk_cmd_draw(cmd, 6, 1, 0, 0);
}

void draw_floor_cleanup(struct gpu *gpu, struct draw_floor_rsc *rsc)
{
    vk_destroy_pipeline(gpu->device, rsc->pipeline, GAC);
    vk_destroy_pipeline_layout(gpu->device, rsc->layout, GAC);
    for(uint i=0; i < carrlen(rsc->modules); ++i)
        vk_destroy_shader_module(gpu->device, rsc->modules[i], GAC);
}
