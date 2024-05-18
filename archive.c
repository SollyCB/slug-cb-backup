#if 0
// functions like this are such a waste of time to write...
u32 get_accessor_byte_stride(Gltf_Accessor_Format accessor_format) {
        switch(accessor_format) {
            case GLTF_ACCESSOR_FORMAT_SCALAR_U8:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S8:
                return 1;

            case GLTF_ACCESSOR_FORMAT_VEC2_U8:
            case GLTF_ACCESSOR_FORMAT_VEC2_S8:
                return 2;

            case GLTF_ACCESSOR_FORMAT_VEC3_U8:
            case GLTF_ACCESSOR_FORMAT_VEC3_S8:
                return 3;

            case GLTF_ACCESSOR_FORMAT_VEC4_U8:
            case GLTF_ACCESSOR_FORMAT_VEC4_S8:
                return 4;

            case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S16:
            case GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT16:
                return 2;

            case GLTF_ACCESSOR_FORMAT_VEC2_U16:
            case GLTF_ACCESSOR_FORMAT_VEC2_S16:
            case GLTF_ACCESSOR_FORMAT_VEC2_FLOAT16:
                return 4;

            case GLTF_ACCESSOR_FORMAT_VEC3_U16:
            case GLTF_ACCESSOR_FORMAT_VEC3_S16:
            case GLTF_ACCESSOR_FORMAT_VEC3_FLOAT16:
                return 6;

            case GLTF_ACCESSOR_FORMAT_VEC4_U16:
            case GLTF_ACCESSOR_FORMAT_VEC4_S16:
            case GLTF_ACCESSOR_FORMAT_VEC4_FLOAT16:
                 return 8;

            case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S32:
            case GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT32:
                return 4;

            case GLTF_ACCESSOR_FORMAT_VEC2_U32:
            case GLTF_ACCESSOR_FORMAT_VEC2_S32:
            case GLTF_ACCESSOR_FORMAT_VEC2_FLOAT32:
                return 8;

            case GLTF_ACCESSOR_FORMAT_VEC3_U32:
            case GLTF_ACCESSOR_FORMAT_VEC3_S32:
            case GLTF_ACCESSOR_FORMAT_VEC3_FLOAT32:
                return 12;

            case GLTF_ACCESSOR_FORMAT_VEC4_U32:
            case GLTF_ACCESSOR_FORMAT_VEC4_S32:
            case GLTF_ACCESSOR_FORMAT_VEC4_FLOAT32:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT2_U8:
            case GLTF_ACCESSOR_FORMAT_MAT2_S8:
                return 4;

            case GLTF_ACCESSOR_FORMAT_MAT3_U8:
            case GLTF_ACCESSOR_FORMAT_MAT3_S8:
                return 9;

            case GLTF_ACCESSOR_FORMAT_MAT4_U8:
            case GLTF_ACCESSOR_FORMAT_MAT4_S8:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT2_U16:
            case GLTF_ACCESSOR_FORMAT_MAT2_S16:
            case GLTF_ACCESSOR_FORMAT_MAT2_FLOAT16:
                return 8;

            case GLTF_ACCESSOR_FORMAT_MAT3_U16:
            case GLTF_ACCESSOR_FORMAT_MAT3_S16:
            case GLTF_ACCESSOR_FORMAT_MAT3_FLOAT16:
                return 18;

            case GLTF_ACCESSOR_FORMAT_MAT4_U16:
            case GLTF_ACCESSOR_FORMAT_MAT4_S16:
            case GLTF_ACCESSOR_FORMAT_MAT4_FLOAT16:
                return 32;

            case GLTF_ACCESSOR_FORMAT_MAT2_U32:
            case GLTF_ACCESSOR_FORMAT_MAT2_S32:
            case GLTF_ACCESSOR_FORMAT_MAT2_FLOAT32:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT3_U32:
            case GLTF_ACCESSOR_FORMAT_MAT3_S32:
            case GLTF_ACCESSOR_FORMAT_MAT3_FLOAT32:
                return 36;

            case GLTF_ACCESSOR_FORMAT_MAT4_U32:
            case GLTF_ACCESSOR_FORMAT_MAT4_S32:
            case GLTF_ACCESSOR_FORMAT_MAT4_FLOAT32:
                return 64;

            default:
                assert(false && "Invalid Accessor Format");
                return Max_u32;
        }
}

void descriptor_write_combined_image_sampler(
    gpu_descriptor_allocator *alloc,
    uint                      count,
    VkDescriptorDataEXT      *datas,
    uchar                    *mem)
{
    /* From the Vulkan Spec for vk_get_descriptor_extEXT:

            "If the VkPhysicalDeviceDescriptorBufferPropertiesEXT::combinedImageSamplerDescriptorSingleArray
            property is VK_FALSE the implementation requires an array of
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors to be written into a descriptor buffer as an
            array of image descriptors, immediately followed by an array of sampler descriptors. Applications
            must write the first VkPhysicalDeviceDescriptorBufferPropertiesEXT::sampledImageDescriptorSize
            bytes of the data returned through pDescriptor to the first array, and the remaining
            VkPhysicalDeviceDescriptorBufferPropertiesEXT::samplerDescriptorSize bytes of the data to the
            second array."

        @Note It is unclear what exactly you do if it is not an array...
    */

    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type                   =  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    size_t combined_size = alloc->info.combinedImageSamplerDescriptorSize;
    size_t image_size;
    size_t sampler_size;

    assert(align(combined_size, alloc->info.descriptorBufferOffsetAlignment) < 64);
    uchar tmp[64];
    size_t sampler_offset;
    if (!alloc->info.combinedImageSamplerDescriptorSingleArray) {
        sampler_offset = image_size * count;
        image_size     = alloc->info.sampledImageDescriptorSize;
        sampler_size   = alloc->info.samplerDescriptorSize;
    }

    VkDevice device = alloc->dvc;
    for(uint i = 0; i < count; ++i) {
        get_info.data = datas[i];

        // These branches will always be predicted.
        if (alloc->info.combinedImageSamplerDescriptorSingleArray) {
            vk_get_descriptor_ext(device, &get_info, combined_size, mem + (i * combined_size));
        } else {
            // This is awkward, but I guess it is the only way...
            vk_get_descriptor_ext(device, &get_info, combined_size, tmp);
            memcpy(mem + (i * image_size), tmp, image_size);
            memcpy(mem + sampler_offset + (i * sampler_size), tmp + image_size, sampler_size);
        }
    }
}

void descriptor_write_uniform_buffer(
    gpu_descriptor_allocator *alloc,
    uint                      count,
    VkDescriptorDataEXT      *datas,
    uchar                    *mem)
{
    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    VkDevice dvc = alloc->dvc;
    size_t ub_sz = alloc->info.uniformBufferDescriptorSize;
    for(uint i = 0; i < count; ++i) {
        get_info.data = datas[i];
        vk_get_descriptor_ext(dvc, &get_info, ub_sz, mem);
    }
}

void descriptor_write_input_attachment(gpu_descriptor_allocator *alloc, uint count, VkDescriptorDataEXT *datas, uchar *mem) {
    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type                   =  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

    VkDevice dvc = alloc->dvc;
    size_t ia_size = alloc->info.inputAttachmentDescriptorSize;
    for(uint i = 0; i < count; ++i) {
        get_info.data = datas[i];
        vk_get_descriptor_ext(dvc, &get_info, ia_size, mem + (i * ia_size));
    }
}

#endif
