//
// @Note @Todo
// I do not like the fact that there is a lot of stuff in this file which is hard coded
// in strings that really I would prefer is generated in some way. Then I can deal with
// compiler errors when names and numbers dont match etc., as opposed to having to go
// through each occurence in strings. But generating code everytime is also slow, and
// would take longer to get a working version. Whenever this becomes a problem (which
// it hopefully never will, since this code seems to do everything I would ever need
// it to) I will make that update, probably.
//
#include "sol_vulkan.h"
#include "gltf.h"
#include "file.h"
#include "timer.h"
#include "gpu.h"
#include "shader.h"
#include "array.h"

const char *SHADER_ENTRY_POINT = "main";

// This is a solution to a problem e.g. in gpu_pl_shader_stage(), where modules
// in shader sets want to be accessed like an array, but keeping module names
// also has its benefits. I really do not enjoy just casting structs to arrays
// and then iterating members that way, as if the struct has been reordered,
// BAM! ur fucked. This way seems to fix this issue of robustness to change to
// some degree. But it still is not particularly beatiful.
const uint SHADER_SET_MODULE_OFFSETS[GRAPHICS_PIPELINE_SHADER_STAGE_LIMIT_VULKAN] = {
    offsetof(struct shader_set, vert),
    Max_u32, // tesselation control
    Max_u32, // tesselation evaluation
    Max_u32, // geometry
    offsetof(struct shader_set, frag),
};

#define SHADER_PRINT 0
#if SHADER_PRINT
#define print_auto_shader(...) println(__VA_ARGS__)
#else
#define print_auto_shader(...)
#endif

#define SHDR_BM 0
#if SHDR_BM
#define SHDR_BM_HASH_START struct timer m__hashtimer = start_timer()
#define SHDR_BM_HASH_END \
struct timespec m__hashtimer_end = end_timer(&m__hashtimer); \
print("Auto Shader Hash Benchmark:\n    "); \
print_time(m__hashtimer_end.tv_sec,m__hashtimer_end.tv_nsec)

#else
#define SHDR_BM_HASH_START
#define SHDR_BM_HASH_END
#endif

// @Todo A few sections of this file can be sped up + cleaned up: When I
// started I had an arbitrary limit of 99 on all attribute.n values. This is
// dumb, as eight is plenty. I acted on this later, so earlier sections of the
// file (particularly the material declaration stuff in the fragment shader)
// have more memcpys than necessary.

enum {
    SKIN_BIT = 0x01,
};

enum {
    INST_TRS_BIT = 0x01,
    INST_TBN_BIT = 0x02,
};

enum {
    BYTE  = ctz(GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT),
    SHORT = ctz(GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT),
    FLOAT = ctz(GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT),
};

enum {
    POS   = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
    NORM  = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
    TANG  = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
    TEXC  = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD,
    COLO  = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR,
    JNTS  = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS,
    WGHTS = GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS,
};

#define MAX_ATTRS 16
#define NUM_BASE_ATTRS 7

struct attr_info {
    uint8 i; // index
    uint8 n; // count
};

struct attr_map {
    struct attr_info attrs[NUM_BASE_ATTRS];
};

static inline uint attr_map_i(struct attr_map *map, struct attr_map match)
{
    assert((sizeof(*map) == 2) && ((MAX_ATTRS * 2) % 16 == 0));
    
    __m128i a;
    __m128i b = _mm_set1_epi16(*(int16*)&match);
    
    uint ret = -1;
    
    uint16 mask;
    uint cnt = MAX_ATTRS * sizeof(*map);
    uint i;
    for(i=0;i<cnt;i+=16) {
        a = _mm_load_si128((__m128i*)((char*)map + i));
        a = _mm_cmpeq_epi16(a,b);
        mask = _mm_movemask_epi8(a);
        ret &= max_if(!pop_count16(mask));
        ret |= ((ctz16(mask) >> 1) + (i >> 3)) & max_if(pop_count16(mask));
    }
    assert((int)ret != -1 && "morph attribute not in base attributes");
    return ret;
}

struct morph_map {
    uint8 i[NUM_BASE_ATTRS]; // indices
    uint m[MAX_ATTRS]; // masks
};

// @Note Calculating the morph_map i values might be incorrect, it is relatively untested.
// Come back here if behaviour is ever weird for attr.n > 0.
static uint get_attr_info(gltf_mesh_primitive *prim,
                          struct attr_map *am, struct morph_map *mm)
{
    assert(prim->attribute_count <= MAX_ATTRS);
    
    memset(mm,0,sizeof(*mm));
    memset(am,0,sizeof(*am));
    
    uint uniq = 0;
    uint i,j;
    for(i=0;i<prim->attribute_count;++i) {
        mm->i[prim->attributes[i].type] = i - am->attrs[prim->attributes[i].type].n;
        am->attrs[prim->attributes[i].type].n += 1;
        am->attrs[prim->attributes[i].type].i += i & zero_if(uniq & (1<<prim->attributes[i].type));
        uniq |= 1 << prim->attributes[i].type;
    }
    
    for(i=0;i<prim->target_count; ++i)
        for(j=0;j<prim->morph_targets[i].attribute_count;++j) {
            mm->m[ mm->i[prim->morph_targets[i].attributes[j].type] + prim->morph_targets[i].attributes[j].n ] |= 1<<i;
    }
    
    return popcnt(uniq);
}

struct shader_dir new_shader_dir(allocator *alloc)
{
    struct shader_dir dir;
    dir.alloc = alloc;
    return dir;
}

const char LAYOUT_0[] = "layout(location = ";

const char U8_TO_VEC2[] = "\
vec2 u8_to_vec2(uint i) {\n\
    return vec2((i & 0xff), (i & 0xff00) >> 8) / 255;\n\
}\n\
";

const char U16_TO_VEC2[] = "\
vec2 u16_to_vec2(uint i) {\n\
    return vec2((i & 0xffff), (i & 0xffff0000) >> 16) / 65535;\n\
}\n\
";

const char S8_TO_VEC2[] = "\
vec2 s8_to_vec2(uint i) {\n\
    vec2 v = vec2((i & 0xff), (i & 0xff00) >> 8) / 127;\n\
    return vec2(max(v.x,-1), max(v.y,-1));\n\
}\n\
";

const char S16_TO_VEC2[] = "\
vec2 s16_to_vec2(uint i) {\n\
    vec2 v = vec2((i & 0xffff), (i & 0xffff0000) >> 16) / 32767;\n\
    return vec2(max(v.x,-1), max(v.y,-1));\n\
}\n\
";

const char U8_TO_VEC3[] = "\
vec3 u8_to_vec3(uint i) {\n\
    return vec3((i & 0xff), (i & 0xff00) >> 8, (i & 0xff0000) >> 16) / 255;\n\
}\n\
";

const char U16_TO_VEC3[] = "\
vec3 u16_to_vec3(uvec2 i) {\n\
    return vec3((i.x & 0xffff), (i.x & 0xffff0000) >> 16, (i.y & 0xffff)) / 65535;\n\
}\n\
";

const char S8_TO_VEC3[] = "\
vec3 s8_to_vec3(uint i) {\n\
    vec3 v = vec3((i & 0xff), (i & 0xff00) >> 8, (i & 0xff0000) >> 16) / 127;\n\
    return vec3(max(v.x,-1),max(v.y,-1),max(v.z,-1));\n\
}\n\
";

const char S16_TO_VEC3[] = "\
vec3 s16_to_vec3(uvec2 i) {\n\
    vec3 v = vec3((i.x & 0xffff), (i.x & 0xffff0000) >> 16, (i.y & 0xffff)) / 32767;\n\
    return vec3(max(v.x,-1),max(v.y,-1),max(v.z,-1));\n\
}\n\
";

const char U8_TO_VEC4[] = "\
vec4 u8_to_vec4(uint i) {\n\
    return vec4((i & 0xff), (i & 0xff00) >> 8, (i & 0xff0000) >> 16, (i & 0xff000000) >> 24) / 255;\n\
}\n\
";

const char U16_TO_VEC4[] = "\
vec4 u16_to_vec4(uvec2 i) {\n\
    return vec4((i.x & 0xffff), (i.x & 0xffff0000) >> 16, (i.y & 0xffff), (i.y & 0xffff0000) >> 16) / 65535;\n\
}\n\
";

const char S8_TO_VEC4[] = "\
vec4 s8_to_vec4(uint i) {\n\
    vec4 v = vec4((i & 0xff), (i & 0xff00) >> 8, (i & 0xff0000) >> 16, (i & 0xff000000) >> 24) / 127;\n\
    return vec4(max(v.x,-1),max(v.y,-1),max(v.z,-1),max(v.w,-1));\n\
}\n\
";

const char S16_TO_VEC4[] = "\
vec4 s16_to_vec4(uvec2 i) {\n\
    vec4 v = vec4((i.x & 0xffff), (i.x & 0xffff0000) >> 16, (i.y & 0xffff), (i.y & 0xffff0000) >> 16) / 32767;\n\
    return vec4(max(v.x,-1),max(v.y,-1),max(v.z,-1),max(v.w,-1));\n\
}\n\
";

const char U8_TO_UVEC4[] = "\
uvec4 u8_to_uvec4(uint i) {\n\
    return uvec4((i & 0xff), (i & 0xff00) >> 8, (i & 0xff0000) >> 16, (i & 0xff000000) >> 24);\n\
}\n\
";

const char U16_TO_UVEC4[] = "\
uvec4 u16_to_uvec4(uvec2 i) {\n\
    return uvec4((i.x & 0xffff), (i.x & 0xffff0000) >> 16, (i.y & 0xffff), (i.y & 0xffff0000) >> 16);\n\
}\n\
";

enum {
    U8_TO_VEC2_BIT   = 0x0001,
    U16_TO_VEC2_BIT  = 0x0002,
    S8_TO_VEC2_BIT   = 0x0004,
    S16_TO_VEC2_BIT  = 0x0008,
    U8_TO_VEC3_BIT   = 0x0010,
    U16_TO_VEC3_BIT  = 0x0020,
    S8_TO_VEC3_BIT   = 0x0040,
    S16_TO_VEC3_BIT  = 0x0080,
    U8_TO_VEC4_BIT   = 0x0100,
    U16_TO_VEC4_BIT  = 0x0200,
    S8_TO_VEC4_BIT   = 0x0400,
    S16_TO_VEC4_BIT  = 0x0800,
    U8_TO_UVEC4_BIT  = 0x1000,
    U16_TO_UVEC4_BIT = 0x2000,
};

struct cnvrt {
    uint len;
    const char *str;
};

struct cnvrt CNVRT_FNS[] = {
    (struct cnvrt){cstr_as_array_len(U8_TO_VEC2),U8_TO_VEC2},
    (struct cnvrt){cstr_as_array_len(U16_TO_VEC2),U16_TO_VEC2},
    (struct cnvrt){cstr_as_array_len(S8_TO_VEC2),S8_TO_VEC2},
    (struct cnvrt){cstr_as_array_len(S16_TO_VEC2),S16_TO_VEC2},
    (struct cnvrt){cstr_as_array_len(U8_TO_VEC3),U8_TO_VEC3},
    (struct cnvrt){cstr_as_array_len(U16_TO_VEC3),U16_TO_VEC3},
    (struct cnvrt){cstr_as_array_len(S8_TO_VEC3),S8_TO_VEC3},
    (struct cnvrt){cstr_as_array_len(S16_TO_VEC3),S16_TO_VEC3},
    (struct cnvrt){cstr_as_array_len(U8_TO_VEC4),U8_TO_VEC4},
    (struct cnvrt){cstr_as_array_len(U16_TO_VEC4),U16_TO_VEC4},
    (struct cnvrt){cstr_as_array_len(S8_TO_VEC4),S8_TO_VEC4},
    (struct cnvrt){cstr_as_array_len(S16_TO_VEC4),S16_TO_VEC4},
    (struct cnvrt){cstr_as_array_len(U8_TO_UVEC4),U8_TO_UVEC4},
    (struct cnvrt){cstr_as_array_len(U16_TO_UVEC4),U16_TO_UVEC4},
};

static inline void complete_layout_short(
                                         string_builder *sb,
                                         uint32          len_location,
                                         const char     *location,
                                         uint32          len_name,
                                         const char     *name,
                                         uint32          mti)
{
    sb_add(sb, cstr_as_array_len(LAYOUT_0), LAYOUT_0);
    sb_add(sb, len_location, location);
    sb_add(sb, len_name, name);
    
    char morph[] = "_morph_00";
    morph[7] = (mti / 10) + '0';
    morph[8] = (mti % 10) + '0';
    assert((mti < 100 || mti == -1) && "2 digits");
    
    sb_add(sb, 9 & max_if(mti != -1), morph);
    
    sb_addc(sb, ';');
    sb_addnl(sb);
}

struct generate_layout_info {
    string_builder *sb_vert;
    string_builder *sb_frag;
    gltf *model;
    gltf_mesh_primitive_attribute *attr;
    uint32 len_location;
    const char *location;
    int mti;
    uint fn_deps;
};

static void generate_position_layout(struct generate_layout_info *info) {
    const char in_name[] = ") in vec3 in_position";
    
    complete_layout_short(info->sb_vert, info->len_location, info->location,
                          cstr_as_array_len(in_name), in_name, info->mti);
}

static void generate_normal_layout(struct generate_layout_info *info) {
    const char in_name[] = ") in vec3 in_normal";
    const char out_name[] = ") out vec3 out_normal";
    
    complete_layout_short(info->sb_vert, info->len_location, info->location,
                          cstr_as_array_len(in_name), in_name, info->mti);
    complete_layout_short(info->sb_vert, info->len_location, info->location,
                          cstr_as_array_len(out_name), out_name, info->mti);
    complete_layout_short(info->sb_frag, info->len_location, info->location,
                          cstr_as_array_len(in_name), in_name, -1);
}

static void generate_tangent_layout(struct generate_layout_info *info) {
    const char in_name[] = ") in vec3 in_tangent";
    
    complete_layout_short(info->sb_vert, info->len_location, info->location,
                          cstr_as_array_len(in_name), in_name, info->mti);
}

struct complete_layout_long_info {
    uint32 len_location;
    uint32 len_type;
    uint32 len_name;
    uint32 in_out_len;
    string_builder *sb;
    const char *location;
    const char *type;
    const char *name;
    const char *in_out;
    char n;
    uint32 mti;
};

static void complete_layout_long(struct complete_layout_long_info *info) {
    sb_add(info->sb, cstr_as_array_len(LAYOUT_0), LAYOUT_0);
    sb_add(info->sb, info->len_location, info->location);
    sb_add(info->sb, info->in_out_len, info->in_out);
    sb_add(info->sb, info->len_type, info->type);
    sb_add(info->sb, info->len_name, info->name);
    
    sb_addc(info->sb, info->n);
    
    char morph[] = "_morph_00";
    morph[7] = (info->mti / 10) + '0';
    morph[8] = (info->mti % 10) + '0';
    assert((info->mti < 100 || info->mti == -1) && "2 digits");
    
    sb_add(info->sb, 9 & max_if(info->mti != -1), morph);
    
    sb_addc(info->sb, ';');
    sb_addnl(info->sb);
}

const char COMPLETE_LAYOUT_IN[] = ") in ";
const char COMPLETE_LAYOUT_OUT[] = ") out ";

static inline void complete_layout_in_long(struct complete_layout_long_info *info)
{
    info->in_out_len = 5;
    info->in_out = COMPLETE_LAYOUT_IN;
    complete_layout_long(info);
}

static inline void complete_layout_out_long(struct complete_layout_long_info *info)
{
    info->in_out_len = 6;
    info->in_out = COMPLETE_LAYOUT_OUT;
    complete_layout_long(info);
}

static void generate_texcoord_layout(struct generate_layout_info *info)
{
    const char types_0[] = "vec2";
    const char types_1[] = "uint";
    const char *type;
    uint len = 4;
    
    switch(info->model->accessors[info->attr->accessor].vkformat) {
        case VK_FORMAT_R32G32_SFLOAT:
        type = types_0;
        break;
        case VK_FORMAT_R8G8_UNORM:
        type = types_1;
        sb_add(info->sb_vert,cstr_as_array_len(U8_TO_VEC2),U8_TO_VEC2);
        break;
        case VK_FORMAT_R16G16_UNORM:
        type = types_1;
        sb_add(info->sb_vert,cstr_as_array_len(U16_TO_VEC2),U16_TO_VEC2);
        break;
        case VK_FORMAT_R8G8_SNORM:
        type = types_1;
        sb_add(info->sb_vert,cstr_as_array_len(S8_TO_VEC2),S8_TO_VEC2);
        break;
        case VK_FORMAT_R16G16_SNORM:
        type = types_1;
        sb_add(info->sb_vert,cstr_as_array_len(S16_TO_VEC2),S16_TO_VEC2);
        break;
        default:
        log_print_error("invalid vkformat for texcoord");
        return;
    }
    
    const char in_name[] = " in_texcoord_";
    const char out_name[] = " out_texcoord_";
    
    struct complete_layout_long_info cli;
    cli.len_location = info->len_location;
    cli.location = info->location;
    cli.len_type = len;
    cli.type = type;
    cli.n = (char)(info->attr->n + '0');
    cli.mti = info->mti;
    
    cli.sb = info->sb_vert;
    cli.name = in_name,
    cli.len_name = cstr_as_array_len(in_name);
    complete_layout_in_long(&cli);
    
    cli.sb = info->sb_frag;
    complete_layout_in_long(&cli);
    
    cli.sb = info->sb_vert;
    cli.name = out_name;
    cli.len_name = cstr_as_array_len(out_name);
    complete_layout_out_long(&cli);
}

static void generate_color_layout(struct generate_layout_info *info) {
    const char types_0[] = "vec3";
    const char types_1[] = "vec4";
    const char types_2[] = "uvec2";
    const char types_3[] = "uint";
    
    const char *type;
    uint len;
    
    switch(info->model->accessors[info->attr->accessor].vkformat) {
        case VK_FORMAT_R32G32B32_SFLOAT:
        type = types_0;
        len = 4;
        break;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        type = types_1;
        len = 4;
        break;
        case VK_FORMAT_R16G16B16_UNORM:
        type = types_2;
        len = 5;
        info->fn_deps |= U16_TO_VEC3_BIT;
        break;
        case VK_FORMAT_R16G16B16A16_UNORM:
        type = types_2;
        len = 5;
        info->fn_deps |= U16_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R8G8B8_UNORM:
        type = types_3;
        len = 4;
        info->fn_deps |= U8_TO_VEC3_BIT;
        break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        type = types_3;
        len = 4;
        info->fn_deps |= U8_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R16G16B16_SNORM:
        type = types_2;
        len = 5;
        info->fn_deps |= S16_TO_VEC3_BIT;
        break;
        case VK_FORMAT_R16G16B16A16_SNORM:
        type = types_2;
        len = 5;
        info->fn_deps |= S16_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R8G8B8_SNORM:
        type = types_3;
        len = 4;
        info->fn_deps |= S8_TO_VEC3_BIT;
        break;
        case VK_FORMAT_R8G8B8A8_SNORM:
        type = types_3;
        len = 4;
        info->fn_deps |= S8_TO_VEC4_BIT;
        break;
        default:
        log_print_error("invalid vkformat for color");
        return;
    }
    
    const char in_name[] = " in_color_";
    const char out_name[] = " out_color_";
    
    struct complete_layout_long_info cli;
    cli.len_location = info->len_location;
    cli.location = info->location;
    cli.len_type = len;
    cli.type = type;
    cli.n = (char)(info->attr->n + '0');
    cli.mti = info->mti;
    
    cli.sb = info->sb_vert;
    cli.name = in_name,
    cli.len_name = cstr_as_array_len(in_name);
    complete_layout_in_long(&cli);
    
    cli.sb = info->sb_frag;
    complete_layout_in_long(&cli);
    
    cli.sb = info->sb_vert;
    cli.name = out_name;
    cli.len_name = cstr_as_array_len(out_name);
    complete_layout_out_long(&cli);
}

static void generate_joints_layout(struct generate_layout_info *info) {
    const char types_0[] = "uvec2";
    const char types_1[] = "uint";
    const char *type;
    uint len;
    
    switch(info->model->accessors[info->attr->accessor].vkformat) {
        case VK_FORMAT_R16G16B16A16_UINT:
        type = types_0;
        len = 5;
        info->fn_deps |= U16_TO_UVEC4_BIT;
        break;
        case VK_FORMAT_R8G8B8A8_UINT:
        type = types_1;
        len = 4;
        info->fn_deps |= U8_TO_UVEC4_BIT;
        break;
        default:
        log_print_error("invalid vkformat for joints");
        return;
    }
    
    const char name[] = " in_joints_";
    
    struct complete_layout_long_info cli;
    cli.len_location = info->len_location;
    cli.location = info->location;
    cli.len_type = len;
    cli.type = type;
    cli.sb = info->sb_vert;
    cli.name = name;
    cli.len_name = cstr_as_array_len(name);
    cli.n = (char)(info->attr->n + '0');
    cli.mti = info->mti;
    
    complete_layout_in_long(&cli);
}

static void generate_weights_layout(struct generate_layout_info *info)
{
    const char types_0[] = {"vec4"};
    const char types_1[] = {"uvec2"};
    const char types_2[] = {"uint"};
    const char *type;
    uint len;
    
    switch(info->model->accessors[info->attr->accessor].vkformat) {
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        type = types_0;
        len = 4;
        break;
        case VK_FORMAT_R16G16B16A16_UNORM:
        type = types_1;
        len = 5;
        info->fn_deps |= U16_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        type = types_2;
        len = 4;
        info->fn_deps |= U8_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R16G16B16A16_SNORM:
        type = types_1;
        len = 5;
        info->fn_deps |= S16_TO_VEC4_BIT;
        break;
        case VK_FORMAT_R8G8B8A8_SNORM:
        type = types_2;
        len = 4;
        info->fn_deps |= S8_TO_VEC4_BIT;
        break;
        default:
        log_print_error("invalid vkformat for joints");
        return;
    }
    
    const char name[] = " in_weights_";
    
    struct complete_layout_long_info cli;
    cli.len_location = info->len_location;
    cli.location = info->location;
    cli.len_type = len;
    cli.type = type;
    cli.sb = info->sb_vert;
    cli.name = name;
    cli.len_name = cstr_as_array_len(name);
    cli.n = (char)(info->attr->n + '0');
    cli.mti = info->mti;
    
    complete_layout_in_long(&cli);
}

typedef void (*generate_layout)(struct generate_layout_info *);

static generate_layout LAYOUT_GENERATOR_FUNCTIONS[] = {
    generate_position_layout,
    generate_normal_layout,
    generate_tangent_layout,
    generate_texcoord_layout,
    generate_color_layout,
    generate_joints_layout,
    generate_weights_layout,
};

const char CLOSE_ARRAY[] = "];\n";

const char IN_DIR_LIGHT_STRUCT[] = "\
struct In_Directional_Light {\n\
    vec4 position;\n\
    vec4 direction;\n\
    vec4 color;\n\
    mat4 space;\n\
    float half_angle;\n\
};\n\
";

const char IN_POINT_LIGHT_STRUCT[] = "\
struct In_Point_Light {\n\
    vec4 position;\n\
    vec4 color;\n\
    mat4 space;\n\
    float linear;\n\
    float quadratic;\n\
};\n\
";

const char VS_INFO_DECL[] = "\
layout(set = 0, binding = 0) uniform Vertex_Info {\n\
    vec4 ambient;\n\
    mat4 view;\n\
    mat4 proj;\n\
";

static void gen_vsinfo(string_builder *sb, uint dl_cnt, uint pl_cnt)
{
    const char dir_light_count[] = "    uint dir_light_count;\n";
    const char dir_lights[] = "    In_Directional_Light dir_lights[";
    const char point_light_count[] = "    uint point_light_count;\n";
    const char point_lights[] = "    In_Point_Light point_lights[";
    const char complete_decl[] = "} vs_info;\n";
    
    char num_buf[8];
    uint num_len;
    
    sb_add(sb, cstr_as_array_len(VS_INFO_DECL), VS_INFO_DECL);

    sb_add_if(sb,cstr_as_array_len(dir_light_count),dir_light_count,dl_cnt);
    sb_add_if(sb,cstr_as_array_len(point_light_count),point_light_count,pl_cnt);
    
    num_len = uint_to_ascii(dl_cnt, num_buf);
    sb_add_if(sb,cstr_as_array_len(dir_lights),dir_lights,dl_cnt);
    sb_add_if(sb,num_len,num_buf,dl_cnt);
    sb_add_if(sb,cstr_as_array_len(CLOSE_ARRAY), CLOSE_ARRAY,dl_cnt);
    
    num_len = uint_to_ascii(pl_cnt, num_buf);
    sb_add_if(sb,cstr_as_array_len(point_lights),point_lights,pl_cnt);
    sb_add_if(sb,num_len,num_buf,pl_cnt);
    sb_add_if(sb,cstr_as_array_len(CLOSE_ARRAY), CLOSE_ARRAY,pl_cnt);
    
    sb_add(sb, cstr_as_array_len(complete_decl), complete_decl);
    sb_addnl(sb);
}

const char TRANSFORMS_START[] = "layout(set = 2, binding = 0) uniform Transforms_Ubo {\n";
const char TRANSFORMS_END[] = "} transforms_ubo[";

static void gen_transforms_ubo(string_builder *sb, gltf *model, uint mesh)
{
    const char n_trs[] = "    mat4 node_trs;\n";
    const char n_tbn[] = "    mat4 node_tbn;\n";
    const char j_trs[] = "    mat4 joints_trs[";
    const char j_tbn[] = "    mat4 joints_tbn[";
    const char w[] = "    float morph_weights[";
    
    uint nl;
    char nb[8];
    
    uint jc = model->meshes[mesh].joint_count;
    uint wc = model->meshes[mesh].weight_count;
    
    sb_add(sb,cstr_as_array_len(TRANSFORMS_START),TRANSFORMS_START);
    
    nl = uint_to_ascii(jc,nb);
    
    sb_add_if(sb,cstr_as_array_len(j_trs),j_trs,jc);
    sb_add_if(sb,nl,nb,jc);
    sb_add_if(sb,3,"];\n",jc);
    
    sb_add_if(sb,cstr_as_array_len(j_tbn),j_tbn,jc);
    sb_add_if(sb,nl,nb,jc);
    sb_add_if(sb,3,"];\n",jc);
    
    sb_add_if(sb,cstr_as_array_len(n_trs),n_trs,!jc);
    sb_add_if(sb,cstr_as_array_len(n_tbn),n_tbn,!jc);
    
    nl = uint_to_ascii(wc,nb);
    
    sb_add_if(sb,cstr_as_array_len(w),w,wc);
    sb_add_if(sb,nl,nb,wc);
    sb_add_if(sb,3,"];\n",wc);
    
    sb_add(sb,cstr_as_array_len(TRANSFORMS_END),TRANSFORMS_END);
    
    nl = uint_to_ascii(model->meshes[mesh].max_instance_count,nb);
    
    sb_add(sb,nl,nb);
    sb_add(sb,4,"];\n\n");
}

const char BRDF_MATERIAL[] = "\
const float PI = 3.1415926;\n\
\n\
float heaviside(float x) {\n\
    return x > 0 ? 1 : 0;\n\
}\n\
\n\
float square(float x) {\n\
    return x * x;\n\
}\n\
\n\
float microfacet_distribution(float r_sq, float n_dot_h) {\n\
    float top = r_sq * heaviside(n_dot_h);\n\
    float bottom = PI * square(square(n_dot_h) * (r_sq - 1) + 1);\n\
    return top / bottom;\n\
}\n\
\n\
float masking_shadowing(float r_sq, float n_dot_l, float h_dot_l, float n_dot_v, float h_dot_v) {\n\
    float top_left = 2 * abs(n_dot_l) * heaviside(h_dot_l);\n\
    float bottom_left = abs(n_dot_l) + sqrt(r_sq + (1 - r_sq) * square(n_dot_l));\n\
    float top_right = 2 * abs(n_dot_v) * heaviside(h_dot_v);\n\
    float bottom_right = abs(n_dot_v) + sqrt(r_sq + (1 - r_sq) * square(n_dot_v));\n\
    return (top_left / bottom_left) * (top_right / bottom_right);\n\
}\n\
\n\
float specular_brdf(float r_sq, vec3 eye_pos, vec3 light_pos, vec3 surface_normal, vec3 half_vector) {\n\
    float D = microfacet_distribution(r_sq, dot(surface_normal, half_vector));\n\
    float G = masking_shadowing(r_sq, dot(surface_normal, light_pos), dot(half_vector, light_pos), dot(surface_normal, eye_pos), dot(half_vector, eye_pos));\n\
    float bottom = 4 * abs(dot(surface_normal, light_pos)) * abs(dot(surface_normal, eye_pos));\n\
    return (G * D) / bottom;\n\
}\n\
\n\
vec3 material_brdf(vec3 base_color, float metallic, float roughness, vec3 eye_pos, vec3 light_pos, vec3 surface_normal) {\n\
    vec3 half_vector = normalize(light_pos + eye_pos);\n\
    float v_dot_h = dot(eye_pos, half_vector);\n\
    vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);\n\
    vec3 fresnel = f0 + (1 - f0) * pow((1 - abs(v_dot_h)), 5);\n\
\n\
    vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);\n\
    float r_sq = roughness * roughness;\n\
\n\
    vec3 diffuse = (1 - fresnel) * (1 / PI) * c_diff;\n\
    vec3 specular = fresnel * specular_brdf(r_sq, eye_pos, light_pos, surface_normal, half_vector);\n\
\n\
    return diffuse + specular;\n\
}\n\
";

const char LIGHT_DECL[] = "\
    vec3 light = vec3(0,0,0);\n\
";

const char DIRECTIONAL_LIGHTING_NO_MATERIAL[] = "\
    for(uint i = 0; i < fs_info.dir_light_count; ++i) {\n\
        vec3 light_dir = normalize(fs_info.dir_lights[i].position - fs_info.frag_pos);\n\
        light = light + zero_if_in_shadow_directional(i) * max(dot(light_dir, in_normal));\n\
    }\n\n\
";

const char POINT_LIGHTING_NO_MATERIAL[] = "\
    for(uint i = 0; i < fs_info.point_light_count; ++i) {\n\
        vec3 light_dir = normalize(fs_info.point_lights[i].position - fs_info.frag_pos);\n\
\n\
        float attenuation = 1 / (1 + (fs_info.point_lights[i].linear * light_distance) +\
                square(fs_info.point_lights[i].linear * light_distance));\n\
\n\
        light = light + zero_if_in_shadow_point(i) * max(dot(light_dir, in_normal)) * attenuation;\n\
    }\n\n\
}\n\
";

const char DIRECTIONAL_LIGHTING[] = "\
    for(uint i = 0; i < fs_info.dir_light_count; ++i) {\n\
        vec3 light_dir = normalize(fs_info.dir_lights[i].position - fs_info.frag_pos);\n\
        vec3 brdf = material_brdf(base_color.xyz, metallic, roughness, fs_info.eye.xyz, light_dir, normal);\n\
\n\
        light = light + zero_if_in_shadow_directional(i) * brdf;\n\
    }\n\n\
";

const char POINT_LIGHTING[] = "\
    for(uint i = 0; i < fs_info.point_light_count; ++i) {\n\
        vec3 light_dir = normalize(fs_info.point_lights[i].position - fs_info.frag_pos);\n\
        vec3 brdf = material_brdf(base_color.xyz, metallic, roughness, fs_info.eye.xyz, light_dir, normal);\n\
\n\
        light = light + zero_if_in_shadow_point(i) * brdf * fs_info.point_lights[i].attenuation;\n\
    }\n\n\
";

const char FRAG_COLOR_ALPHA_CUTOFF[] = "\
    frag_color = vec4(light + emissive, base_color.w > alpha_cutoff);\n\
";

const char FRAG_COLOR[] = "\
    frag_color = vec4(light + emissive, base_color.w);\n\
";

const char FRAGMENT_MAIN_NO_LIGHTS_NO_MATERIAL[] = "\
    frag_color = vec4(base_color.xyz * fs_info.ambient, base_color.w);\n\
";

const char FRAGMENT_MAIN_NO_LIGHTS[] = "\
    frag_color = vec4(base_color.xyz * fs_info.ambient * occlusion + emissive,\n\
            base_color.w);\n\
";

const char FRAGMENT_MAIN_NO_LIGHTS_ALPHA_CUTOFF[] = "\
    frag_color = vec4(base_color.xyz * fs_info.ambient * occlusion + emissive,\n\
            base_color.w > material_ubo.alpha_cutoff);\n\
";

const char BASE_COLOR[] = "\
    vec4 base_color = texture(textures[x], in_texcoord_x) * material_ubo.base_color_factor;\n\
";

const char METALLIC_ROUGHNESS[] = "\
    vec4 metallic_roughness = texture(textures[x], in_texcoord_x);\n\
    float roughness = metallic_roughness.y * material_ubo.roughness_factor;\n\
    float metallic = metallic_roughness.z * material_ubo.metallic_factor;\n\
";

// @Todo I am not sure about the ordering of these ops.
const char NORMAL[] = "\
    vec3 normal = texture(textures[x], in_texcoord_x).xyz * 2 - 1;\n\
    normal.xy = normal.xy * material_ubo.normal_factor;\n\
    normal = normalize(normal);\n\
";

const char OCCLUSION[] = "\
    float occlusion = texture(textures[x], in_texcoord_x).x * material_ubo.occlusion_factor;\n\
";

const char EMISSIVE[] = "\
    vec3 emissive = texture(textures[x], in_texcoord_x).xyz;\n\
    base_color = mix(base_color.xyz, emissive, material_ubo.emissive_factor);\n\
";

const char TEX_ARRAY[] = "layout(set = 4, binding = 0) uniform sampler2D textures[";

const char NO_BASE_COLOR[] = "    vec4 base_color = vec4(0,0,0,1);\n";
const char NO_METALLIC_ROUGHNESS[] = "    float roughness = 1;\n    float metallic = 0;\n";
const char NO_NORMAL[] = "    vec3 normal = in_normal;\n";
const char NO_OCCLUSION[] = "    float occlusion = 1;\n";
const char NO_EMISSIVE[] = "    vec3 emissive = vec3(0,0,0);\n";

const char DIRECTIONAL_SHADOW_FUNCTION[] = "\
float zero_if_in_shadow_directional(uint light_index) {\n\
    vec3 projection = fs_info.dir_lights[light_index].light_space_frag_pos.xyz /\n\
            fs_info.dir_lights[light_index].light_space_frag_pos.w;\n\
    projection = projection * 0.5 + 0.5;\n\
    return float((projection.z - 0.05) < texture(directional_shadow_maps[light_index], projection.xy).r);\n\
}\n\n\
";

const char POINT_SHADOW_FUNCTION[] = "\
float zero_if_in_shadow_point(uint light_index) {\n\
    vec3 projection = fs_info.point_lights[light_index].light_space_frag_pos.xyz /\n\
            fs_info.point_lights[light_index].light_space_frag_pos.w;\n\
    projection = projection * 0.5 + 0.5;\n\
    return float((projection.z - 0.05) < texture(point_shadow_maps[light_index], projection).r);\n\
}\n\n\
";

// Used if there is no shadow mapping. Saves memcpys.
const char DIRECTIONAL_SHADOW_FUNCTION_EMPTY[] = "\
float zero_if_in_shadow_directional(uint light_index) {\n\
    return 1;\n\
}\n\
";
const char POINT_SHADOW_FUNCTION_EMPTY[] = "\
float zero_if_in_shadow_point(uint light_index) {\n\
    return 1;\n\
}\n\
";

const char END_LINE[] = ";\n";

const char DIRECTIONAL_SHADOW_MAPS_START[] = "layout(set = 1, binding = ";
const char DIRECTIONAL_SHADOW_MAPS_END[] = ") uniform sampler2D directional_shadow_maps";
const char POINT_SHADOW_MAPS_START[] = "layout(set = 1, binding = ";
const char POINT_SHADOW_MAPS_END[] = ") uniform sampler3D point_shadow_maps";

const char DIRECTIONAL_LIGHT_STRUCT[] = "\
struct Directional_Light {\n\
    vec3 color;\n\
    vec3 position;\n\
    vec4 light_space_frag_pos;\n\
};\n\
";

const char POINT_LIGHT_STRUCT[] = "\
struct Point_Light {\n\
    float attenuation;\n\
    vec3 color;\n\
    vec3 position;\n\
    vec4 light_space_frag_pos;\n\
};\n\
";

const char FRAGMENT_INFO_START[] = "layout(location = ";

const char FRAGMENT_INFO_IN_OUT[] = ") flat ";

const char FRAGMENT_INFO_DECL[] = " struct Fragment_Info {\n\
    vec3 eye;\n\
    vec3 frag_pos;\n\
    vec3 ambient;\n\
";

const char FS_INFO_DIRECTION_LIGHT_ARRAY_START[] = "\
    uint dir_light_count;\n\
    Directional_Light dir_lights[\
";

const char FS_INFO_POINT_LIGHT_ARRAY_START[] = "\
    uint point_light_count;\n\
    Point_Light point_lights[\
";

const char FRAGMENT_INFO_END[] = "\
} fs_info;\n\
";

const char MATERIAL_UBO[] = "layout(set = 3, binding = 1) uniform Material_Ubo {\n\
    vec4 base_color_factor;\n\
    float metallic_factor;\n\
    float roughness_factor;\n\
    float normal_scale;\n\
    float occlusion_strength;\n\
    vec3 emissive_factor;\n\
    float alpha_cutoff;\n\
} material_ubo;\n\n";

const char FRAGMENT_FN_MAIN[] = "\
layout(location = 0) out vec4 frag_color;\n\n\
void main() {\n\
";

const char CLOSE_FN[] = "}\n";

struct vertex_to_fragment_info {
    uint32 fs_info_location;
};

static void color_frag(
                       string_builder       *sb_vert,
                       string_builder       *sb_frag,
                       gltf                 *model,
                       uint32                mesh_index,
                       uint32                primitive_index,
                       struct shader_config *config)
{
    uint bi = 0;
    
    sb_add_if(sb_frag, cstr_as_array_len(BRDF_MATERIAL), BRDF_MATERIAL,
              model->meshes[mesh_index].primitives[primitive_index].material != Max_u32);
    
    sb_addnl_if(sb_frag,
                model->meshes[mesh_index].primitives[primitive_index].material != Max_u32);
    
    sb_insertnum_if(sb_frag,
                    cstr_as_array_len(DIRECTIONAL_SHADOW_MAPS_START), DIRECTIONAL_SHADOW_MAPS_START,
                    cstr_as_array_len(DIRECTIONAL_SHADOW_MAPS_END), DIRECTIONAL_SHADOW_MAPS_END,
                    bi,
                    config->dir_light_count);
    bi += config->dir_light_count != 0;
    
    sb_addarr_if(sb_frag,
                 0, "",
                 0, "",
                 config->dir_light_count,
                 config->dir_light_count);
    sb_endl_if(sb_frag, config->dir_light_count);
    
    sb_insertnum_if(sb_frag,
                    cstr_as_array_len(POINT_SHADOW_MAPS_START), POINT_SHADOW_MAPS_START,
                    cstr_as_array_len(POINT_SHADOW_MAPS_END), POINT_SHADOW_MAPS_END,
                    bi,
                    config->point_light_count);
    bi += config->point_light_count != 0;
    
    sb_addarr_if(sb_frag,
                 0, "",
                 0, "",
                 config->point_light_count,
                 config->point_light_count);
    sb_endl_if(sb_frag, config->point_light_count);
    
    sb_addnl(sb_frag);
    
    // shadow fns
    sb_add_if(sb_frag, cstr_as_array_len(DIRECTIONAL_SHADOW_FUNCTION),
              DIRECTIONAL_SHADOW_FUNCTION, config->dir_light_count);
    sb_add_if(sb_frag, cstr_as_array_len(DIRECTIONAL_SHADOW_FUNCTION_EMPTY),
              DIRECTIONAL_SHADOW_FUNCTION_EMPTY, !(config->dir_light_count));
    
    sb_add_if(sb_frag, cstr_as_array_len(POINT_SHADOW_FUNCTION),
              POINT_SHADOW_FUNCTION,  config->point_light_count);
    sb_add_if(sb_frag, cstr_as_array_len(POINT_SHADOW_FUNCTION_EMPTY),
              POINT_SHADOW_FUNCTION_EMPTY,  !(config->point_light_count));
    
    // using if as indexing gltf.materials is required
    if (model->meshes[mesh_index].primitives[primitive_index].material == Max_u32) {
        log_print_error("@UntestedCodePath");
        
        sb_add(sb_frag, cstr_as_array_len(FRAGMENT_FN_MAIN), FRAGMENT_FN_MAIN);
        
        sb_add_if(sb_frag, cstr_as_array_len(FRAGMENT_MAIN_NO_LIGHTS_NO_MATERIAL),
                  FRAGMENT_MAIN_NO_LIGHTS_NO_MATERIAL,
                  !(config->dir_light_count || config->point_light_count));
        
        sb_add_if(sb_frag, cstr_as_array_len(DIRECTIONAL_LIGHTING_NO_MATERIAL),
                  DIRECTIONAL_LIGHTING_NO_MATERIAL, config->dir_light_count);
        sb_add_if(sb_frag, cstr_as_array_len(POINT_LIGHTING_NO_MATERIAL),
                  POINT_LIGHTING_NO_MATERIAL, max_if(config->point_light_count));
        
        sb_add(sb_frag, cstr_as_array_len(CLOSE_FN), CLOSE_FN);
        
    } else {
        // material uniforms
        sb_add(sb_frag, cstr_as_array_len(MATERIAL_UBO), MATERIAL_UBO);
        
        gltf_material *mat = &model->materials[model->meshes[mesh_index]
                                               .primitives[primitive_index].material];
        
        // texture array
        sb_add_if(sb_frag,cstr_as_array_len(TEX_ARRAY),TEX_ARRAY,
                  mat->flags & GLTF_MATERIAL_TEXTURE_BITS);
        assert(popcnt(mat->flags & GLTF_MATERIAL_TEXTURE_BITS) < 10);
        sb_add_digit_if(sb_frag,popcnt(mat->flags & GLTF_MATERIAL_TEXTURE_BITS),
                        mat->flags & GLTF_MATERIAL_TEXTURE_BITS);
        sb_close_arr_and_endl_if(sb_frag,mat->flags & GLTF_MATERIAL_TEXTURE_BITS);
        
        sb_addnl_if(sb_frag,mat->flags & GLTF_MATERIAL_TEXTURE_BITS);
        sb_addnl_if(sb_frag,mat->flags & GLTF_MATERIAL_TEXTURE_BITS);
        
        // begin main
        sb_add(sb_frag, cstr_as_array_len(FRAGMENT_FN_MAIN), FRAGMENT_FN_MAIN);
        
        bi = 0;
        uint tmp = sb_frag->used;
        
        assert(mat->base_color.texcoord < 10 || mat->base_color.texcoord == Max_u32);
        sb_add_if(sb_frag,cstr_as_array_len(BASE_COLOR),BASE_COLOR,
                  mat->flags & GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+39,mat->base_color.texcoord,
                            mat->flags & GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+55,bi,
                            mat->flags & GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT);
        sb_add_if(sb_frag, cstr_as_array_len(NO_BASE_COLOR), NO_BASE_COLOR,
                  !(mat->flags & GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT));
        bi += flag_check(mat->flags, GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT);
        
        tmp = sb_frag->used;
        
        assert(mat->metallic_roughness.texcoord < 10 || mat->metallic_roughness.texcoord == Max_u32);
        sb_add_if(sb_frag,cstr_as_array_len(METALLIC_ROUGHNESS),METALLIC_ROUGHNESS,
                  mat->flags & GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+47,mat->metallic_roughness.texcoord,
                            mat->flags & GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+63,bi,
                            mat->flags & GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT);
        sb_add_if(sb_frag, cstr_as_array_len(NO_METALLIC_ROUGHNESS), NO_METALLIC_ROUGHNESS,
                  !(mat->flags & GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT));
        bi += flag_check(mat->flags, GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT);
        
        tmp = sb_frag->used;
        
        assert(mat->normal.texcoord < 10 || mat->normal.texcoord == Max_u32);
        sb_add_if(sb_frag,cstr_as_array_len(NORMAL),NORMAL,
                  mat->flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+35,mat->normal.texcoord,
                            mat->flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+51,bi,
                            mat->flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT);
        sb_add_if(sb_frag, cstr_as_array_len(NO_NORMAL), NO_NORMAL,
                  !(mat->flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT));
        bi += flag_check(mat->flags, GLTF_MATERIAL_NORMAL_TEXTURE_BIT);
        
        tmp = sb_frag->used;
        
        assert(mat->occlusion.texcoord < 10 || mat->occlusion.texcoord == Max_u32);
        sb_add_if(sb_frag,cstr_as_array_len(OCCLUSION),OCCLUSION,
                  mat->flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+39,mat->occlusion.texcoord,
                            mat->flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+55,bi,
                            mat->flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT);
        sb_add_if(sb_frag, cstr_as_array_len(NO_OCCLUSION), NO_OCCLUSION,
                  !(mat->flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT));
        bi += flag_check(mat->flags, GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT);
        
        tmp = sb_frag->used;
        
        assert(mat->emissive.texcoord < 10 || mat->emissive.texcoord == Max_u32);
        sb_add_if(sb_frag,cstr_as_array_len(EMISSIVE),EMISSIVE,
                  mat->flags & GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+39,mat->emissive.texcoord,
                            mat->flags & GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT);
        sb_replace_digit_if(sb_frag,tmp+55,bi,
                            mat->flags & GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT);
        sb_add_if(sb_frag, cstr_as_array_len(NO_EMISSIVE), NO_EMISSIVE,
                  !(mat->flags & GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT));
        bi += flag_check(mat->flags, GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT);
        
        sb_addnl(sb_frag);
        sb_add(sb_frag, 26, "    vec3 light = vec3(0);\n");
        sb_addnl(sb_frag);
        
        // lighting calculations
        sb_add_if(sb_frag, cstr_as_array_len(FRAGMENT_MAIN_NO_LIGHTS_ALPHA_CUTOFF),
                  FRAGMENT_MAIN_NO_LIGHTS_ALPHA_CUTOFF,
                  !(config->dir_light_count || config->point_light_count) &&
                  (mat->flags & GLTF_MATERIAL_ALPHA_MODE_MASK_BIT));
        
        sb_add_if(sb_frag, cstr_as_array_len(FRAGMENT_MAIN_NO_LIGHTS), FRAGMENT_MAIN_NO_LIGHTS,
                  !(config->dir_light_count || config->point_light_count));
        
        sb_add_if(sb_frag, cstr_as_array_len(DIRECTIONAL_LIGHTING), DIRECTIONAL_LIGHTING,
                  config->dir_light_count);
        
        sb_add_if(sb_frag, cstr_as_array_len(POINT_LIGHTING), POINT_LIGHTING,
                  config->point_light_count);
        
        // output
        sb_add_if(sb_frag, cstr_as_array_len(FRAG_COLOR_ALPHA_CUTOFF), FRAG_COLOR_ALPHA_CUTOFF,
                  (config->dir_light_count || config->point_light_count) &&
                  (mat->flags & GLTF_MATERIAL_ALPHA_MODE_MASK_BIT));
        
        sb_add_if(sb_frag, cstr_as_array_len(FRAG_COLOR), FRAG_COLOR,
                  config->dir_light_count || config->point_light_count);
        
        // end main
        sb_add(sb_frag, cstr_as_array_len(CLOSE_FN), CLOSE_FN);
    }
}

static void gen_fsinfo(string_builder *sb, uint loc, uint dl_cnt, uint pl_cnt, bool vertex)
{
    char num_buf[8];
    uint num_len = uint_to_ascii(loc, num_buf);
    
    sb_add_if(sb, cstr_as_array_len(DIRECTIONAL_LIGHT_STRUCT),
              DIRECTIONAL_LIGHT_STRUCT, dl_cnt);
    sb_add_if(sb, cstr_as_array_len(POINT_LIGHT_STRUCT), POINT_LIGHT_STRUCT, pl_cnt);
    
    sb_addnl_if(sb, dl_cnt || pl_cnt);
    
    sb_add(sb, cstr_as_array_len(FRAGMENT_INFO_START), FRAGMENT_INFO_START);
    sb_add(sb, num_len, num_buf);
    
    sb_add(sb, cstr_as_array_len(FRAGMENT_INFO_IN_OUT), FRAGMENT_INFO_IN_OUT);
    sb_add_if(sb, 3, "out", vertex);
    sb_add_if(sb, 2, "in", !vertex);
    sb_add(sb, cstr_as_array_len(FRAGMENT_INFO_DECL), FRAGMENT_INFO_DECL);
    
    sb_insertnum_if(sb, cstr_as_array_len(FS_INFO_DIRECTION_LIGHT_ARRAY_START),
                    FS_INFO_DIRECTION_LIGHT_ARRAY_START, cstr_as_array_len(CLOSE_ARRAY),
                    CLOSE_ARRAY, dl_cnt, dl_cnt);
    
    sb_insertnum_if(sb,
                    cstr_as_array_len(FS_INFO_POINT_LIGHT_ARRAY_START),
                    FS_INFO_POINT_LIGHT_ARRAY_START, cstr_as_array_len(CLOSE_ARRAY),
                    CLOSE_ARRAY, pl_cnt, pl_cnt);
    
    sb_add(sb,cstr_as_array_len(FRAGMENT_INFO_END), FRAGMENT_INFO_END);
    sb_addnl(sb);
}
#define gen_fsinfo_vert(d,loc,dlc,plc) gen_fsinfo(d, loc, dlc, plc, true)
#define gen_fsinfo_frag(d,loc,dlc,plc) gen_fsinfo(d, loc, dlc, plc, false)

struct gen_attr_info {
    uint flags;
    uint morph_mask;
    uint attr_n;
};

static inline void weight_calc_short(string_builder *sb, uint mask,
                                     uint len, const char *str)
{
    const char w[] = " +\n        transforms_ubo[gl_InstanceIndex].weights[";
    char nb[8];
    uint nl;
    uint pc = pop_count32(mask);
    uint i,tz;
    for(i=0;i<pc;++i) {
        tz = ctz32(mask);
        mask &= ~(1<<tz);
        
        nl = uint_to_ascii(tz,nb);
        sb_add(sb,cstr_as_array_len(w),w);
        sb_add(sb,nl,nb);
        sb_add(sb,len,str);
    }
    sb_add(sb,2,";\n");
}

const char POS_CALC[] = "\
    vec4 world_pos = transforms_ubo[gl_InstanceIndex].node_trs * vec4(position, 1.0);\n\
    vec4 view_pos = vs_info.view * world_pos;\n\
    gl_Position = vs_info.proj * view_pos;\n\
    gl_Position.y = -gl_Position.y;\n\
";
const char POS_CALC_SKIN[] = "\
    vec4 world_pos = skin * vec4(position, 1.0);\n\
    vec4 view_pos = vs_info.view * world_pos;\n\
    gl_Position = vs_info.projection * view_pos;\n\
    gl_Position.y = -gl_Position.y;\n\
";

const char NORM_CALC[] = "\
    out_normal = mat3(transforms_ubo[gl_InstanceIndex].node_tbn) * normal;\n\
";
const char NORM_CALC_SKIN[] = "\
    out_normal = tbn * normal;\n\
";

// @Error? Are tangents supposed to use the tbn? I forget. I assume so since
// they are in the same space as normals, and the desired transform is the same I think.
const char TANG_CALC[] = "\
    vec3 ts_tangent = mat3(transforms_ubo[gl_InstanceIndex].node_tbn) * tangent;\n\
";
const char TANG_CALC_SKIN[] = "\
    vec3 ts_tang_tangent = tbn * tangent;\n\
";

static void gen_pos(string_builder *sb, struct gen_attr_info *info)
{
    const char pos[] = "    vec3 position = in_position";
    const char m_pos[] = "] * in_position_morph_";
    
    sb_add(sb,cstr_as_array_len(pos),pos);
    weight_calc_short(sb,info->morph_mask,cstr_as_array_len(m_pos),m_pos);
    sb_add_if(sb,cstr_as_array_len(POS_CALC), POS_CALC, !(info->flags & SKIN_BIT));
    sb_add_if(sb,cstr_as_array_len(POS_CALC_SKIN), POS_CALC_SKIN, info->flags & SKIN_BIT);
}

static void gen_norm(string_builder *sb, struct gen_attr_info *info)
{
    const char norm[] = "    vec3 normal = in_normal";
    const char m_norm[] = "] * in_normal_morph_";
    
    sb_add(sb,cstr_as_array_len(norm), norm);
    weight_calc_short(sb,info->morph_mask,cstr_as_array_len(m_norm),m_norm);
    sb_add_if(sb,cstr_as_array_len(NORM_CALC), NORM_CALC, !(info->flags & SKIN_BIT));
    sb_add_if(sb,cstr_as_array_len(NORM_CALC_SKIN), NORM_CALC_SKIN, info->flags & SKIN_BIT);
}

static void gen_tang(string_builder *sb, struct gen_attr_info *info)
{
    const char tang[] = "    vec3 tangent = in_tangent";
    const char m_tang[] = "] * in_tangent_morph_";
    
    sb_add(sb,cstr_as_array_len(tang), tang);
    weight_calc_short(sb,info->morph_mask,cstr_as_array_len(m_tang),m_tang);
    sb_add_if(sb,cstr_as_array_len(TANG_CALC), TANG_CALC, !(info->flags & SKIN_BIT));
    sb_add_if(sb,cstr_as_array_len(TANG_CALC_SKIN), TANG_CALC_SKIN, info->flags & SKIN_BIT);
}

static inline uint find_morphed_attr(gltf_mesh_primitive_morph_target *tgt, uint t, uint n)
{
    uint ret = 0;
    uint i;
    for(i=0;i<tgt->attribute_count;++i) {
        ret += i & max_if(tgt->attributes[i].n == n &&
                          tgt->attributes[i].type == t);
        assert(ret == 0 ||
               !(tgt->attributes[i].n == n &&
                 tgt->attributes[i].type == t));
    }
    return ret;
}

struct gen_morphed_attr_info {
    uint mm; // morph_mask
    gltf_mesh_primitive_attribute *pa; // single
    gltf_mesh_primitive_morph_target *mt; // array
    gltf_accessor *ac; // array
};

static void gen_texc(string_builder *sb, struct gen_morphed_attr_info *info)
{
    char m_tex[] = "    vec2 m_texc_x = ";
    m_tex[16] = info->pa->n + '0';
    sb_add_if(sb,cstr_as_array_len(m_tex),m_tex,info->mm);
    
    char m_tcf[] = "] * in_texcoord_x_morph_";
    char m_tcub[] = "] * u8_to_vec2(in_texcoord_x_morph_";
    char m_tcus[] = "] * u16_to_vec2(in_texcoord_x_morph_";
    char m_tcsb[] = "] * s8_to_vec2(in_texcoord_x_morph_";
    char m_tcss[] = "] * s16_to_vec2(in_texcoord_x_morph_";
    
    m_tcf[16] = info->pa->n + '0';
    m_tcub[27] = info->pa->n + '0';
    m_tcus[28] = info->pa->n + '0';
    m_tcsb[27] = info->pa->n + '0';
    m_tcss[28] = info->pa->n + '0';
    
    char mw[] = "\n        transforms_ubo[gl_InstanceIndex].weights[";
    
    char nb[8];
    uint nl;
    
    uint m = info->mm;
    uint pc = popcnt(m);
    uint i,tz,tmp;
    for(i=0;i<pc;++i) {
        tz = ctz(m);
        m &= (~1<<tz);
        
        sb_add(sb,cstr_as_array_len(mw),mw);
        nl = uint_to_ascii(tz,nb);
        sb_add(sb,nl,nb);
        
        tmp = find_morphed_attr(&info->mt[tz],info->pa->type,info->pa->n);
        tmp = info->ac[info->mt[tz].attributes[tmp].accessor].vkformat;
        
        sb_add_if(sb,cstr_as_array_len(m_tcf),m_tcf,
                  tmp == VK_FORMAT_R32G32_SFLOAT);
        sb_add_if(sb,cstr_as_array_len(m_tcub),m_tcub,
                  tmp == VK_FORMAT_R8G8_UNORM);
        sb_add_if(sb,cstr_as_array_len(m_tcsb),m_tcsb,
                  tmp == VK_FORMAT_R8G8_SNORM);
        sb_add_if(sb,cstr_as_array_len(m_tcus),m_tcus,
                  tmp == VK_FORMAT_R16G16_UNORM);
        sb_add_if(sb,cstr_as_array_len(m_tcss),m_tcss,
                  tmp == VK_FORMAT_R16G16_SNORM);
        
        nb[nl] = ')';
        sb_add(sb,nl+1,nb);
        
        sb_add_if(sb,2," +",i<pc-1);
    }
    sb_add_if(sb,3,";\n",pc);
    
    char tc[] = "    out_texcoord_x = in_texcoord_x";
    tc[17] = info->pa->n + '0';
    tc[33] = info->pa->n + '0';
    
    sb_add(sb,cstr_as_array_len(tc),tc);
    
    char m_in[] = " + m_texc_x;\n";
    m_in[10] = info->pa->n + '0';
    
    sb_add_if(sb,cstr_as_array_len(m_in),m_in,pc);
    sb_add_if(sb,2,";\n",!pc);
}

static void gen_jw(string_builder *sb,
                   uint attr_n, uint j_comp_t, uint w_comp_t)
{
    char jb[] = "    uvec4 joints_x = u8_to_uvec4(in_joints_x);\n";
    char js[] = "    uvec4 joints_x = u16_to_uvec4(in_joints_x);\n";
    char wf[] = "    vec4 weights_x = in_weights_x;\n";
    char wb[] = "    vec4 weights_x = u8_to_vec4(in_weights_x);\n";
    char ws[] = "    vec4 weights_x = u16_to_vec4(in_weights_x);\n";
    
    jb[17] = attr_n + '0';
    js[17] = attr_n + '0';
    jb[cstr_as_array_len(jb)-4] = attr_n + '0';
    js[cstr_as_array_len(js)-4] = attr_n + '0';
    
    wf[17] = attr_n + '0';
    wb[17] = attr_n + '0';
    ws[17] = attr_n + '0';
    wf[cstr_as_array_len(wf)-3] = attr_n + '0';
    wb[cstr_as_array_len(wb)-4] = attr_n + '0';
    ws[cstr_as_array_len(ws)-4] = attr_n + '0';
    
    uint64 j = (uint64)jb & max_if(j_comp_t == BYTE);
    j += (uint64)js & max_if(j_comp_t == SHORT);
    
    uint64 w = (uint64)wf & max_if(w_comp_t == FLOAT);
    w += (uint64)wb & max_if(w_comp_t == BYTE);
    w += (uint64)ws & max_if(w_comp_t == SHORT);
    
    uint wl = cstr_as_array_len(wf) & max_if(w_comp_t == FLOAT);
    wl += cstr_as_array_len(wb) & max_if(w_comp_t == BYTE);
    wl += cstr_as_array_len(ws) & max_if(w_comp_t == SHORT);
    
    sb_add(sb,cstr_as_array_len(jb) + flag_check(j_comp_t, SHORT),(char*)j);
    sb_add(sb,wl,(char*)w);
}

const char SKIN_CALC[] = "\
        weights_x.x * transforms_ubo[gl_InstanceIndex].joints_trs[joints_x.x] +\n\
        weights_x.y * transforms_ubo[gl_InstanceIndex].joints_trs[joints_x.y] +\n\
        weights_x.z * transforms_ubo[gl_InstanceIndex].joints_trs[joints_x.z] +\n\
        weights_x.w * transforms_ubo[gl_InstanceIndex].joints_trs[joints_x.w]\
";

const char SKIN_CALC_TBN[] = "\
        weights_x.x * transforms_ubo[gl_InstanceIndex].joints_tbn[joints_x.x] +\n\
        weights_x.y * transforms_ubo[gl_InstanceIndex].joints_tbn[joints_x.y] +\n\
        weights_x.z * transforms_ubo[gl_InstanceIndex].joints_tbn[joints_x.z] +\n\
        weights_x.w * transforms_ubo[gl_InstanceIndex].joints_tbn[joints_x.w]\
";

static void skin_mat(string_builder *sb, gltf_mesh_primitive *prim, struct attr_map *am)
{
    const char skin[] = "    mat4 skin = \n";
    const char tbn[] = "    mat3 tbn = mat3(\n";
    const char plus[] = " +\n";
    
    uint i,offs;
    
    sb_add(sb,cstr_as_array_len(skin),skin);
    for(i=0;i<am->attrs[JNTS].n;++i) {
        offs = sb->used;
        sb_add(sb,cstr_as_array_len(SKIN_CALC),SKIN_CALC);
        
        sb_replace_digit(sb,offs+16+80*0,i);
        sb_replace_digit(sb,offs+73+80*0,i);
        sb_replace_digit(sb,offs+16+80*1,i);
        sb_replace_digit(sb,offs+73+80*1,i);
        sb_replace_digit(sb,offs+16+80*2,i);
        sb_replace_digit(sb,offs+73+80*2,i);
        sb_replace_digit(sb,offs+16+80*3,i);
        sb_replace_digit(sb,offs+73+80*3,i);
        
        sb_add_if(sb,cstr_as_array_len(plus),plus,i<am->attrs[JNTS].n-1);
    }
    sb_add(sb,2,";\n");
    
    sb_add(sb,cstr_as_array_len(tbn),tbn);
    for(i=0;i<am->attrs[JNTS].n;++i) {
        offs = sb->used;
        sb_add(sb,cstr_as_array_len(SKIN_CALC_TBN),SKIN_CALC_TBN);
        
        sb_replace_digit(sb,offs+16+80*0,i);
        sb_replace_digit(sb,offs+73+80*0,i);
        sb_replace_digit(sb,offs+16+80*1,i);
        sb_replace_digit(sb,offs+73+80*1,i);
        sb_replace_digit(sb,offs+16+80*2,i);
        sb_replace_digit(sb,offs+73+80*2,i);
        sb_replace_digit(sb,offs+16+80*3,i);
        sb_replace_digit(sb,offs+73+80*3,i);
        
        sb_add_if(sb,cstr_as_array_len(plus),plus,i<am->attrs[JNTS].n-1);
    }
    sb_add(sb,4,");\n\n");
}

// @Check the maths here.
const char FS_INFO_OUT_BASE[] = "\
    fs_info.eye = tbn * vec3(view_pos);\n\
    fs_info.frag_pos = tbn * vec3(world_pos);\n\
    fs_info.ambient = vs_info.ambient;\n\n\
";

const char FS_INFO_OUT_DIR_LIGHTING[] = "\
    fs_info.dir_light_count = 0;\n\
    for(uint i=0;i<vs_info.dir_light_count;++i) {\n\
        vec3 P = vs_info.dir_lights[i].position - vec3(world_pos);\n\
        vec3 N = vs_info.dir_lights[i].direction;\n\
        bool vis = dot(N,P) > vs_info.dir_lights[i].half_angle;\n\
        fs_info.dir_lights[fs_info.dir_light_count].color = vs_info.dir_lights[i].color;\n\
        fs_info.dir_lights[fs_info.dir_light_count].position = tbn * vs_info.dir_lights[i].position;\n\
        fs_info.dir_lights[fs_info.dir_light_count].light_space_frag_pos = vs_info.dir_lights[i].space * world_pos;\n\
        fs_info.dir_light_count = fs_info.dir_light_count + uint(vis);\n\
    }\n\n\
";

const char FS_INFO_OUT_POINT_LIGHTING[] = "\
    fs_info.point_light_count = 0;\n\
    for(uint i=0;i<vs_info.dir_light_count;++i) {\n\
        fs_info.point_lights[fs_info.point_light_count].color = vs_info.point_lights[i].color;\n\
        fs_info.point_lights[fs_info.point_light_count].position = tbn * vs_info.point_lights[i].position;\n\
\n\
        // This is wrong. I am just scaffolding point lights for now.\n\
        fs_info.point_lights[fs_info.point_light_count].light_space_frag_pos =\n\
            vs_info.point_lights[i].space * world_pos;\n\
\n\
        float d = length(vs_info.point_lights[i].position - vec3(world_pos));\n\
        fs_info.point_lights[fs_info.point_light_count].attenuation = 1 /\n\
            (1 + vs_info.point_lights[i].linear * d + vs_info.point_lights[i].quadratic * d*d);\n\
\n\
        // @Test 0.05 is completely arbitrary, it may be too strong or too weak.\n\
        fs_info.point_light_count = fs_info.point_light_count +\n\
            uint(fs_info.point_lights[fs_info.point_light_count].attenuation > 0.05);\n\
    }\n\n\
";

struct vert_main_info {
    struct shader_config *config;
    gltf                 *model;
    uint32                mesh_index;
    uint32                primitive_index;
    struct attr_map      *am;
    struct morph_map     *mm;
};

static void vert_main(string_builder *sb, struct vert_main_info *info)
{
    gltf_mesh_primitive *prim =
        &info->model->meshes[info->mesh_index].primitives[info->primitive_index];
    
    char mainfn[] = "void main() {\n";
    sb_add(sb,cstr_as_array_len(mainfn),mainfn);
    
    uint i;
    
    // @Todo could convert all these 'ifs' to branchless, but I cannot think of
    // a super clean way immediately, so them for now.
    
    if (info->model->meshes[info->mesh_index].joint_count) {
        assert(info->am->attrs[JNTS].n == info->am->attrs[WGHTS].n);
        
        uint j_comp_t,w_comp_t;
        for(i=0;i<info->am->attrs[JNTS].n;++i) {
            j_comp_t = ctz(info->model->accessors[
                                                  prim->attributes[
                                                                   info->am->attrs[JNTS].i+i].accessor].flags &
                           GLTF_ACCESSOR_COMPONENT_TYPE_BITS);
            
            w_comp_t = ctz(info->model->accessors[
                                                  prim->attributes[
                                                                   info->am->attrs[WGHTS].i+i].accessor].flags &
                           GLTF_ACCESSOR_COMPONENT_TYPE_BITS);
            
            gen_jw(sb,i,j_comp_t,w_comp_t);
            sb_addnl(sb);
        }
        skin_mat(sb,prim,info->am);
        
        sb_addnl(sb);
    }
    
    struct gen_attr_info gai;
    gai.flags = SKIN_BIT & max_if(info->model->meshes[info->mesh_index].joint_count);
    
    if (info->am->attrs[POS].n) {
        gai.morph_mask = info->mm->m[info->mm->i[POS]];
        gen_pos(sb,&gai);
        sb_addnl(sb);
    }
    if (info->am->attrs[NORM].n) {
        gai.morph_mask = info->mm->m[info->mm->i[NORM]];
        gen_norm(sb,&gai);
        sb_addnl(sb);
    }
    if (info->am->attrs[TANG].n) {
        gai.morph_mask = info->mm->m[info->mm->i[TANG]];
        gen_tang(sb,&gai);
        sb_addnl(sb);
    }
    
    struct gen_morphed_attr_info gmai;
    for(i=0;i<info->am->attrs[TEXC].n;++i) {
        gmai.mm = info->mm->m[info->mm->i[TEXC]+i];
        gmai.pa = &prim->attributes[info->am->attrs[TEXC].i+i];
        gmai.mt = prim->morph_targets;
        gmai.ac = info->model->accessors;
        gen_texc(sb,&gmai);
    }
    sb_addnl(sb);
    
    sb_add(sb,cstr_as_array_len(FS_INFO_OUT_BASE),FS_INFO_OUT_BASE);
    sb_add_if(sb,cstr_as_array_len(FS_INFO_OUT_DIR_LIGHTING),
              FS_INFO_OUT_DIR_LIGHTING,info->config->dir_light_count);
    sb_add_if(sb,cstr_as_array_len(FS_INFO_OUT_POINT_LIGHTING),
              FS_INFO_OUT_POINT_LIGHTING,info->config->point_light_count);
    
    char closefn[] = "}\n";
    sb_add(sb,cstr_as_array_len(closefn),closefn);
}

struct color_vert_info {
    gltf *model;
    uint32 mesh_index;
    uint32 primitive_index;
    struct shader_config *config;
    struct attr_map *am;
    struct morph_map *mm;
};

static void color_vert(
                       string_builder         *sb_vert,
                       string_builder         *sb_frag,
                       struct color_vert_info *info)
{
    uint num_len;
    char num_buf[8];
    
    struct generate_layout_info gli;
    gli.mti = -1;
    gli.sb_vert = sb_vert;
    gli.sb_frag = sb_frag;
    gli.model = info->model;
    gli.fn_deps = 0x0;
    
    gltf_mesh_primitive *prim = &info->model->meshes[info->mesh_index]
        .primitives[info->primitive_index];
    
    uint loc = 0;
    uint i,j;
    for(i = 0; i < prim->attribute_count; ++i) {
        num_len = uint_to_ascii(loc, num_buf);
        loc++;
        
        gli.attr = &prim->attributes[i];
        gli.len_location = num_len;
        gli.location = num_buf;
        
        LAYOUT_GENERATOR_FUNCTIONS[prim->attributes[i].type](&gli);
    }
    sb_addnl(sb_vert);
    
    j = popcnt(gli.fn_deps);
    uint tz;
    for(i=0;i<j;++i) {
        tz = ctz(gli.fn_deps);
        gli.fn_deps &= ~(1<<tz);
        sb_add(sb_vert,CNVRT_FNS[tz].len,CNVRT_FNS[tz].str);
    }
    
    // @Optimise Current solution to morphing is to loop once for layouts, then
    // again later when calculating. I am sure there is a better way, but my
    // brain is just not great rn. This really seems fine anyway, there cannot
    // be much overhead to this, as these target arrays will be tiny.
    for(i = 0; i < prim->target_count; ++i) {
        gli.mti = i;
        for(j = 0; j < prim->morph_targets[i].attribute_count; ++j) {
            num_len = uint_to_ascii(loc, num_buf);
            
            gli.attr = &prim->morph_targets[i].attributes[j];
            gli.len_location = num_len;
            gli.location = num_buf;
            
            LAYOUT_GENERATOR_FUNCTIONS[prim->morph_targets[i].attributes[j].type](&gli);
        }
    }
    
    sb_addnl(sb_vert);
    sb_addnl(sb_frag);
    
    sb_add_if(sb_vert,cstr_as_array_len(IN_DIR_LIGHT_STRUCT),
              IN_DIR_LIGHT_STRUCT,info->config->dir_light_count);
    sb_add_if(sb_vert,cstr_as_array_len(IN_POINT_LIGHT_STRUCT),
              IN_POINT_LIGHT_STRUCT,info->config->point_light_count);
    sb_addnl_if(sb_vert,
                info->config->point_light_count || info->config->dir_light_count);
    
    gen_vsinfo(sb_vert,info->config->dir_light_count,info->config->point_light_count);
    
    gen_transforms_ubo(sb_vert,info->model,info->mesh_index);
    
    gen_fsinfo_vert(sb_vert, loc,
                    info->config->dir_light_count, info->config->point_light_count);
    gen_fsinfo_frag(sb_frag, loc,
                    info->config->dir_light_count, info->config->point_light_count);
    
    struct vert_main_info vmi;
    vmi.config = info->config;
    vmi.model = info->model;
    vmi.mesh_index = info->mesh_index;
    vmi.primitive_index = info->primitive_index;
    vmi.am = info->am;
    vmi.mm = info->mm;
    vert_main(sb_vert, &vmi);
}

struct vulkan_stuff_info {
    struct shader_config *conf;
    uint mat_flags;
    uint fmt_cnt;
    uint *strides;
    VkFormat *fmts;
};

struct prim_hash {
    struct shader_config conf;
    struct attr_map am;
    struct morph_map mm;
    uint mat_flags;
    uint joint_cnt;
    VkFormat fmts[MAX_ATTRS*2]; // attrs + morphed attrs
};

static inline uint dir_hash_i(struct shader_dir *dir, uint64 hash)
{
    __m128i a;
    __m128i b = _mm_set1_epi64((__m64)hash);
    uint16 m;
    uint ret = 0;
    bool f = 0;
    uint len = array_len(dir->hashes);
    uint i;
    for(i=0;i<len;i+=2) {
        a = _mm_load_si128((__m128i*)(dir->hashes + i));
        a = _mm_cmpeq_epi64(a,b);
        m = _mm_movemask_epi8(a);
        ret += (ctz(m) >> 3) & max_if(popcnt(m));
        f = popcnt(m) || f;
        assert(!f || popcnt(m));
    }
    return ret | max_if(!f);
}

static inline uint dir_add(struct shader_dir *dir, uint64 hash, struct shader_set *set)
{
    array_add(dir->hashes,hash);
    array_add(dir->sets,*set);
    return array_len(dir->hashes)-1;
}

#define MAX_SHADER_SIZE 8096

struct shader_cl {
    shaderc_compiler_t cl;
    shaderc_compile_options_t opt;
};

// @TODO I think that this will fail to find a set which does not have
// an exact matching joint count or instance count, but this should really
// only be if the joint count or instance count is greater. Maybe then,
// if a shader which requires more joints or instances is found, but everyhting
// else matches, then the other shaders which accomodate fewer are removed from
// the dir and are instead pointed at this new one.
static uint find_or_generate_shader_set(
    struct shader_dir    *dir,
    struct shader_cl     *cl,
    struct shader_config *conf,
    gltf                 *model,
    uint                  mesh_index,
    uint                  primitive_index)
{
    gltf_mesh_primitive *prim =
        &model->meshes[mesh_index].primitives[primitive_index];
    
    struct prim_hash phi;
    phi.conf = *conf;
    phi.mat_flags = prim->material+1 ?
        model->materials[prim->material].flags : 0x0;
    phi.joint_cnt = model->meshes[mesh_index].joint_count;
    
    SHDR_BM_HASH_START;
    
    get_attr_info(prim,&phi.am,&phi.mm);
    
    memset(phi.fmts,0,sizeof(phi.fmts));
    uint cnt = 0;
    
    uint i,j;
    for(i=0;i<prim->attribute_count;++i) {
        phi.fmts[cnt++] = model->accessors[
                                           prim->attributes[i].accessor].vkformat;
        assert(cnt <= MAX_ATTRS*2);
    }
    for(i=0;i<prim->target_count;++i)
        for(j=0;j<prim->morph_targets[i].attribute_count;++j) {
        phi.fmts[cnt++] = model->accessors[
                                           prim->morph_targets[i].attributes[j].accessor].vkformat;
        assert(cnt <= MAX_ATTRS*2);
    }
    
    uint64 ph = get_hash(sizeof(phi),&phi);
    uint set_i = dir_hash_i(dir,ph);
    
    SHDR_BM_HASH_END;
    
    if (set_i != Max_u32) {
        print_auto_shader("Matched shader set %u with model %s, mesh %u, primitive %u",
                          set_i,model->dir.cstr,mesh_index,primitive_index);
        return set_i;
    } else {
        print_auto_shader("Generating shader for model %s, mesh %u, primitive %u",
                          model->dir.cstr,mesh_index,primitive_index);
    }
    
    string_builder sb_vert = sb_new(MAX_SHADER_SIZE, allocate(dir->alloc, MAX_SHADER_SIZE));
    string_builder sb_frag = sb_new(MAX_SHADER_SIZE, allocate(dir->alloc, MAX_SHADER_SIZE));
    
    const char glsl_version[] = "#version 450\n";
    uint32 vert_pos = cstr_as_array_len(glsl_version);
    uint32 frag_pos = cstr_as_array_len(glsl_version);
    sb_add(&sb_vert, vert_pos, glsl_version);
    sb_add(&sb_frag, frag_pos, glsl_version);
    
    sb_addnl(&sb_vert);
    sb_addnl(&sb_frag);
    
    struct color_vert_info cvi;
    cvi.model = model;
    cvi.mesh_index = mesh_index;
    cvi.primitive_index = primitive_index;
    cvi.config = conf;
    cvi.am = &phi.am;
    cvi.mm = &phi.mm;
    color_vert(&sb_vert, &sb_frag, &cvi);
    
    color_frag(&sb_vert, &sb_frag, model,
               mesh_index, primitive_index, conf);
    
    // @Optimise Lots of the below code uses string_format. This would be a lot
    // faster with a string builder. It is a little more fiddly, so using this
    // for now. (Tbf, I doubt there is much overhead. But still, not a
    // release type vibe.)
    
    char buf[32];
    const uint32 *v_spv,*f_spv;
    uint v_len,f_len;
    shaderc_compilation_result_t vr,fr;
    
    // vert
    string_format(buf,"shaders/auto_%u.vert",array_len(dir->hashes));
    file_write_char(buf, sb_vert.used, sb_vert.data);
    
    vr = shaderc_compile_into_spv(cl->cl,sb_vert.data,sb_vert.used,
                                  shaderc_glsl_vertex_shader,buf,"main",cl->opt);
    
    if (shaderc_result_get_num_errors(vr) || shaderc_result_get_num_warnings(vr)) {
        /*const char *em = shaderc_result_get_error_message(vr);
        print_count_chars(em,strlen(em));*/
        print_count_chars(sb_vert.data, sb_vert.used);
        log_print_error("Failed to compile vertex shader %s",buf);
        return Max_u32;
    } else {
        v_spv = (const uint32*)shaderc_result_get_bytes(vr);
        v_len = shaderc_result_get_length(vr);
        string_format(buf,"shaders/auto_%u.vert.spv",array_len(dir->hashes));
        file_write_bin(buf,v_len,v_spv);
    }
    
    // frag
    string_format(buf,"shaders/auto_%u.frag",array_len(dir->hashes));
    file_write_char(buf, sb_frag.used, sb_frag.data);
    
    fr = shaderc_compile_into_spv(cl->cl,sb_frag.data,sb_frag.used,
                                  shaderc_glsl_fragment_shader,buf,"main",cl->opt);
    
    if (shaderc_result_get_num_errors(fr) || shaderc_result_get_num_warnings(fr)) {
        /*(const char *em = shaderc_result_get_error_message(fr);
        print_count_chars(em,strlen(em));*/
        log_print_error("Failed to compile fragment shader %s",buf);
        return Max_u32;
    } else {
        f_spv = (const uint32*)shaderc_result_get_bytes(fr);
        f_len = shaderc_result_get_length(fr);
        string_format(buf,"shaders/auto_%u.frag.spv",array_len(dir->hashes));
        file_write_bin(buf,f_len,f_spv);
    }
    shaderc_result_release(vr);
    shaderc_result_release(fr);
    
    struct shader_set set;
    set.flags = conf->flags;
    VkShaderModuleCreateInfo mod_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    
    mod_info.codeSize = v_len;
    mod_info.pCode = v_spv;
    VkResult check = vkCreateShaderModule(dir->gpu->device,&mod_info,GAC,&set.vert);
    DEBUG_VK_OBJ_CREATION(vkCreateShaderModule,check);
    
    mod_info.codeSize = f_len;
    mod_info.pCode = f_spv;
    check = vkCreateShaderModule(dir->gpu->device,&mod_info,GAC,&set.frag);
    DEBUG_VK_OBJ_CREATION(vkCreateShaderModule,check);
    
    return dir_add(dir,ph,&set);
}

uint find_or_generate_primitive_shader_set(struct shader_dir *dir,
                                           struct shader_config *conf, gltf *model, uint mesh_i, uint prim_i)
{
    struct shader_cl cl;
    cl.cl = shaderc_compiler_initialize();
    cl.opt = shaderc_compile_options_initialize();
    shaderc_compile_options_set_optimization_level(cl.opt,SHDR_OPTIMISATION);
    shaderc_compile_options_set_target_env(cl.opt, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_3);
    
    uint ret = find_or_generate_shader_set(dir,&cl,conf,model,mesh_i,prim_i);
    
    shaderc_compile_options_release(cl.opt);
    shaderc_compiler_release(cl.cl);
    
    return ret;
}

void find_or_generate_model_shader_sets(struct shader_dir *dir,
                                        struct shader_config *conf, gltf *model)
{
    struct shader_cl cl;
    cl.cl = shaderc_compiler_initialize();
    cl.opt = shaderc_compile_options_initialize();
    shaderc_compile_options_set_optimization_level(cl.opt,SHDR_OPTIMISATION);
    shaderc_compile_options_set_target_env(cl.opt, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_3);
    
    uint i,j;
    for(i=0;i<model->mesh_count;++i)
        for(j=0;j<model->meshes[i].primitive_count;++j) {
        model->meshes[i].primitives[j].shader_i =
            find_or_generate_shader_set(dir,&cl,conf,model,i,j);
    }
    model->shader_conf_flags = conf->flags;
    
    shaderc_compile_options_release(cl.opt);
    shaderc_compiler_release(cl.cl);
}

#define DIR_INFO_FILE "shaders/dir_info.bin"
#define MAX_SPV_SIZE 15 * 1024

struct shader_dir load_shader_dir(struct gpu *gpu, allocator *alloc)
{
    struct file f = file_read_bin_all(DIR_INFO_FILE,alloc);
    
    struct shader_dir dir;
    dir.gpu = gpu;
    dir.alloc = alloc;
    if (RECREATE_SHADER_DIR || !f.size) {
        dir.hashes = new_array(32,*dir.hashes,alloc); // @Todo These are too small and should not be dynamic
        dir.sets = new_array(32,*dir.sets,alloc);
        print_auto_shader("Loading empty shader dir from %s",DIR_INFO_FILE);
    } else {
        dir.hashes = load_array(DIR_INFO_FILE,alloc);
        dir.sets = new_array(array_cap(dir.hashes),*dir.sets,alloc);
        print_auto_shader("Loading shader dir from %s, shader count %u",
                          DIR_INFO_FILE,array_len(dir.hashes));
    }
    
    void *tmp_mem = allocate(alloc,MAX_SPV_SIZE*2);
    allocator tmp = new_linear_allocator(MAX_SPV_SIZE*2,tmp_mem);
    
    VkResult check;
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    
    const int buf_len = 32;
    char buf[buf_len];
    struct shader_set set;
    for(uint i=0;i<array_len(dir.hashes);++i) {
        string_format(buf,"shaders/auto_%u.vert.spv",i);
        f = file_read_bin_all(buf,&tmp);
        
        info.codeSize = f.size;
        info.pCode = (const uint32*)f.data;
        check = vk_create_shader_module(gpu->device,&info,GAC,&set.vert);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule,check);
        
        string_format(buf,"shaders/auto_%u.frag.spv",i);
        f = file_read_bin_all(buf,&tmp);
        
        info.codeSize = f.size;
        info.pCode = (const uint32*)f.data;
        check = vk_create_shader_module(gpu->device,&info,GAC,&set.frag);
        DEBUG_VK_OBJ_CREATION(vkCreateShaderModule,check);
        
        array_add(dir.sets,set);
        allocator_reset_linear(&tmp);
    }
    deallocate(alloc,tmp_mem);
    
    return dir;
}

void store_shader_dir(struct shader_dir *dir)
{
    print_auto_shader("Storing shader dir to %s",DIR_INFO_FILE);
    array_set_len(dir->hashes,0);
    store_array(DIR_INFO_FILE,dir->hashes);
    free_array(dir->hashes);
    free_array(dir->sets);
}
