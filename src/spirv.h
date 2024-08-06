#ifndef SOL_SPIRV_H_INCLUDE_GUARD_
#define SOL_SPIRV_H_INCLUDE_GUARD_

#include "sol_vulkan.h"
#include "defs.h"
#include "allocator.h"
#include "test.h"

struct spirv {
    VkShaderStageFlags stage;
    uint layout_count;
    VkDescriptorSetLayoutCreateInfo *layout_infos;
    uint32 sets_used; // set bits = set index referenced by shader
    uint32 location_mask;
    VkVertexInputAttributeDescription *attribute_descriptions;
};
void parse_spirv(size_t byte_count, const uint32 *pcode, allocator *alloc, struct spirv *spirv);

#if TEST
void test_spirv(test_suite *suite);
#endif

#endif
