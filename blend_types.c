#include "sol_vulkan.h"
#include "blend_types.h"

// Credit https://vkguide.dev/docs/new_chapter_3/blending/ for the settings

VkPipelineColorBlendAttachmentState COLOR_BLEND_NONE = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};

VkPipelineColorBlendAttachmentState COLOR_BLEND_ALPHA = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA,
    .colorBlendOp        = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp        = VK_BLEND_OP_ADD,
    .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};

VkPipelineColorBlendAttachmentState COLOR_BLEND_ADDITIVE = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA,
    .colorBlendOp        = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp        = VK_BLEND_OP_ADD,
    .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};
