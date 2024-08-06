// Sampler =              UniformConstant      , OpTypeSampler                               , No Decoration
// SampledImage =         UniformConstant      , OpTypeImage (sampled = 1)                   , No Decoration
// StorageImage =         UniformConstant      , OpTypeImage (sampled = 2)                   , No Decoration
// CombinedImageSampler = UniformConstant      , OpTypeSampledImage                          , No Decoration
// UniformTexelBuffer   = UniformConstant      , OpTypeImage (Dim = Buffer, Sampled = 1)     , No Decoration
// StorageTexelBuffer   = UniformConstant      , OpTypeImage (Dim = Buffer, Sampled = 2)     , No Decoration
// UniformBuffer        = Uniform              , OpTypeStruct                                , Block + Offset
// StorageBuffer        = Uniform/StorageBuffer, OpTypeStruct                                , Block/BufferBlock + Offset
// InputAttachment      = UniformConstant      , OpTypeImage (Dim = SubpassData, Sampled = 2), InputAttachmentIndex
// InlineUniformBlock   = UniformConstant      , OpTypeStruct <-- (confusing because it is identical to uniform buffer, I think the spirv spec it has decorations)

#include "spirv.h"
#include "log.h"
#include "assert.h"

#if 0
#include "test.hpp"
#include "file.hpp"
#endif

// @Note A lot of stuff in this file is very quick and dirty.

enum {
    SPIRV_TYPE_SCALAR_BIT = 0x001,
    SPIRV_TYPE_VEC2_BIT   = 0x002,
    SPIRV_TYPE_VEC3_BIT   = 0x004,
    SPIRV_TYPE_VEC4_BIT   = 0x008,
    SPIRV_TYPE_U8_BIT     = 0x010,
    SPIRV_TYPE_U16_BIT    = 0x020,
    SPIRV_TYPE_U32_BIT    = 0x040,
    SPIRV_TYPE_S8_BIT     = 0x080,
    SPIRV_TYPE_S16_BIT    = 0x100,
    SPIRV_TYPE_S32_BIT    = 0x200,
    SPIRV_TYPE_FLOAT_BIT  = 0x400,
};

static VkFormat spirv_type_flags_to_format(int flags)
{
    switch(flags) {
    case SPIRV_TYPE_U8_BIT:
        return VK_FORMAT_R8_UINT;
    case SPIRV_TYPE_U16_BIT:
        return VK_FORMAT_R16_UINT;
    case SPIRV_TYPE_U32_BIT:
        return VK_FORMAT_R32_UINT;
    case SPIRV_TYPE_S8_BIT:
        return VK_FORMAT_R8_SINT;
    case SPIRV_TYPE_S16_BIT:
        return VK_FORMAT_R16_SINT;
    case SPIRV_TYPE_S32_BIT:
        return VK_FORMAT_R32_SINT;
    case SPIRV_TYPE_FLOAT_BIT:
        return VK_FORMAT_R32_SFLOAT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_U8_BIT:
        return VK_FORMAT_R8G8_UINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_U8_BIT:
        return VK_FORMAT_R8G8B8_UINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_U8_BIT:
        return VK_FORMAT_R8G8B8A8_UINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_U16_BIT:
        return VK_FORMAT_R16G16_UINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_U16_BIT:
        return VK_FORMAT_R16G16B16_UINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_U16_BIT:
        return VK_FORMAT_R16G16B16A16_UINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_U32_BIT:
        return VK_FORMAT_R32G32_UINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_U32_BIT:
        return VK_FORMAT_R32G32B32_UINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_U32_BIT:
        return VK_FORMAT_R32G32B32A32_UINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_S8_BIT:
        return VK_FORMAT_R8G8_SINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_S8_BIT:
        return VK_FORMAT_R8G8B8_SINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_S8_BIT:
        return VK_FORMAT_R8G8B8A8_SINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_S16_BIT:
        return VK_FORMAT_R16G16_SINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_S16_BIT:
        return VK_FORMAT_R16G16B16_SINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_S16_BIT:
        return VK_FORMAT_R16G16B16A16_SINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_S32_BIT:
        return VK_FORMAT_R32G32_SINT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_S32_BIT:
        return VK_FORMAT_R32G32B32_SINT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_S32_BIT:
        return VK_FORMAT_R32G32B32A32_SINT;
    case SPIRV_TYPE_VEC2_BIT | SPIRV_TYPE_FLOAT_BIT:
        return VK_FORMAT_R32G32_SFLOAT;
    case SPIRV_TYPE_VEC3_BIT | SPIRV_TYPE_FLOAT_BIT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case SPIRV_TYPE_VEC4_BIT | SPIRV_TYPE_FLOAT_BIT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
        log_print_error("invalid spirv type flags");
        return 0;
    }
}

struct spv_type {
    uint id;
    uint result_id;
    uint binding;
    uint descriptor_set;
    uint descriptor_count;
    VkDescriptorType descriptor_type;
    uint32 type_flags;
    bool input;
    uint location;
    //u32 input_attachment_index; <- I dont think that this is actually useful...?
};

#define SPIRV_MAX_VAR_COUNT 64 // Assumes fewer than 32 OpVariable instructions
#define SPIRV_MAX_TYPE_COUNT 128 // Assumes fewer than 64 type instructions
#define SPIRV_MAX_PUSH_CONSTANT_RANGE_COUNT 8 // Assumes fewer than 8 push constant declarations (I am pretty sure this can only ever be one lol)
#define SPIRV_DESCRIPTOR_SET_MASK uint32 // Assumes fewer than 32 descriptor sets
#define SPIRV_MAX_DESCRIPTOR_SET_COUNT 32

#define SPIRV_MAX_LOCATION 32

void parse_spirv(size_t byte_count, const uint32 *pcode, allocator *alloc, struct spirv *spirv)
{
    if (pcode[0] != 0x07230203) { // Magic number equivalency
        log_print_error("incorrect spirv magic number");
        return;
    }

    // VkPushConstantRange pc_rngs[SPIRV_MAX_PUSH_CONSTANT_RANGE_COUNT]; -Wunused
    // int type_count = 0; -Wunused
    struct spv_type types[SPIRV_MAX_TYPE_COUNT];
    // Makes it easy to tell what fields on the type have been set
    memset(types, 0xff, SPIRV_MAX_TYPE_COUNT * sizeof(types[0]));
    for(uint i = 0; i < SPIRV_MAX_TYPE_COUNT; ++i) {
        types[i].descriptor_set = Max_u32; // @Dirty
        types[i].descriptor_count = Max_u32;
        types[i].descriptor_type = Max_u32;
        types[i].type_flags = 0;
        types[i].input = 0;
    }

    uint vc = 0;
    uint vars[SPIRV_MAX_VAR_COUNT];

    SPIRV_DESCRIPTOR_SET_MASK set_mask = 0x0; // Assume fewer than 32 descriptor sets
    uint binding_cnts[SPIRV_MAX_DESCRIPTOR_SET_COUNT];
    memset(binding_cnts, 0, sizeof(uint) * SPIRV_MAX_DESCRIPTOR_SET_COUNT);

    spirv->location_mask = 0x0;

    assert(SPIRV_MAX_TYPE_COUNT <= 64 * 2);
    uint64 loc_types[2] = {0,0};

    byte_count >>= 2; // convert to word count
    size_t offset = 5; // point to first word in instr stream
    uint16 *instr_size;
    uint16 *op_code;
    const uint32 *instr;
    uint tmp;
    uint64 one = 1;
    while(offset < byte_count) {
        op_code = (uint16*)(pcode + offset);
        instr_size = op_code + 1;
        instr = pcode + offset + 1;
        switch(*op_code) {
        case 15: // OpEntryPoint
        {
            switch(instr[0]) {
            case 0:
                spirv->stage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case 1:
                spirv->stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                break;
            case 2:
                spirv->stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                break;
            case 3:
                spirv->stage = VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case 4:
                spirv->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case 5:
                spirv->stage = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            }
            break;
        }
        case 71: // OpDecorate
        {
            switch(instr[1]) { // switch on Decoration
            case 2: // Block
                types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case 3: // BufferBlock
                types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case 30: // Location
                loc_types[instr[0] >> 6] |= one << (instr[0] & 63);
                types[instr[0]].location = instr[2];
                break;
            case 33: // Binding
                types[instr[0]].binding = instr[2];
                break;
            case 34: // DescriptorSet
                types[instr[0]].descriptor_set = instr[2];
                binding_cnts[instr[2]]++;
                set_mask |= 1 << instr[2];
                assert(instr[2] < sizeof(SPIRV_DESCRIPTOR_SET_MASK) * 8 && "desc_mask local var is too small to hold this bit shift");
                break;
            case 43: // InputAttachmentIndex
                types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                //type->input_attachment_index = instr[2]; <-  I dont think that this is actually useful
                break;
            default:
                break;
            }
            break;
        }
        case 59: // OpVariable
        {
            vars[vc] = instr[1];
            vc++;
            assert(vc < SPIRV_MAX_VAR_COUNT);
            types[instr[1]].result_id = instr[0];
            switch(instr[2]) { // switch on StorageClass
            /*case 9: // PushConstant
            {
                type->descriptor_type = SPIRV_SHADER_INTERFACE_PUSH_CONSTANT;
                break;
            }*/
            case 1:
            {
                types[instr[1]].input = 1;
                break;
            }
            case 12:
            {
                types[instr[1]].descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // There can be two ways to define a storage buffer depending on the spec version...
                break;
            }
            default:
                break;
            }
            break;
        }
        case 32: // OpTypePointer
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].result_id = instr[2];
            break;
        }
        case 43: // OpConstant (for array len - why is the literal not just encoded in the array instr...)
        {
            types[instr[1]].descriptor_count = instr[2];
            break;
        }
        case 21: // Int
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].type_flags |= instr[2] ? SPIRV_TYPE_S8_BIT : SPIRV_TYPE_U8_BIT;
            types[instr[0]].type_flags <<= (instr[1] >> 3) / 2;
            assert(instr[1] <= 32);
            break;
        }
        case 22: // Float
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].type_flags |= SPIRV_TYPE_FLOAT_BIT;
            break;
        }
        case 23: // Vector
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].type_flags |= SPIRV_TYPE_VEC2_BIT;
            types[instr[0]].type_flags <<= instr[2] - 2;
            types[instr[0]].type_flags |= types[instr[1]].type_flags;
            assert(instr[2] <= 4);
            break;
        }
        case 28: // OpTypeArray
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].descriptor_count = types[instr[2]].descriptor_count; // array len
            types[instr[0]].result_id = instr[1];
            break;
        }
        case 27: // OpTypeSampledImage
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            break;
        }
        case 26: // OpTypeSampler
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLER;
            break;
        }
        case 25: // OpTypeImage
        {
            assert(instr[0] < SPIRV_MAX_TYPE_COUNT);
            switch(instr[6]) { // switch Sampled
            case 1: // read only
            {
                switch(instr[2]) { // switch Dim
                case 5: // Buffer
                    types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                    break;
                case 1: // 2D
                    types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    break;
                }
                break;
            }
            case 2: // read + write
            {
                switch(instr[2]) { // switch Dim
                case 5: // Buffer
                    types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                    break;
                case 1: // 2D
                    types[instr[0]].descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                }
                break;
            }
            default:
                break;
            } // switch Sampled
            break;
        }
        default:
            break;
        }
        offset += *instr_size;
    }

    spirv->sets_used = set_mask;
    spirv->layout_count = pop_count32(set_mask);
    spirv->location_mask = 0x0;

    assert(c_array_len(loc_types) == 2);
    if (!set_mask && !loc_types[0] && !loc_types[1])
        return;

    uint pc = pop_count64(loc_types[0]) + pop_count64(loc_types[1]);
    spirv->attribute_descriptions = sallocate(alloc, *spirv->attribute_descriptions, pc);

    uint i,j,tz;
    uint64 bit64,tmp64;
    for(i=0; i < 2; ++i) {
        pc = pop_count64(loc_types[i]);
        tmp64 = loc_types[i];
        for(j=0; j < pc; ++j) {
            tz = ctz64(tmp64);
            tmp = tz + 64 * i;
            bit64 = types[tmp].input == 0;
            loc_types[i] &= ~(bit64 << tz);
            tmp64 &= ~(one << tz);
            spirv->location_mask |= types[tmp].input << (types[tmp].location & max32_if_true(types[tmp].input));
        }
    }

    for(i=0; i < 2; ++i) {
        pc = pop_count64(loc_types[i]);
        for(j=0; j < pc; ++j) {
            tz = ctz64(loc_types[i]);
            loc_types[i] &= ~(one << tz);
            tmp = tz + 64 * i;

            tz = packed_sparse_array_index_to_bit(types[tmp].location, spirv->location_mask);
            spirv->attribute_descriptions[tz].location = types[tmp].location;
            spirv->attribute_descriptions[tz].binding = 0;
            spirv->attribute_descriptions[tz].offset = 0;
            spirv->attribute_descriptions[tz].format = spirv_type_flags_to_format(types[types[types[tmp].result_id].result_id].type_flags);
        }
    }

    if (!set_mask)
        return;

    uint max_idx = (32 - clz32(set_mask));
    // Sets do not have to be contiguous within the shader, but it is probably a good idea.
    // log_print_error_if(tz != ((sizeof(set_mask) * 8) - clz32(set_mask)), "descriptor set indices are not contiguous");

    uint binding_offsets[SPIRV_MAX_DESCRIPTOR_SET_COUNT];
    tmp = 0;
    for(i=0;i<max_idx;++i) {
        binding_offsets[i] = tmp;
        tmp += binding_cnts[i];
    }

    size_t sz0 = alloc_align(sizeof(*spirv->layout_infos) * max_idx);
    size_t sz1 = alloc_align(sizeof(*spirv->layout_infos->pBindings) * tmp);
    spirv->layout_infos = allocate(alloc, sz0 + sz1);
    memset(spirv->layout_infos, 0, sz0);

    tmp = spirv->layout_count;
    VkDescriptorSetLayoutBinding *ptr = (VkDescriptorSetLayoutBinding*)((uchar*)spirv->layout_infos + sz0); // rename to pbinding
    for(i=0;i<tmp;++i) {
        tz = ctz32(set_mask);
        set_mask &= ~(1 << tz);
        spirv->layout_infos[tz] = (VkDescriptorSetLayoutCreateInfo){VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        spirv->layout_infos[tz].flags = DESCRIPTOR_BUFFER ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT : 0;
        spirv->layout_infos[tz].bindingCount = 0;
        spirv->layout_infos[tz].pBindings = ptr + binding_offsets[i];
    }

    struct spv_type *t0,*t1;
    for(i=0;i<vc;++i) {
        t0 = &types[vars[i]];
        t1 = &types[t0->result_id];
        t1 = &types[t1->result_id]; // deref, opvar result id is a ptr

        // @Todo track descriptor set'd types with a mask
        if (t0->descriptor_set == Max_u32)
            continue;

        tmp = spirv->layout_infos[t0->descriptor_set].bindingCount;
        ptr = (VkDescriptorSetLayoutBinding*)&spirv->layout_infos[t0->descriptor_set].pBindings[tmp];
        ptr->binding = t0->binding;
        ptr->stageFlags = spirv->stage;
        ptr->pImmutableSamplers = NULL;

        if (t1->descriptor_count != Max_u32) {
            ptr->descriptorCount = t1->descriptor_count;
            t1 = &types[t1->result_id]; // deref array
        } else {
            ptr->descriptorCount = 1;
        }

        if (t0->descriptor_type != Max_u32)
            ptr->descriptorType = t0->descriptor_type;
        else
            ptr->descriptorType = t1->descriptor_type;

        spirv->layout_infos[t0->descriptor_set].bindingCount++;
    }
}

#if TEST
static void test_parser(test_suite *suite);

void test_spirv(test_suite *suite) {
    test_parser(suite);
}

void test_parser(test_suite *suite) {
    BEGIN_TEST_MODULE("Parsed_Spirv", false, false);

    struct file f = file_read_bin_all("test/spirv_test_parser.frag.spv", suite->alloc);
    const uint32 *pcode = (const uint32*)f.data;

    struct spirv spv;
    parse_spirv(f.size, pcode, suite->alloc, &spv);
    deallocate(suite->alloc, f.data);

    TEST_EQ("layout_count", spv.layout_count, 5, false);

    TEST_EQ("layouts[0].binding", spv.layout_infos[0].bindingCount, 1, false);
    TEST_EQ("layouts[0].binding", spv.layout_infos[0].pBindings[0].binding, 1, false);
    TEST_EQ("layouts[0].descriptorCount", spv.layout_infos[0].pBindings[0].descriptorCount, 2, false);
    TEST_EQ("layouts[0].descriptorType", spv.layout_infos[0].pBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);

    TEST_EQ("layouts[1].binding", spv.layout_infos[1].bindingCount, 2, false);
    TEST_EQ("layouts[1].binding", spv.layout_infos[1].pBindings[1].binding, 0, false);
    TEST_EQ("layouts[1].descriptorCount", spv.layout_infos[1].pBindings[1].descriptorCount, 1, false);
    TEST_EQ("layouts[1].descriptorType", spv.layout_infos[1].pBindings[1].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false);

    TEST_EQ("layouts[1].binding", spv.layout_infos[1].bindingCount, 2, false);
    TEST_EQ("layouts[1].binding", spv.layout_infos[1].pBindings[0].binding, 1, false);
    TEST_EQ("layouts[1].descriptorCount", spv.layout_infos[1].pBindings[0].descriptorCount, 4, false);
    TEST_EQ("layouts[1].descriptorType", spv.layout_infos[1].pBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false);

    TEST_EQ("layouts[2].binding", spv.layout_infos[2].bindingCount, 1, false);
    TEST_EQ("layouts[2].binding", spv.layout_infos[2].pBindings[0].binding, 3, false);
    TEST_EQ("layouts[2].descriptorCount", spv.layout_infos[2].pBindings[0].descriptorCount, 4, false);
    TEST_EQ("layouts[2].descriptorType", spv.layout_infos[2].pBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, false);

    TEST_EQ("layouts[3].binding", spv.layout_infos[3].bindingCount, 1, false);
    TEST_EQ("layouts[3].binding", spv.layout_infos[3].pBindings[0].binding, 1, false);
    TEST_EQ("layouts[3].descriptorCount", spv.layout_infos[3].pBindings[0].descriptorCount, 4, false);
    TEST_EQ("layouts[3].descriptorType", spv.layout_infos[3].pBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, false);

    TEST_EQ("layouts[4].binding", spv.layout_infos[4].bindingCount, 1, false);
    TEST_EQ("layouts[4].binding", spv.layout_infos[4].pBindings[0].binding, 0, false);
    TEST_EQ("layouts[4].descriptorCount", spv.layout_infos[4].pBindings[0].descriptorCount, 1, false);
    TEST_EQ("layouts[4].descriptorType", spv.layout_infos[4].pBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false);

    TEST_EQ("location count", pop_count32(spv.location_mask), 4, false);

    // uint locations[] = {0,1,2,3,4};
    uint locations[] = {0,1,7,10};
    VkFormat formats[] = {VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32_SFLOAT};
    for(uint i = 0; i < 4; ++i) {
        TEST_EQ("location", spv.attribute_descriptions[i].location, locations[i], false);
        TEST_EQ("format", spv.attribute_descriptions[i].format, formats[i], false);
        TEST_EQ("pos", flag_check(spv.location_mask, 1<<locations[i]), 1, false);
    }

    deallocate(suite->alloc, spv.layout_infos);
    deallocate(suite->alloc, spv.attribute_descriptions);
    END_TEST_MODULE();
}

#endif
