#ifndef SOL_BLEND_TYPES_H_
#define SOL_BLEND_TYPES_H_

typedef enum {
    COLOR_BLEND_TYPE_NONE,
    COLOR_BLEND_TYPE_ALPHA,
    COLOR_BLEND_TYPE_ADDITIVE,
} color_blend_type;

extern VkPipelineColorBlendAttachmentState COLOR_BLEND_NONE;
extern VkPipelineColorBlendAttachmentState COLOR_BLEND_ALPHA;
extern VkPipelineColorBlendAttachmentState COLOR_BLEND_ADDITIVE;

#endif // include guard

