#include "gltf.h"
#include "json.h"
#include "log.h"
#include "shader.h"
#include "timer.h"
#include "defs.h"

#define PROCESSED_GLTF_FILE_EXTENSION ".sol"

void store_gltf(gltf *model, const char *file_name, allocator *alloc)
{
    assert(strlen(file_name) < 150);

    char buf[128];
    uint len = strlen(file_name);
    memcpy(buf, file_name, len);
    memcpy(buf + len, PROCESSED_GLTF_FILE_EXTENSION, 5);

    char *data = allocate(alloc, sizeof(*model) + model->meta.size);
    memcpy(data, model, sizeof(*model));
    memcpy(data + sizeof(*model), model->meta.data, model->meta.size);

    file_write_bin(buf, sizeof(*model) + model->meta.size, data);
}

void load_gltf(const char *file_name, struct shader_dir *dir, struct shader_config *conf,
        allocator *temp, allocator *persistent, gltf *g)
{
    char buf[128];
    uint len = strlen(file_name);
    memcpy(buf, file_name, len);
    memcpy(buf + len, PROCESSED_GLTF_FILE_EXTENSION, 5);

    // @Tidy I dont like statics in functions but I cannot think of a
    // significantly cleaner way other than passing in some arg.
    static int gltf_source_changed = -1;
    static bool gltf_source_changed_msg = false;

    if (gltf_source_changed == -1)
        gltf_source_changed = ts_after(file_last_modified("gltf.c"), file_last_modified("exe"));

    if (gltf_source_changed && !gltf_source_changed_msg) {
        println("gltf.c has changed, parsing files again...");
        gltf_source_changed_msg = true;
    }

    if (gltf_source_changed || !file_exists(buf)) {
        if (!file_exists(buf))
            println("loading model file %s for the first time, parsing gltf", file_name);
        parse_gltf(file_name, dir, conf, temp, persistent, g);
        store_gltf(g, file_name, temp); // to create file_name.gltf.sol
        return;
    }

    // @Optimise I do not love this, as it is uses a potentially unnecessary copy,
    // but I do not want to store pointers to gltf structures, but the structures
    // themselves, and I do not want fragmented memory.
    struct file file = file_read_bin_all(buf, temp);
    memcpy(g, file.data, sizeof(*g));
    g->meta.data = allocate(persistent, g->meta.size);
    memcpy(g->meta.data, file.data + sizeof(*g), g->meta.size);

    char *b = ((gltf*)file.data)->meta.data;
    char *d = g->meta.data;

    g->accessors = ptrinc_or_null(g->accessors, b, d);
    g->animations = ptrinc_or_null(g->animations, b, d);
    g->buffers = ptrinc_or_null(g->buffers, b, d);
    g->buffer_views = ptrinc_or_null(g->buffer_views, b, d);
    g->cameras = ptrinc_or_null(g->cameras, b, d);
    g->images = ptrinc_or_null(g->images, b, d);
    g->materials = ptrinc_or_null(g->materials, b, d);
    g->meshes = ptrinc_or_null(g->meshes, b, d);
    g->nodes = ptrinc_or_null(g->nodes, b, d);
    g->samplers = ptrinc_or_null(g->samplers, b, d);
    g->scenes = ptrinc_or_null(g->scenes, b, d);
    g->skins = ptrinc_or_null(g->skins, b, d);
    g->textures = ptrinc_or_null(g->textures, b, d);
    g->dir.cstr = ptrinc_or_null(g->dir.cstr, b, d);

    for(uint i=0; i < g->animation_count; ++i) {
        g->animations[i].targets  = ptrinc_or_null(g->animations[i].targets, b, d);
        g->animations[i].samplers = ptrinc_or_null(g->animations[i].samplers, b, d);
    }
    for(uint i=0; i < g->buffer_count; ++i) {
        g->buffers[i].uri.cstr  = ptrinc_or_null(g->buffers[i].uri.cstr, b, d);
    }
    for(uint i=0; i < g->image_count; ++i) {
        g->images[i].uri.cstr  = ptrinc_or_null(g->images[i].uri.cstr, b, d);
    }
    for(uint i=0; i < g->mesh_count; ++i) {
        log_print_error_if(g->meshes[i].weight_count > GLTF_MORPH_WEIGHT_COUNT,
                "meshes[%u].weight_count exceeds GLTF_MORPH_WEIGHT_COUNT");

        g->meshes[i].primitives = ptrinc_or_null(g->meshes[i].primitives, b, d);
        for(uint j=0; j < g->meshes[i].primitive_count; ++j) {
            g->meshes[i].primitives[j].attributes =
                ptrinc_or_null(g->meshes[i].primitives[j].attributes, b, d);
            g->meshes[i].primitives[j].morph_targets =
                ptrinc_or_null(g->meshes[i].primitives[j].morph_targets, b, d);
            for(uint k=0; k < g->meshes[i].primitives[j].target_count; ++k)
                g->meshes[i].primitives[j].morph_targets[k].attributes =
                    ptrinc_or_null(g->meshes[i].primitives[j].morph_targets[k].attributes, b, d);
        }
        g->meshes[i].weights = ptrinc_or_null(g->meshes[i].weights, b, d);
    }
    for(uint i=0; i < g->node_count; ++i) {
        g->nodes[i].children = ptrinc_or_null(g->nodes[i].children, b, d);
        g->nodes[i].weights  = ptrinc_or_null(g->nodes[i].weights, b, d);
    }
    for(uint i=0; i < g->scene_count; ++i) {
        g->scenes[i].name.cstr = ptrinc_or_null(g->scenes[i].name.cstr, b, d);
        g->scenes[i].nodes = ptrinc_or_null(g->scenes[i].nodes, b, d);
    }
    for(uint i=0; i < g->skin_count; ++i) {
        g->skins[i].joints = ptrinc_or_null(g->skins[i].joints, b, d);
    }
}

#define GLTF_PROPERTY_COUNT 13
#define GLTF_MAX_URI_LEN 64

#if GLTF_MAX_URI_LEN % ALLOCATOR_ALIGNMENT != 0
    #error "allocating this size will cause problems..."
#endif

typedef enum {
    GLTF_PROPERTY_INDEX_ACCESSORS    = 0,
    GLTF_PROPERTY_INDEX_ANIMATIONS   = 1,
    GLTF_PROPERTY_INDEX_BUFFERS      = 2,
    GLTF_PROPERTY_INDEX_BUFFER_VIEWS = 3,
    GLTF_PROPERTY_INDEX_CAMERAS      = 4,
    GLTF_PROPERTY_INDEX_IMAGES       = 5,
    GLTF_PROPERTY_INDEX_MATERIALS    = 6,
    GLTF_PROPERTY_INDEX_MESHES       = 7,
    GLTF_PROPERTY_INDEX_NODES        = 8,
    GLTF_PROPERTY_INDEX_SAMPLERS     = 9,
    GLTF_PROPERTY_INDEX_SCENES       = 10,
    GLTF_PROPERTY_INDEX_SKINS        = 11,
    GLTF_PROPERTY_INDEX_TEXTURES     = 12,
} gltf_property_index;

struct gltf_required_size {
    uint extra_mesh_attrs;
    size_t size;
    uint *anim_target_counts;
};

struct gltf_extra_attrs {
    uint mesh;
    uint prim;
    bool norm;
    bool tang;
};

struct gltf_index_data {
    uint count;
    uint offset;
    uint buffer;
    bool type_u16;
};

struct gltf_attr_data {
    uint count;
    uint stride;
    uint offset;
    uint buffer;
};

static struct gltf_index_data gltf_index_data(gltf *g, uint mesh, uint prim)
{
    log_print_error_if(g->meshes[mesh].primitives[prim].indices == Max_u32,
            "trying to access indices data, but this primitive does not declare indices");

    gltf_accessor *pa = &g->accessors[g->meshes[mesh].primitives[prim].indices];
    gltf_buffer_view *pb = &g->buffer_views[pa->buffer_view];

    struct gltf_index_data r;
    uint index_stride = pb->byte_stride ? pb->byte_stride : pa->byte_stride; // assert purposes only.
    r.count    = pa->count;
    r.offset   = pa->byte_offset + pb->byte_offset;
    r.buffer   = pb->buffer;
    r.type_u16 = pa->flags & GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT;
    assert((index_stride == 2 && r.type_u16) || (index_stride == 4 && !r.type_u16));
    return r;
}

static struct gltf_attr_data gltf_attr_data(gltf *g, uint mesh, uint prim, gltf_mesh_primitive_attribute_type type)
{
    log_print_error_if(type == GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD &&
            g->meshes[mesh].primitives[prim].attributes[type].type !=
            GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD,
            "trying to access texcoord attr data, but this primitive does not declare texcoords");

    gltf_accessor *pa = &g->accessors[g->meshes[mesh].primitives[prim].attributes[type].accessor];
    gltf_buffer_view *pb = &g->buffer_views[pa->buffer_view];

    struct gltf_attr_data r;
    r.count  = pa->count;
    r.stride = pb->byte_stride ? pb->byte_stride : pa->byte_stride;
    r.offset = pa->byte_offset + pb->byte_offset;
    r.buffer = pb->buffer;
    return r;
}

static struct gltf_required_size gltf_required_size(json *j, allocator *temp, uint *indices);
static size_t gltf_required_size_accessors(json *j, uint *index);
static size_t gltf_required_size_animations(json *j, uint *index, allocator *temp, uint **anim_target_counts);
static size_t gltf_required_size_buffers(json *j, uint *index);
static size_t gltf_required_size_buffer_views(json *j, uint *index);
static size_t gltf_required_size_cameras(json *j, uint *index);
static size_t gltf_required_size_images(json *j, uint *index);
static size_t gltf_required_size_materials(json *j, uint *index);
static size_t gltf_required_size_meshes(json *j, uint *index, struct gltf_required_size *extra_sz);
static size_t gltf_required_size_nodes(json *j, uint *index);
static size_t gltf_required_size_samplers(json *j, uint *index);
static size_t gltf_required_size_scenes(json *j, uint *index);
static size_t gltf_required_size_skins(json *j, uint *index);
static size_t gltf_required_size_textures(json *j, uint *index);
static void gltf_parse_accessors(uint index, json *j, uint extra_attrs, allocator *alloc, gltf *g);
static void gltf_parse_animations(uint index, json *j, allocator *alloc, uint *anim_target_counts, gltf *g);
static uint gltf_parse_animation_targets(uint count, json_object *channel_objs, gltf_animation_target *targets);
static void gltf_parse_animation_samplers(uint count, json_object *sampler_objs, gltf_animation_sampler *samplers);
static void gltf_parse_buffers(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_buffer_views(uint index, json *j, bool extra_attrs, allocator *alloc, gltf *g);
static void gltf_parse_cameras(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_camera_parse_orthographic(json_object *json_orthographic, gltf_camera_orthographic *orthographic);
static void gltf_camera_parse_perspective(json_object *json_perspective, gltf_camera_perspective *perspective);
static void gltf_parse_images(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_materials(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_meshes(uint index, json *j, struct gltf_extra_attrs *extra_attrs, uint *extra_attr_count, allocator *alloc, gltf *g);
static uint gltf_mesh_parse_primitives(json_array *j_prim_array, struct gltf_extra_attrs *extra_attrs, allocator *alloc, gltf_mesh *mesh);
static uint gltf_mesh_parse_primitive_attributes(json_object *j_attribs, struct gltf_extra_attrs *extra_attrs, gltf_mesh_primitive_attribute *attribs);
static void gltf_parse_nodes(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_samplers(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_scenes(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_skins(uint index, json *j, allocator *alloc, gltf *g);
static void gltf_parse_textures(uint index, json *j, allocator *alloc, gltf *g);

void parse_gltf(const char *file_name, struct shader_dir *dir, struct shader_config *conf,
        allocator *temp, allocator *persistent, gltf *g)
{
    struct file f = file_read_char_all(file_name, temp);
    struct allocation json_allocation;
    json j = parse_json(&f, temp, &json_allocation);

    uint indices[GLTF_PROPERTY_COUNT];
    struct gltf_required_size req_size = gltf_required_size(&j, temp, indices);

    // Ik this may allocate more than necessary because each gltf_extra_attrs
    // stores both normal and tangent info. I allocate +1 still to ensure that
    // I can do an out of bounds write later, even if there is no missing
    // tangent/normal overlap.
    struct gltf_extra_attrs *extra_attrs = sallocate(temp, *extra_attrs, req_size.extra_mesh_attrs + 1);
    memset(extra_attrs, 0, sizeof(*extra_attrs) * req_size.extra_mesh_attrs);

    req_size.size = align(req_size.size, getpagesize());
    g->meta.size = req_size.size;
    g->meta.data = allocate(persistent, g->meta.size);

    allocator gltf_alloc = new_linear_allocator(g->meta.size + strlen(file_name), g->meta.data);

    g->dir.cstr = allocate(&gltf_alloc, strlen(file_name));
    g->dir.len = file_dir_name(file_name, (char*)g->dir.cstr);

    // counts the number of primitives which require extra attributes, not the
    // total number of new attributes.
    uint eac = 0;

    gltf_parse_accessors(indices[GLTF_PROPERTY_INDEX_ACCESSORS], &j, req_size.extra_mesh_attrs, &gltf_alloc, g);
    gltf_parse_animations(indices[GLTF_PROPERTY_INDEX_ANIMATIONS], &j, &gltf_alloc, req_size.anim_target_counts, g);
    gltf_parse_buffers(indices[GLTF_PROPERTY_INDEX_BUFFERS], &j, &gltf_alloc, g);
    gltf_parse_buffer_views(indices[GLTF_PROPERTY_INDEX_BUFFER_VIEWS], &j, req_size.extra_mesh_attrs > 0, &gltf_alloc, g);
    gltf_parse_cameras(indices[GLTF_PROPERTY_INDEX_CAMERAS], &j, &gltf_alloc, g);
    gltf_parse_images(indices[GLTF_PROPERTY_INDEX_IMAGES], &j, &gltf_alloc, g);
    gltf_parse_materials(indices[GLTF_PROPERTY_INDEX_MATERIALS], &j, &gltf_alloc, g);
    gltf_parse_meshes(indices[GLTF_PROPERTY_INDEX_MESHES], &j, extra_attrs, &eac, &gltf_alloc, g);
    gltf_parse_samplers(indices[GLTF_PROPERTY_INDEX_SAMPLERS], &j, &gltf_alloc, g);
    gltf_parse_scenes(indices[GLTF_PROPERTY_INDEX_SCENES], &j, &gltf_alloc, g);
    gltf_parse_skins(indices[GLTF_PROPERTY_INDEX_SKINS], &j, &gltf_alloc, g);
    gltf_parse_textures(indices[GLTF_PROPERTY_INDEX_TEXTURES], &j, &gltf_alloc, g);

    // must happen after skins and meshes, as mesh.joint_count is filled in here.
    gltf_parse_nodes(indices[GLTF_PROPERTY_INDEX_NODES], &j, &gltf_alloc, g);

#if !TEST // test.gltf uses a bogus file which would not make sense to run this on.
    assert(g->mesh_count < 32);
    uint instance_counts[32] = {};
    uint64 skin_mask;
    for(uint i=0; i < g->scene_count; ++i)
        for(uint j=0; j < g->scenes[i].node_count; ++j)
            gltf_count_mesh_instances(g->nodes, g->scenes[i].nodes[j],
                                      instance_counts, &skin_mask);
    for(uint i=0; i < g->mesh_count; ++i) {
        g->meshes[i].max_instance_count = instance_counts[i];
        log_print_error_if(instance_counts[i] > SHADER_MAX_MESH_INSTANCE_COUNT,
                "mesh max instance count too large for model %s, mesh %u", file_name, i);
    }
#endif

    if (!eac)
        return;

    uint bv = g->buffer_view_count;
    uint ac = g->accessor_count;
    g->buffer_view_count++;
    g->accessor_count += req_size.extra_mesh_attrs; // counts both missing tangent and normals
    {
        // @Optimise @Test I might want to find a way to store the data closer
        // to the other primitive data... Since individual elements are already
        // technically sparse (unless the data is being interleaved), and the
        // GPU does large gathers anyway, idk what kind of performance impact
        // this would really have.
        g->buffer_views[bv].flags = GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT;
        g->buffer_views[bv].buffer = 0; // @Note Idk if there is a better idea.
        g->buffer_views[bv].byte_stride = 0;
        g->buffer_views[bv].byte_offset =
            align(g->buffers[g->buffer_views[bv].buffer].byte_length, 16);
    }

    uint64 alloc_pos = allocator_used(temp);
    char *bufs[8]; assert(g->buffer_count <= carrlen(bufs));
    vector *tn_data;
    {
        uint64 sz = 0;
        for(uint i=0; i < g->buffer_count; ++i)
            sz += g->buffers[i].byte_length;

        sz = align(sz, 16);
        uint tn_ofs = sz;

        for(uint i=0; i < eac; ++i)
            sz += sizeof(*tn_data) * g->accessors[g->meshes[extra_attrs[i].mesh]
                .primitives[extra_attrs[i].prim]
                .attributes[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION].accessor].count *
                (extra_attrs[i].tang + extra_attrs[i].norm);

        bufs[0] = allocate(temp, sz);
        for(uint i=1; i < g->buffer_count; ++i)
            bufs[i] = bufs[i-1] + g->buffers[i-1].byte_length;

        tn_data = (vector*)(bufs[0] + tn_ofs);

        for(uint i=0; i < g->buffer_count; ++i)
            gltf_read_buffer(g, i, bufs[i]);
    }

    uint bc = 0;
    for(uint i=0; i < eac; ++i) {
        uint m = extra_attrs[i].mesh;
        uint p = extra_attrs[i].prim;

        // @Unimplemented @Todo
        log_print_error_if(g->meshes[m].primitives[p].indices == Max_u32,
                "@Unimplemented I do not have non-indexed tang/norm attribute generation functions yet lol");

        struct gltf_index_data index = gltf_index_data(g, m, p);
        struct gltf_attr_data vert = gltf_attr_data(g, m, p, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION);

        if (extra_attrs[i].norm) {
            g->accessors[ac].flags = GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT|GLTF_ACCESSOR_TYPE_VEC3_BIT;
            g->accessors[ac].vkformat = VK_FORMAT_R32G32B32_SFLOAT;
            g->accessors[ac].byte_stride = 16;
            g->accessors[ac].count = g->accessors[g->meshes[m].primitives[p]
                .attributes[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION].accessor].count;
            g->accessors[ac].buffer_view = bv;
            g->accessors[ac].byte_offset = bc;

            vector *normals = tn_data + (bc>>4);

            if (index.type_u16)
                calc_vertex_normals16(index.count, (uint16*)(bufs[index.buffer] + index.offset),
                                      vert.count, vert.stride, (float*)(bufs[vert.buffer] + vert.offset), normals);
            else
                calc_vertex_normals(index.count, (uint*)(bufs[index.buffer] + index.offset),
                                    vert.count, vert.stride, (float*)(bufs[vert.buffer] + vert.offset), normals);

            g->meshes[m].primitives[p].attribute_count++;
            g->meshes[m].primitives[p].attributes[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL].accessor = ac;
            ac++;
            bc += sizeof(*normals) * vert.count;
        }
        if (extra_attrs[i].tang) {
            struct gltf_attr_data norm = gltf_attr_data(g, m, p, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL);
            struct gltf_attr_data texcoord = gltf_attr_data(g, m, p, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD);

            g->accessors[ac].flags = GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT|GLTF_ACCESSOR_TYPE_VEC4_BIT;
            g->accessors[ac].vkformat = VK_FORMAT_R32G32B32A32_SFLOAT;
            g->accessors[ac].byte_stride = 16;
            g->accessors[ac].count = g->accessors[g->meshes[m].primitives[p]
                .attributes[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION].accessor].count;
            g->accessors[ac].buffer_view = bv;
            g->accessors[ac].byte_offset = bc;

            vector *tangents = tn_data + (bc>>4);

            if (index.type_u16)
                calc_vertex_tangents16(index.count, (uint16*)(bufs[index.buffer] + index.offset),
                                      vert.count, vert.stride, (float*)(bufs[vert.buffer] + vert.offset),
                                      norm.stride == 4, norm.stride, (float*)(bufs[norm.buffer] + norm.offset),
                                      texcoord.stride, (float*)(bufs[texcoord.buffer] + texcoord.offset),
                                      temp, tangents);
            else
                calc_vertex_tangents(index.count, (uint*)(bufs[index.buffer] + index.offset),
                                      vert.count, vert.stride, (float*)(bufs[vert.buffer] + vert.offset),
                                      norm.stride == 4, norm.stride, (float*)(bufs[norm.buffer] + norm.offset),
                                      texcoord.stride, (float*)(bufs[texcoord.buffer] + texcoord.offset),
                                      temp, tangents);

            g->meshes[m].primitives[p].attribute_count++;
            g->meshes[m].primitives[p].attributes[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT].accessor = ac;
            ac++;
            bc += sizeof(*tangents) * vert.count;
        }
    }
    g->buffer_views[bv].byte_length = bc;
    g->buffers[g->buffer_views[bv].buffer].byte_length = g->buffer_views[bv].byte_offset +
                                                         g->buffer_views[bv].byte_length;

    int fd = gltf_open_buffer_w(g, g->buffer_views[bv].buffer);
    file_resize(fd, g->buffers[g->buffer_views[bv].buffer].byte_length);
    file_write(fd, g->buffer_views[bv].byte_offset, g->buffer_views[bv].byte_length, tn_data);
    file_close(fd);

    allocator_reset_linear_to(temp, alloc_pos);
}

static struct gltf_required_size gltf_required_size(json *j, allocator *temp, uint *indices)
{
    struct gltf_required_size ret = {};

    ret.size += gltf_required_size_accessors(j, &indices[GLTF_PROPERTY_INDEX_ACCESSORS]);
    ret.size += gltf_required_size_animations(j, &indices[GLTF_PROPERTY_INDEX_ANIMATIONS], temp, &ret.anim_target_counts);
    ret.size += gltf_required_size_buffers(j, &indices[GLTF_PROPERTY_INDEX_BUFFERS]);
    ret.size += gltf_required_size_buffer_views(j, &indices[GLTF_PROPERTY_INDEX_BUFFER_VIEWS]);
    ret.size += gltf_required_size_cameras(j, &indices[GLTF_PROPERTY_INDEX_CAMERAS]);
    ret.size += gltf_required_size_images(j, &indices[GLTF_PROPERTY_INDEX_IMAGES]);
    ret.size += gltf_required_size_materials(j, &indices[GLTF_PROPERTY_INDEX_MATERIALS]);
    ret.size += gltf_required_size_meshes(j, &indices[GLTF_PROPERTY_INDEX_MESHES], &ret);
    ret.size += gltf_required_size_nodes(j, &indices[GLTF_PROPERTY_INDEX_NODES]);
    ret.size += gltf_required_size_samplers(j, &indices[GLTF_PROPERTY_INDEX_SAMPLERS]);
    ret.size += gltf_required_size_scenes(j, &indices[GLTF_PROPERTY_INDEX_SCENES]);
    ret.size += gltf_required_size_skins(j, &indices[GLTF_PROPERTY_INDEX_SKINS]);
    ret.size += gltf_required_size_textures(j, &indices[GLTF_PROPERTY_INDEX_TEXTURES]);

    ret.size += sizeof(gltf_accessor)                 *  ret.extra_mesh_attrs +
                sizeof(gltf_mesh_primitive_attribute) *  ret.extra_mesh_attrs +
                sizeof(gltf_buffer_view)              * (ret.extra_mesh_attrs > 0);

    return ret;
}

static size_t gltf_required_size_accessors(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "accessors");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_accessor), ALLOCATOR_ALIGNMENT);
}

static size_t gltf_required_size_animations(json *j, uint *index, allocator *temp, uint **anim_target_counts)
{
    uint tmp = json_find_key(&j->obj, "animations");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    json_object *animations = j->obj.values[ki].arr.objs;
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);

    uint *target_counts = sallocate(temp, *target_counts, cnt);

    *index = tmp;
    uint target_cnt = 0;
    uint sampler_cnt = 0;
    uint channel_cnt;
    for(uint i = 0; i < cnt; ++i) {
        ki = json_find_key(&animations[i], "channels");
        log_print_error_if(ki == Max_u32, "animations.channels must be defined");
        channel_cnt = animations[i].values[ki].arr.len;

        uint64 node_mask[GLTF_U64_NODE_MASK] = {};
        uint64 one = 1;
        for(uint j=0; j < channel_cnt; ++j) {
            tmp = json_find_key(&animations[i].values[ki].arr.objs[j], "target");
            log_print_error_if(tmp == Max_u32, "animation.channel.target must be defined");
            uint node = json_find_key(&animations[i].values[ki].arr.objs[j].values[tmp].obj, "node");
            log_print_error_if(tmp == Max_u32, "animation.channel.target.node must be defined, this parser does not support this extension");

            node = (uint)animations[i].values[ki].arr.objs[j].values[tmp].obj.values[node].num;
            node_mask[node>>6] |= one << (node & 63);
        }
        for(uint j=0; j < c_array_len(node_mask); ++j)
            target_cnt += popcnt(node_mask[j]);
        target_counts[i] = target_cnt;

        ki = json_find_key(&animations[i], "samplers");
        log_print_error_if(ki == Max_u32, "animations.samplers must be defined");
        sampler_cnt += animations[i].values[ki].arr.len;
    }
    *anim_target_counts = target_counts;
    size_t ret = 0;
    ret += cnt * align(sizeof(gltf_animation), ALLOCATOR_ALIGNMENT);
    ret += target_cnt * align(sizeof(gltf_animation_target), ALLOCATOR_ALIGNMENT);
    ret += sampler_cnt * align(sizeof(gltf_animation_sampler), ALLOCATOR_ALIGNMENT);
    return ret;
}

static size_t gltf_required_size_buffers(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "buffers");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_buffer), ALLOCATOR_ALIGNMENT) + GLTF_MAX_URI_LEN * cnt;
}

static size_t gltf_required_size_buffer_views(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "bufferViews");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_buffer_view), ALLOCATOR_ALIGNMENT);
}

static size_t gltf_required_size_cameras(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "cameras");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_camera), ALLOCATOR_ALIGNMENT);
}

static size_t gltf_required_size_images(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "images");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_image), ALLOCATOR_ALIGNMENT) + cnt * GLTF_MAX_URI_LEN;
}

static size_t gltf_required_size_materials(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "materials");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_material), ALLOCATOR_ALIGNMENT);
}

static size_t gltf_required_size_meshes(json *j, uint *index, struct gltf_required_size *extra_info)
{
    uint tmp = json_find_key(&j->obj, "meshes");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;

    json_object *j_meshes = j->obj.values[ki].arr.objs;
    json_object *j_prims;
    json_object *j_targets;
    uint weight_cnt;
    uint prim_cnt = 0;
    uint attrib_cnt = 0;
    uint target_cnt = 0;
    uint weights_size = 0;
    uint total_prim_cnt = 0;
    uint total_attrib_cnt = 0;
    uint total_target_cnt = 0;

    uint i0,i1,i2;
    for(i0=0;i0<cnt;++i0) {
        ki = json_find_key(&j_meshes[i0],"primitives");
        log_print_error_if(ki == Max_u32,"mesh.primitives must be defined");
        j_prims = j_meshes[i0].values[ki].arr.objs;
        prim_cnt = j_meshes[i0].values[ki].arr.len;
        total_prim_cnt += prim_cnt;
        for(i1=0;i1<prim_cnt;++i1) {
            ki = json_find_key(&j_prims[i1],"attributes");
            log_print_error_if(ki == Max_u32,"mesh.primitives.attributes must be defined");
            attrib_cnt = j_prims[i1].values[ki].obj.key_count;
            total_attrib_cnt += attrib_cnt;

            extra_info->extra_mesh_attrs += json_find_key(&j_prims[i1].values[ki].obj,"TANGENT") == Max_u32;
            extra_info->extra_mesh_attrs += json_find_key(&j_prims[i1].values[ki].obj,"NORMAL") == Max_u32;

            tmp = json_find_key(&j_prims[i1],"targets");
            ki = tmp & max32_if_true(tmp != Max_u32);
            j_targets = j_prims[i1].values[ki].arr.objs;
            target_cnt = j_prims[i1].values[ki].arr.len & max32_if_true(tmp != Max_u32);
            total_target_cnt += target_cnt;
            for(i2=0;i2<target_cnt;++i2)
                total_attrib_cnt += j_targets[i2].key_count;
        }

        tmp = json_find_key(&j_meshes[i0],"weights");
        ki = tmp & max32_if_true(tmp != Max_u32);
        weight_cnt = j_meshes[i0].values[ki].arr.len & max32_if_true(tmp != Max_u32);
        weights_size += alloc_align(sizeof(float) * weight_cnt); // I do not want to align each float.
    }

    total_attrib_cnt += extra_info->extra_mesh_attrs;

    return           cnt * alloc_align_type(gltf_mesh) +
          total_prim_cnt * alloc_align_type(gltf_mesh_primitive) +
        total_attrib_cnt * alloc_align_type(gltf_mesh_primitive_attribute) +
        total_target_cnt * alloc_align_type(gltf_mesh_primitive_morph_target) +
        weights_size;
}

static size_t gltf_required_size_nodes(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "nodes");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;

    json_object *nodes = j->obj.values[ki].arr.objs;
    uint csz = 0;
    uint wsz = 0;
    uint i;
    for(i = 0; i < cnt; ++i) {
        ki = json_find_key(&nodes[i], "children");
        csz += ki != Max_u32 ? alloc_align(sizeof(uint) * nodes[i].values[ki].arr.len) : 0;
        ki = json_find_key(&nodes[i], "weights");
        wsz += ki != Max_u32 ? alloc_align(sizeof(uint) * nodes[i].values[ki].arr.len) : 0;
    }
    return cnt * align(sizeof(gltf_node), ALLOCATOR_ALIGNMENT) + csz + wsz;
}

static size_t gltf_required_size_samplers(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "samplers");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return cnt * align(sizeof(gltf_sampler), ALLOCATOR_ALIGNMENT);
}

static size_t gltf_required_size_scenes(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "scenes");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;

    json_object *scenes = j->obj.values[ki].arr.objs;
    uint sz = 0;
    uint i;
    for(i=0;i<cnt;++i) {
        ki = json_find_key(&scenes[i], "nodes");
        tmp = ki != Max_u32 ? scenes[i].values[ki].arr.len : 0;
        sz += alloc_align(sizeof(uint) * tmp);

        ki = json_find_key(&scenes[i], "name");
        sz += GLTF_MAX_URI_LEN & max32_if_true(ki != Max_u32);
    }
    return cnt * align(sizeof(gltf_sampler), ALLOCATOR_ALIGNMENT) + sz;
}

static size_t gltf_required_size_skins(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "skins");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;

    json_object *skins = j->obj.values[ki].arr.objs;
    uint sz = 0;
    uint i;
    for(i=0;i<cnt;++i) {
        ki = json_find_key(&skins[i], "joints");
        log_print_error_if(ki == Max_u32, "skin.joints must be defined");
        sz += alloc_align(sizeof(uint) * skins[i].values[ki].arr.len);
    }
    return cnt * sizeof(gltf_skin) + sz;
}
static size_t gltf_required_size_textures(json *j, uint *index)
{
    uint tmp = json_find_key(&j->obj, "textures");
    uint ki = tmp & max64_if_true(tmp != Max_u32);
    uint cnt = j->obj.values[ki].arr.len & max64_if_true(tmp != Max_u32);
    *index = tmp;
    return alloc_align(cnt * sizeof(gltf_texture));
}

typedef enum {
    GLTF_ACCESSOR_COMPONENT_TYPE_BYTE           = 5120,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE  = 5121,
    GLTF_ACCESSOR_COMPONENT_TYPE_SHORT          = 5122,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
    GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT   = 5125,
    GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT          = 5126,
} gltf_accessor_component_type;

static inline gltf_accessor_flag_bits gltf_accessor_component_type_to_flags(double d) {
    int type = (int)d;
    switch(type) {
    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE:
        return GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE:
        return GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT:
        return GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT:
        return GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT:
        return GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT:
        return GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT;
    default:
        log_print_error("Invalid accessor.componentType");
        return 0x0;
    }
}

#define GLTF_ACCESSOR_TYPE_LEN_SCALAR 6
#define GLTF_ACCESSOR_TYPE_LEN_VEC2 4
#define GLTF_ACCESSOR_TYPE_LEN_VEC3 4
#define GLTF_ACCESSOR_TYPE_LEN_VEC4 4
#define GLTF_ACCESSOR_TYPE_LEN_MAT2 4
#define GLTF_ACCESSOR_TYPE_LEN_MAT3 4
#define GLTF_ACCESSOR_TYPE_LEN_MAT4 4

static inline gltf_accessor_flag_bits gltf_accessor_type_to_flags(json_string type) {
    if (memcmp("SCALAR", type.cstr, GLTF_ACCESSOR_TYPE_LEN_SCALAR) == 0)
        return GLTF_ACCESSOR_TYPE_SCALAR_BIT;
    else if (memcmp("VEC2", type.cstr, GLTF_ACCESSOR_TYPE_LEN_VEC2) == 0)
         return GLTF_ACCESSOR_TYPE_VEC2_BIT;
    else if (memcmp("VEC3", type.cstr, GLTF_ACCESSOR_TYPE_LEN_VEC3) == 0)
         return GLTF_ACCESSOR_TYPE_VEC3_BIT;
    else if (memcmp("VEC4", type.cstr, GLTF_ACCESSOR_TYPE_LEN_VEC4) == 0)
         return GLTF_ACCESSOR_TYPE_VEC4_BIT;
    else if (memcmp("MAT2", type.cstr, GLTF_ACCESSOR_TYPE_LEN_MAT2) == 0)
         return GLTF_ACCESSOR_TYPE_MAT2_BIT;
    else if (memcmp("MAT3", type.cstr, GLTF_ACCESSOR_TYPE_LEN_MAT3) == 0)
         return GLTF_ACCESSOR_TYPE_MAT3_BIT;
    else if (memcmp("MAT4", type.cstr, GLTF_ACCESSOR_TYPE_LEN_MAT4) == 0)
        return GLTF_ACCESSOR_TYPE_MAT4_BIT;
    log_print_error("Invalid accessor type");
    return 0x0;
}

// @Todo There must be a better way... There just seems to be no uniformity to VkFormat
static VkFormat gltf_accessor_flags_to_vkformat(uint flags, uint *byte_stride)
{
    switch(flags & (GLTF_ACCESSOR_COMPONENT_TYPE_BITS | GLTF_ACCESSOR_TYPE_BITS | GLTF_ACCESSOR_NORMALIZED_BIT)) {
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | GLTF_ACCESSOR_TYPE_MAT4_BIT:
        *byte_stride = 64;
        return VK_FORMAT_UNDEFINED;

    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 1;
        return VK_FORMAT_R8_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R8G8_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 3;
        return VK_FORMAT_R8G8B8_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R8G8B8A8_SINT;

    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 1;
        return VK_FORMAT_R8_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R8G8_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 3;
        return VK_FORMAT_R8G8B8_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R8G8B8A8_UINT;

    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R16_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R16G16_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 6;
        return VK_FORMAT_R16G16B16_SINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R16G16B16A16_SINT;

    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R16_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R16G16_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 6;
        return VK_FORMAT_R16G16B16_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R16G16B16A16_UINT;

    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R32_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R32G32_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 12;
        return VK_FORMAT_R32G32B32_UINT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 16;
        return VK_FORMAT_R32G32B32A32_UINT;

    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R32_SFLOAT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R32G32_SFLOAT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 12;
        return VK_FORMAT_R32G32B32_SFLOAT;
    case GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 16;
        return VK_FORMAT_R32G32B32A32_SFLOAT;

    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 1;
        return VK_FORMAT_R8_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R8G8_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 3;
        return VK_FORMAT_R8G8B8_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R8G8B8A8_SNORM;

    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 1;
        return VK_FORMAT_R8_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R8G8_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 3;
        return VK_FORMAT_R8G8B8_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_BYTE_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R8G8B8A8_UNORM;

    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R16_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R16G16_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 6;
        return VK_FORMAT_R16G16B16_SNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R16G16B16A16_SNORM;

    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_SCALAR_BIT:
        *byte_stride = 2;
        return VK_FORMAT_R16_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC2_BIT:
        *byte_stride = 4;
        return VK_FORMAT_R16G16_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC3_BIT:
        *byte_stride = 6;
        return VK_FORMAT_R16G16B16_UNORM;
    case GLTF_ACCESSOR_NORMALIZED_BIT | GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT | GLTF_ACCESSOR_TYPE_VEC4_BIT:
        *byte_stride = 8;
        return VK_FORMAT_R16G16B16A16_UNORM;

    default:
        return VK_FORMAT_UNDEFINED;
    }
}

static void gltf_parse_accessors(uint index, json *j, uint extra_attrs, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_array *json_accessor_array = &j->obj.values[index].arr;
    json_object *accessor_obj, *sparse_obj, *tmp_obj;

    uint cnt = json_accessor_array->len & tmp;
    g->accessor_count = cnt;
    g->accessors = sallocate(alloc, *g->accessors, cnt + extra_attrs);
    gltf_accessor *accessor;
    uint i, ki, tmp_cnt;
    for(i = 0; i < cnt; ++i) {
        accessor_obj = &json_accessor_array->objs[i];
        accessor = &g->accessors[i];
        accessor->flags = 0x0;

        // I think I can remove the ternaries because some keys are required, therefore it
        // must be safe to deref at zero. I do this in the later properties, e.g animations.
        ki = json_find_key(accessor_obj, "bufferView");
        accessor->buffer_view = ki == Max_u32 ? ki : accessor_obj->values[ki].num;
        ki = json_find_key(accessor_obj, "byteOffset");
        accessor->byte_offset = ki == Max_u32 ? 0 : accessor_obj->values[ki].num;

        ki = json_find_key(accessor_obj, "componentType");
        log_print_error_if(ki == Max_u32, "accessor.componentType must be defined");
        accessor->flags |= gltf_accessor_component_type_to_flags(accessor_obj->values[ki].num);

        ki = json_find_key(accessor_obj, "normalized");
        accessor->flags |= ki == Max_u32 ? 0 : GLTF_ACCESSOR_NORMALIZED_BIT & max32_if_true(accessor_obj->values[ki].boolean);

        ki = json_find_key(accessor_obj, "count");
        log_print_error_if(ki == Max_u32, "accessor.count must be defined");
        accessor->count = accessor_obj->values[ki].num;

        ki = json_find_key(accessor_obj, "type");
        log_print_error_if(ki == Max_u32, "accessor.type must be defined");
        accessor->flags |= gltf_accessor_type_to_flags(accessor_obj->values[ki].str);

        accessor->vkformat = gltf_accessor_flags_to_vkformat(accessor->flags, &accessor->byte_stride);

        ki = json_find_key(accessor_obj, "max");
        if (ki != Max_u32) {
            accessor->flags |= GLTF_ACCESSOR_MINMAX_BIT;

            // Cannot just memcpy, as the json_nums are stored as doubles not floats.
            tmp_cnt = accessor_obj->values[ki].arr.len;
            for(tmp = 0; tmp < tmp_cnt; ++tmp)
                accessor->max_min.max[tmp] = accessor_obj->values[ki].arr.nums[tmp];

            ki = json_find_key(accessor_obj, "min");
            log_print_error_if(ki == Max_u32, "if accessors.max is defined, accessors.min must also be defined");
            for(tmp = 0; tmp < tmp_cnt; ++tmp)
                accessor->max_min.min[tmp] = accessor_obj->values[ki].arr.nums[tmp];
        }

        ki = json_find_key(accessor_obj, "sparse");
        if (ki != Max_u32) {
            accessor->flags |= GLTF_ACCESSOR_SPARSE_BIT;

            sparse_obj = &accessor_obj->values[ki].obj;

            ki = json_find_key(sparse_obj, "count");
            log_print_error_if(ki == Max_u32, "accessor.sparse.count must be defined");
            accessor->sparse.count = sparse_obj->values[ki].num;

            ki = json_find_key(sparse_obj, "indices");
            log_print_error_if(ki == Max_u32, "accessor.sparse.indices must be defined");
            tmp_obj = &sparse_obj->values[ki].obj;

            ki = json_find_key(tmp_obj, "bufferView");
            log_print_error_if(ki == Max_u32, "accessor.sparse.indices.bufferView must be defined");
            accessor->sparse.indices.buffer_view = tmp_obj->values[ki].num;

            ki = json_find_key(tmp_obj, "byteOffset");
            accessor->sparse.indices.byte_offset = ki == Max_u32 ? 0 : tmp_obj->values[ki].num;

            ki = json_find_key(tmp_obj, "componentType");
            log_print_error_if(ki == Max_u32, "accessor.sparse.indices.componentType must be defined");
            accessor->sparse.indices.component_type = gltf_accessor_component_type_to_flags(tmp_obj->values[ki].num);

            ki = json_find_key(sparse_obj, "values");
            log_print_error_if(ki == Max_u32, "accessor.sparse.values must be defined");
            tmp_obj = &sparse_obj->values[ki].obj;

            ki = json_find_key(tmp_obj, "bufferView");
            log_print_error_if(ki == Max_u32, "accessor.sparse.values.bufferView must be defined");
            accessor->sparse.values.buffer_view = tmp_obj->values[ki].num;

            ki = json_find_key(tmp_obj, "byteOffset");
            accessor->sparse.values.byte_offset = ki == Max_u32 ? 0 : tmp_obj->values[ki].num;
        }
    }
}

static inline gltf_animation_path_bits gltf_animation_translate_target_path(string *str) {
    if (memcmp("translation", str->cstr, str->len) == 0)
        return GLTF_ANIMATION_PATH_TRANSLATION_BIT;
    else if (memcmp("rotation", str->cstr, str->len) == 0)
        return GLTF_ANIMATION_PATH_ROTATION_BIT;
    else if (memcmp("scale", str->cstr, str->len) == 0)
        return GLTF_ANIMATION_PATH_SCALE_BIT;
    else if (memcmp("weights", str->cstr, str->len) == 0)
        return GLTF_ANIMATION_PATH_WEIGHTS_BIT;

    log_print_error("Invalid animation target path");
    return Max_u32;
}

#define GLTF_ANIMATION_TARGET_MASK 2
static uint gltf_parse_animation_targets(uint count, json_object *channel_objs, gltf_animation_target *targets)
{
    assert(count < (GLTF_ANIMATION_TARGET_MASK << 6) && "Below mask too small"); // This probably could be one u64
    uint64 mask[GLTF_ANIMATION_TARGET_MASK] = {};
    uint64 one = 1;
    uint tc = 0;
    uint i, j, ki0, ki1, tmp, node;
    for(i = 0; i < count; ++i) {
        if (mask[i>>6] & (one << (i & 63)))
            continue;

        targets[tc].path_mask = 0;
        memset(targets[tc].samplers, 0xff, sizeof(targets[tc].samplers));
        for(j = i; j < count; ++j) {
            if (mask[j>>6] & (one << (j & 63)))
                continue;

            ki0 = json_find_key(&channel_objs[j], "target");
            log_print_error_if(ki0 == Max_u32, "animations.channels.target must be defined");

            tmp = json_find_key(&channel_objs[j].values[ki0].obj, "node");
            ki1 = tmp & max64_if_true(tmp != Max_u32);
            log_print_error_if(ki1 == Max_u32, "animations.channels.target.node must be defined, this parser does not support this extension yet.");

            if ((uint)channel_objs[j].values[ki0].obj.values[ki1].num != targets[tc].node && j != i)
                continue;

            mask[j>>6] |= one << (j & 63);

            targets[tc].node = (uint)channel_objs[j].values[ki0].obj.values[ki1].num;

            ki1 = json_find_key(&channel_objs[j].values[ki0].obj, "path");
            log_print_error_if(ki1 == Max_u32, "animations.channels.target.path must be defined");
            uint path = gltf_animation_translate_target_path(&channel_objs[j].values[ki0].obj.values[ki1].str);
            targets[tc].path_mask |= path;

            ki0 = json_find_key(&channel_objs[j], "sampler");
            log_print_error_if(ki0 == Max_u32, "animations.channels.sampler must be defined");
            targets[tc].samplers[ctz(path)] = (uint16)channel_objs[j].values[ki0].num;
        }
        tc++;
    }
    return tc;
}

#define GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_LINEAR 6
#define GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_STEP 4
#define GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_CUBICSPLINE 11

static inline gltf_animation_interpolation gltf_animation_translate_interpolation(string *str) {
    if (!str)
        return GLTF_ANIMATION_INTERPOLATION_LINEAR;
    else if (memcmp("LINEAR", str->cstr, GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_LINEAR) == 0)
        return GLTF_ANIMATION_INTERPOLATION_LINEAR;
    else if (memcmp("STEP", str->cstr, GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_STEP) == 0)
        return GLTF_ANIMATION_INTERPOLATION_STEP;
    else if (memcmp("CUBICSPLINE", str->cstr, GLTF_ANIMATION_INTERPOLATION_TYPE_LEN_CUBICSPLINE) == 0)
        return GLTF_ANIMATION_INTERPOLATION_CUBICSPLINE;

    log_print_error("Invalid animation target path");
    return Max_u32;
}

static void gltf_parse_animation_samplers(uint count, json_object *sampler_objs, gltf_animation_sampler *samplers)
{
    uint i, ki, tmp;
    uint64 ptr;
    for(i = 0; i < count; ++i) {
        ki = json_find_key(&sampler_objs[i], "input");
        log_print_error_if(ki == Max_u32, "animations.samplers.input must be defined");
        samplers[i].input = sampler_objs[i].values[ki].num;

        tmp = json_find_key(&sampler_objs[i], "interpolation");
        ki = tmp & max64_if_true(tmp != Max_u32);
        ptr = ((uint64)(&sampler_objs[i].values[ki].str)) & max64_if_true(tmp != Max_u32);
        samplers[i].interpolation = gltf_animation_translate_interpolation((string*)ptr);

        ki = json_find_key(&sampler_objs[i], "output");
        log_print_error_if(ki == Max_u32, "animations.samplers.output must be defined");
        samplers[i].output = sampler_objs[i].values[ki].num;
    }
}

static void gltf_parse_animations(uint index, json *j, allocator *alloc, uint *anim_target_counts, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_animations = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->animation_count = cnt;
    g->animations = sallocate(alloc, *g->animations, cnt);
    gltf_animation *animations = g->animations;
    uint i, ki;
    for(i = 0; i < cnt; ++i) {
        ki = json_find_key(&json_animations[i], "channels");
        log_print_error_if(ki == Max_u32, "animations.channels must be defined");

        tmp = json_animations[i].values[ki].arr.len;
        animations[i].targets = sallocate(alloc, *animations->targets, anim_target_counts[i]);
        animations[i].target_count = gltf_parse_animation_targets(tmp, json_animations[i].values[ki].arr.objs, animations[i].targets);

        ki = json_find_key(&json_animations[i], "samplers");
        log_print_error_if(ki == Max_u32, "animations.samplers must be defined");

        tmp = json_animations[i].values[ki].arr.len;
        animations[i].sampler_count = tmp;
        animations[i].samplers = sallocate(alloc, *animations->samplers, tmp);
        gltf_parse_animation_samplers(tmp, json_animations[i].values[ki].arr.objs, animations[i].samplers);
    }
}

static void gltf_parse_buffers(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_buffers = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->buffer_count = cnt;
    g->buffers = sallocate(alloc, *g->buffers, cnt);
    gltf_buffer *buffers = g->buffers;
    uint i, ki;
    char *ptr;
    for(i = 0; i < cnt; ++i) {
        ki = json_find_key(&json_buffers[i], "byteLength");
        log_print_error_if(ki == Max_u32, "buffer.byteLength must be defined");
        buffers[i].byte_length = json_buffers[i].values[ki].num;

        // always allocate at least one byte, even if uri is undefined, and null terminate.
        tmp = json_find_key(&json_buffers[i], "uri");
        ki = tmp & max64_if_true(tmp != Max_u32);
        buffers[i].uri.len = json_buffers[i].values[ki].str.len & max64_if_true(tmp != Max_u32);
        assert(buffers[i].uri.len < GLTF_MAX_URI_LEN); // must be '<' for null temination
        ptr = allocate(alloc, GLTF_MAX_URI_LEN);
        memcpy(ptr, json_buffers[i].values[ki].str.cstr, buffers[i].uri.len);
        ptr[buffers[i].uri.len] = '\0';
        buffers[i].uri.cstr = ptr;
    }
}

typedef enum {
    GLTF_BUFFER_VIEW_TARGET_UNDEFINED     = 0,
    GLTF_BUFFER_VIEW_TARGET_VERTEX_BUFFER = 34962,
    GLTF_BUFFER_VIEW_TARGET_INDEX_BUFFER  = 34963,
} gltf_buffer_view_target;

static inline gltf_buffer_view_flag_bits gltf_buffer_view_translate_target(uint type) {
    switch(type) {
    case GLTF_BUFFER_VIEW_TARGET_VERTEX_BUFFER:
        return GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT;
    case GLTF_BUFFER_VIEW_TARGET_INDEX_BUFFER:
        return GLTF_BUFFER_VIEW_INDEX_BUFFER_BIT;
    default:
        return 0x0;
    }
}

static void gltf_parse_buffer_views(uint index, json *j, bool extra_attrs, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_buffer_views = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->buffer_view_count = cnt;
    g->buffer_views = sallocate(alloc, *g->buffer_views, cnt + extra_attrs);
    gltf_buffer_view *buffer_views = g->buffer_views;
    uint i, ki;
    for(i = 0; i < cnt; ++i) {
        buffer_views[i].flags = 0x0;

        ki = json_find_key(&json_buffer_views[i], "buffer");
        log_print_error_if(ki == Max_u32, "bufferView.buffer must be defined");
        buffer_views[i].buffer = json_buffer_views[i].values[ki].num;

        tmp = json_find_key(&json_buffer_views[i], "byteOffset");
        ki = tmp & max64_if_true(tmp != Max_u32);
        buffer_views[i].byte_offset = (uint64)json_buffer_views[i].values[ki].num & max64_if_true(tmp != Max_u32);

        ki = json_find_key(&json_buffer_views[i], "byteLength");
        log_print_error_if(ki == Max_u32, "bufferView.byteLength must be defined");
        buffer_views[i].byte_length = json_buffer_views[i].values[ki].num;

        tmp = json_find_key(&json_buffer_views[i], "byteStride");
        ki = tmp & max64_if_true(tmp != Max_u32);
        buffer_views[i].byte_stride = (uint64)json_buffer_views[i].values[ki].num & max64_if_true(tmp != Max_u32);

        tmp = json_find_key(&json_buffer_views[i], "target");
        ki = tmp & max64_if_true(tmp != Max_u32);
        buffer_views[i].flags |= gltf_buffer_view_translate_target((uint64)json_buffer_views[i].values[ki].num & max64_if_true(tmp != Max_u32));
    }
}

static void gltf_camera_parse_orthographic(json_object *json_camera, gltf_camera_orthographic *orthographic)
{
    uint ki = json_find_key(json_camera, "orthographic");
    log_print_error_if(ki == Max_u32, "if camera.type == orthographic, camera.orthographic must be defined");
    json_object *json_orthographic = &json_camera->values[ki].obj;

    ki = json_find_key(json_orthographic, "xmag");
    log_print_error_if(ki == Max_u32, "camera.orthographic.xmag must be defined");
    orthographic->xmag = json_orthographic->values[ki].num;

    ki = json_find_key(json_orthographic, "ymag");
    log_print_error_if(ki == Max_u32, "camera.orthographic.ymag must be defined");
    orthographic->ymag = json_orthographic->values[ki].num;

    ki = json_find_key(json_orthographic, "zfar");
    log_print_error_if(ki == Max_u32, "camera.orthographic.zfar must be defined");
    orthographic->zfar = json_orthographic->values[ki].num;

    ki = json_find_key(json_orthographic, "znear");
    log_print_error_if(ki == Max_u32, "camera.orthographic.znear must be defined");
    orthographic->znear = json_orthographic->values[ki].num;
}

static void gltf_camera_parse_perspective(json_object *json_camera, gltf_camera_perspective *perspective)
{
    uint ki = json_find_key(json_camera, "perspective");
    log_print_error_if(ki == Max_u32, "if camera.type == perspective, camera.perspective must be defined");
    json_object *json_perspective = &json_camera->values[ki].obj;

    uint tmp = json_find_key(json_perspective, "aspectRatio");
    ki = tmp & max32_if_true(tmp != Max_u32);
    perspective->aspect_ratio = tmp == Max_u32 ? Max_f32 : json_perspective->values[ki].num;

    ki = json_find_key(json_perspective, "yfov");
    log_print_error_if(ki == Max_u32, "camera.perspective.yfov must be defined");
    perspective->yfov = json_perspective->values[ki].num;

    tmp = json_find_key(json_perspective, "zfar");
    ki = tmp & max32_if_true(tmp != Max_u32);
    perspective->zfar = tmp == Max_u32 ? Max_f32 : json_perspective->values[ki].num;

    ki = json_find_key(json_perspective, "znear");
    log_print_error_if(ki == Max_u32, "camera.perspective.znear must be defined");
    perspective->znear = json_perspective->values[ki].num;
}

#define GLTF_CAMERA_TYPE_LEN_ORTHOGRAPHIC 12
#define GLTF_CAMERA_TYPE_LEN_PERSPECTIVE 11

static void gltf_parse_cameras(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_cameras = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->camera_count = cnt;
    g->cameras = sallocate(alloc, *g->cameras, cnt);
    gltf_camera *cameras = g->cameras;
    uint i, ki;
    for(i = 0; i < cnt; ++i) {
        cameras[i].flags = 0x0;
        ki = json_find_key(&json_cameras[i], "type");
        log_print_error_if(ki == Max_u32, "camera.type must be defined");
        if (memcmp("orthographic", json_cameras[i].values[ki].str.cstr, GLTF_CAMERA_TYPE_LEN_ORTHOGRAPHIC) == 0) {
            cameras[i].flags |= GLTF_CAMERA_ORTHOGRAPHIC_BIT;
            gltf_camera_parse_orthographic(&json_cameras[i], &cameras[i].orthographic);
        } else if (memcmp("perspective", json_cameras[i].values[ki].str.cstr, GLTF_CAMERA_TYPE_LEN_PERSPECTIVE) == 0) {
            cameras[i].flags |= GLTF_CAMERA_PERSPECTIVE_BIT;
            gltf_camera_parse_perspective(&json_cameras[i], &cameras[i].perspective);
        } else {
            log_print_error("camera.type must be one of 'orthographic' or 'perspective' but is neither.");
        }
    }
}

#define GLTF_IMAGE_MIME_TYPE_LEN_JPEG 10 // strlen("image/jpeg")
#define GLTF_IMAGE_MIME_TYPE_LEN_PNG 9 // strlen("image/png")

static inline gltf_image_flag_bits gltf_image_translate_mime_type(string *str) {
    if (memcmp("image/jpeg", str->cstr, GLTF_IMAGE_MIME_TYPE_LEN_JPEG) == 0)
        return GLTF_IMAGE_JPEG_BIT;
    else if (memcmp("image/png", str->cstr, GLTF_IMAGE_MIME_TYPE_LEN_PNG) == 0)
        return GLTF_IMAGE_PNG_BIT;
    log_print_error("unrecognised image mime type");
    return 0x0;
}

static void gltf_parse_images(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_images = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->image_count = cnt;
    g->images = sallocate(alloc, *g->images, cnt);
    gltf_image *images = g->images;
    uint i, ki;
    char *ptr;
    for(i = 0; i < cnt; ++i) {
        images[i].flags = 0x0;
        ki = json_find_key(&json_images[i], "uri");
        if (ki != Max_u32) {
            assert(json_images[i].values[ki].str.len < GLTF_MAX_URI_LEN);
            ptr = allocate(alloc, GLTF_MAX_URI_LEN);
            memcpy(ptr, json_images[i].values[ki].str.cstr, json_images[i].values[ki].str.len);
            ptr[json_images[i].values[ki].str.len] = '\0';
            images[i].uri.len = json_images[i].values[ki].str.len;
            images[i].uri.cstr = ptr;
        } else {
            images[i].uri = (string){NULL, 0};
        }
        ki = json_find_key(&json_images[i], "mimeType");
        images[i].flags |= ki != Max_u32 ? gltf_image_translate_mime_type(&json_images[i].values[ki].str) : 0x0;
        ki = json_find_key(&json_images[i], "bufferView");
        images[i].buffer_view = ki != Max_u32 ? json_images[i].values[ki].num : Max_u32;
    }
}

static void gltf_material_parse_pbr(json_object *j_pbr, gltf_material *mat)
{
    uint ki0, ki1;
    ki0 = json_find_key(j_pbr, "baseColorFactor");
    mat->uniforms.base_color_factor[0] = 1;
    mat->uniforms.base_color_factor[1] = 1;
    mat->uniforms.base_color_factor[2] = 1;
    mat->uniforms.base_color_factor[3] = 1;
    for(uint i = 0; i < 4 * (ki0 != Max_u32); ++i)
        mat->uniforms.base_color_factor[i] = j_pbr->values[ki0].arr.nums[i];

    ki0 = json_find_key(j_pbr, "metallicFactor");
    mat->uniforms.metallic_factor = ki0 == Max_u32 ? 1 : j_pbr->values[ki0].num;
    ki0 = json_find_key(j_pbr, "roughnessFactor");
    mat->uniforms.roughness_factor = ki0 == Max_u32 ? 1 : j_pbr->values[ki0].num;

    ki0 = json_find_key(j_pbr, "baseColorTexture");
    if (ki0 != Max_u32) {
        mat->flags |= GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT;

        ki1 = json_find_key(&j_pbr->values[ki0].obj, "index");
        log_print_error_if(ki1 == Max_u32, "material.pbrMetallicRoughness.baseColorTexture.index must be defined");
        mat->base_color.texture = j_pbr->values[ki0].obj.values[ki1].num;
        ki1 = json_find_key(&j_pbr->values[ki0].obj, "texCoord");
        mat->base_color.texcoord = ki1 != Max_u32 ? j_pbr->values[ki0].obj.values[ki1].num : 0;
    }

    ki0 = json_find_key(j_pbr, "metallicRoughnessTexture");
    if (ki0 != Max_u32) {
        mat->flags |= GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT;

        ki1 = json_find_key(&j_pbr->values[ki0].obj, "index");
        log_print_error_if(ki1 == Max_u32, "material.pbrMetallicRoughness.metallicRoughnessTexture.index must be defined");
        mat->metallic_roughness.texture = j_pbr->values[ki0].obj.values[ki1].num;
        ki1 = json_find_key(&j_pbr->values[ki0].obj, "texCoord");
        mat->metallic_roughness.texcoord = ki1 != Max_u32 ? j_pbr->values[ki0].obj.values[ki1].num : 0;
    }
}

static void gltf_material_parse_normal(json_object *j_norm, gltf_material *mat)
{
    mat->flags |= GLTF_MATERIAL_NORMAL_TEXTURE_BIT;

    uint ki0 = json_find_key(j_norm, "scale");
    mat->uniforms.normal_scale = ki0 == Max_u32 ? 1 : j_norm->values[ki0].num;

    ki0 = json_find_key(j_norm, "index");
    log_print_error_if(ki0 == Max_u32, "material.normalTexture.index must be defined");
    mat->normal.texture = j_norm->values[ki0].num;

    ki0 = json_find_key(j_norm, "texCoord");
    mat->normal.texcoord = ki0 != Max_u32 ? j_norm->values[ki0].num : 0;
}

static void gltf_material_parse_occlusion(json_object *j_occl, gltf_material *mat)
{
    mat->flags |= GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT;

    uint ki0 = json_find_key(j_occl, "strength");
    mat->uniforms.occlusion_strength = ki0 == Max_u32 ? 1 : j_occl->values[ki0].num;

    ki0 = json_find_key(j_occl, "index");
    log_print_error_if(ki0 == Max_u32, "material.occlusionTexture.index must be defined");
    mat->occlusion.texture = j_occl->values[ki0].num;

    ki0 = json_find_key(j_occl, "texCoord");
    mat->occlusion.texcoord = ki0 != Max_u32 ? j_occl->values[ki0].num : 0;
}

static void gltf_material_parse_emissive(json_object *j_emi, gltf_material *mat)
{
    mat->flags |= GLTF_MATERIAL_NORMAL_TEXTURE_BIT;

    uint ki0 = json_find_key(j_emi, "index");
    log_print_error_if(ki0 == Max_u32, "material.emissiveTexture.index must be defined");
    mat->emissive.texture = j_emi->values[ki0].num;

    ki0 = json_find_key(j_emi, "texCoord");
    mat->emissive.texcoord = ki0 != Max_u32 ? j_emi->values[ki0].num : 0;
}

#define GLTF_MATERIAL_ALPHA_MODE_LEN_OPAQUE 6
#define GLTF_MATERIAL_ALPHA_MODE_LEN_MASK 4
#define GLTF_MATERIAL_ALPHA_MODE_LEN_BLEND 5

static inline gltf_material_flag_bits gltf_material_translate_alpha_mode(string *s) {
    if (memcmp("OPAQUE", s->cstr, GLTF_MATERIAL_ALPHA_MODE_LEN_OPAQUE) == 0)
        return GLTF_MATERIAL_ALPHA_MODE_OPAQUE_BIT;
    else if (memcmp("MASK", s->cstr, GLTF_MATERIAL_ALPHA_MODE_LEN_MASK) == 0)
        return GLTF_MATERIAL_ALPHA_MODE_MASK_BIT;
    else if (memcmp("BLEND", s->cstr, GLTF_MATERIAL_ALPHA_MODE_LEN_BLEND) == 0)
        return GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT;
    log_print_error("unrecognised material alpha mode");
    return 0x0;
}

static void gltf_parse_materials(uint index, json *j, allocator *alloc, gltf *g)
{
    log_print_error_if(SHADER_MATERIAL_UBO_SIZE != sizeof(gltf_material_uniforms),
            "These sizes must match for the sake of simpler copy code");

    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_materials = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->material_count = cnt;
    g->materials = sallocate(alloc, *g->materials, cnt);
    gltf_material *materials = g->materials;
    uint i, ki;
    for(i = 0; i < cnt; ++i) {
        ki = json_find_key(&json_materials[i], "pbrMetallicRoughness");
        if (ki != Max_u32) {
            gltf_material_parse_pbr(&json_materials[i].values[ki].obj, &materials[i]);
        } else {
            materials[i].uniforms.base_color_factor[0] = 1;
            materials[i].uniforms.base_color_factor[1] = 1;
            materials[i].uniforms.base_color_factor[2] = 1;
            materials[i].uniforms.base_color_factor[3] = 1;
            materials[i].uniforms.metallic_factor = 1;
            materials[i].uniforms.roughness_factor = 1;
        }

        ki = json_find_key(&json_materials[i], "normalTexture");
        if (ki != Max_u32)
            gltf_material_parse_normal(&json_materials[i].values[ki].obj, &materials[i]);
        else
            materials[i].uniforms.normal_scale = 1;

        ki = json_find_key(&json_materials[i], "occlusionTexture");
        if (ki != Max_u32)
            gltf_material_parse_occlusion(&json_materials[i].values[ki].obj, &materials[i]);
        else
            materials[i].uniforms.occlusion_strength = 1;

        ki = json_find_key(&json_materials[i], "emissiveTexture");
        if (ki != Max_u32)
            gltf_material_parse_emissive(&json_materials[i].values[ki].obj, &materials[i]);

        ki = json_find_key(&json_materials[i], "emissiveFactor");
        materials[i].uniforms.emissive_factor[0] = 0;
        materials[i].uniforms.emissive_factor[1] = 0;
        materials[i].uniforms.emissive_factor[2] = 0;
        for(tmp = 0; tmp < 3 * (ki != Max_u32); ++tmp)
            materials[i].uniforms.emissive_factor[tmp] = json_materials[i].values[ki].arr.nums[tmp];

        ki = json_find_key(&json_materials[i], "alphaMode");
        materials[i].flags |= ki != Max_u32 ? gltf_material_translate_alpha_mode(&json_materials[i].values[ki].str) : GLTF_MATERIAL_ALPHA_MODE_OPAQUE_BIT;
        ki = json_find_key(&json_materials[i], "alphaCutoff");
        materials[i].uniforms.alpha_cutoff = ki != Max_u32 ? json_materials[i].values[ki].num : 0.5;
        ki = json_find_key(&json_materials[i], "doubleSided");
        materials[i].flags |= ki != Max_u32 ? GLTF_MATERIAL_DOUBLE_SIDED_BIT & max32_if_true(json_materials[i].values[ki].boolean) : 0;
    }
}

static inline gltf_mesh_primitive_topology gltf_mesh_primitive_translate_mode(uint64 mode) {
    switch(mode) {
    case 0: // POINTS
        return GLTF_MESH_PRIMITIVE_POINT_LIST;
    case 1: // LINES
        return GLTF_MESH_PRIMITIVE_LINE_LIST;
    case 2: // LINE_LOOP
        log_print_error("@Todo Idk how to map mesh.primitive.mode.line_loop to vulkan.");
        return 0x0; // Idk what to return. Point list should make it obvious to see graphically what is wrong.
    case 3: // LINE_STRIP
        return GLTF_MESH_PRIMITIVE_LINE_STRIP;
    case 4: // TRIANGLES
        return GLTF_MESH_PRIMITIVE_TRIANGLE_LIST;
    case 5: // TRIANGLE_STRIP
        return GLTF_MESH_PRIMITIVE_TRIANGLE_STRIP;
    case 6: // TRIANGLE_FAN
        return GLTF_MESH_PRIMITIVE_TRIANGLE_FAN;
    case Max_u64: // Undefined defaults to TRIANGLES
        return GLTF_MESH_PRIMITIVE_TRIANGLE_LIST;
    default:
        log_print_error("unrecognisable mesh.primitive.mode");
        return GLTF_MESH_PRIMITIVE_POINT_LIST; // Again, this should make it easy to see what is wrong.
    }
}

#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_POSITION 8
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_NORMAL 6
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TANGENT 7
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TEXCOORD 8
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_COLOR 5
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_JOINTS 6
#define GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_WEIGHTS 7

const int ATTR_KEY_LENS[] = {
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_POSITION,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_JOINTS,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_WEIGHTS,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_NORMAL,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TANGENT,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TEXCOORD,
    GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_COLOR,
};

static inline bool gltf_single_attr(uint i)
{
    return i == GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION ||
           i == GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL ||
           i == GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT;
}

static uint gltf_mesh_parse_primitive_attributes(json_object *j_attribs, struct gltf_extra_attrs *extra_attrs, gltf_mesh_primitive_attribute *attribs)
{
    assert(j_attribs->key_count < 32);
    uint attr_m[7];
    memset(attr_m,0,sizeof(attr_m));
    uint i;
    for(i=0;i<j_attribs->key_count;++i) {
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "POSITION", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_POSITION) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "JOINTS", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_JOINTS) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "WEIGHTS", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_WEIGHTS) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "NORMAL", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_NORMAL) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "TANGENT", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TANGENT) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "TEXCOORD", GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_TEXCOORD) == 0);
        attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR] |= (1<<i) &
            max_if(memcmp(j_attribs->keys[i].cstr, "COLOR",GLTF_MESH_PRIMITIVE_ATTRIBUTE_KEY_LEN_COLOR) == 0);
    }

    extra_attrs->norm = !attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL] &&
                         attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD];
    extra_attrs->tang = !attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT] &&
                         attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD];
    uint extra_attr_cnt = extra_attrs->norm || extra_attrs->tang;

    // ensure tang and norm attribs are initialized (their accessor fields will
    // take the accessor of the 0th attr, but this is corrected later in parse_gltf)
    attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL]  |= 0x1 & maxif(!attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL] &&
                                                                       attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD]);
    attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT] |= 0x1 & maxif(!attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT] &&
                                                                       attr_m[GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD]);

    uint cnt = 0;
    uint tz,pc,j,idx;
    for(i=0;i<7;++i) {
        pc = popcnt(attr_m[i]);
        for(j=0;j<pc;++j) {
            tz = ctz(attr_m[i]);
            attr_m[i] &= ~(1<<tz);

            idx = ((j_attribs->keys[tz].cstr[ATTR_KEY_LENS[i]+1] - '0') & max_if(!gltf_single_attr(i))) + cnt;
            attribs[idx].type = i;
            attribs[idx].n = (j_attribs->keys[tz].cstr[ATTR_KEY_LENS[i]+1] - '0') | max_if(gltf_single_attr(i));
            attribs[idx].accessor = j_attribs->values[tz].num;

            // sections in shader.c rely on this assert
            assert((attribs[idx].n <= 7 || attribs[idx].n == Max_u32));
        }
        cnt += pc;
    }
    return extra_attr_cnt;
}

static uint gltf_mesh_parse_primitives(json_array *j_prim_array, struct gltf_extra_attrs *extra_attrs, allocator *alloc, gltf_mesh *mesh)
{
    json_object *j_prims = j_prim_array->objs;
    json_object *j_attribs;
    uint cnt = j_prim_array->len;
    mesh->primitive_count = cnt;
    mesh->primitives = sallocate(alloc, *mesh->primitives, cnt);
    gltf_mesh_primitive *prims = mesh->primitives;
    uint extra_attr_cnt = 0;
    uint i,j,ki,tmp,cnt2;
    for(i = 0; i < cnt; ++i) {
        ki = json_find_key(&j_prims[i], "attributes");
        log_print_error_if(ki == Max_u32, "mesh.primitives.attributes must be defined");

        prims[i].attribute_count = j_prims[i].values[ki].obj.key_count;
        prims[i].attributes = sallocate(alloc, *prims[i].attributes,
                prims[i].attribute_count +
                (json_find_key(&j_prims[i].values[ki].obj, "NORMAL") == Max_u32) +
                (json_find_key(&j_prims[i].values[ki].obj, "TANGENT") == Max_u32));

        tmp = gltf_mesh_parse_primitive_attributes(&j_prims[i].values[ki].obj, extra_attrs + extra_attr_cnt, prims[i].attributes);
        extra_attrs[extra_attr_cnt].prim = i; // this out of bounds write is fine, I am allocating +1
        extra_attr_cnt += tmp;

        tmp = json_find_key(&j_prims[i], "indices");
        ki = tmp & max32_if_true(tmp != Max_u32);
        prims[i].indices = (uint64)j_prims[i].values[ki].num | max64_if_true(tmp == Max_u32);

        tmp = json_find_key(&j_prims[i], "material");
        mesh->primitives_without_material_count += tmp == Max_u32;
        ki = tmp & max32_if_true(tmp != Max_u32);
        prims[i].material = (uint64)j_prims[i].values[ki].num | max64_if_true(tmp == Max_u32);

        tmp = json_find_key(&j_prims[i], "mode");
        ki = tmp & max32_if_true(tmp != Max_u32);
        prims[i].topology =
            gltf_mesh_primitive_translate_mode((uint64)j_prims[i].values[ki].num | max64_if_true(tmp == Max_u32));

        tmp = json_find_key(&j_prims[i], "targets");
        ki = tmp & max32_if_true(tmp != Max_u32);
        cnt2 = j_prims[i].values[ki].arr.len & max32_if_true(tmp != Max_u32);
        prims[i].target_count = cnt2;
        prims[i].morph_targets = sallocate(alloc, *prims->morph_targets, cnt2);
        for(j = 0; j < cnt2; ++j) {
            j_attribs = &j_prims[i].values[ki].arr.objs[j];
            prims[i].morph_targets[j].attribute_count = j_attribs->key_count;
            prims[i].morph_targets[j].attributes =
                sallocate(alloc, *prims[i].morph_targets[j].attributes, j_attribs->key_count);
            gltf_mesh_parse_primitive_attributes(j_attribs, extra_attrs + extra_attr_cnt, prims[i].morph_targets[j].attributes);
        }
    }
    return extra_attr_cnt;
}

static void gltf_parse_meshes(uint index, json *j, struct gltf_extra_attrs *extra_attrs, uint *extra_attr_count, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *json_meshes = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    assert(cnt <= GLTF_MAX_MESH_COUNT);

    g->mesh_count = cnt;
    g->meshes = sallocate(alloc, *g->meshes, cnt);
    gltf_mesh *meshes = g->meshes;

    uint eac = 0;

    uint i, ki;
    for(i = 0; i < cnt; ++i) {
        meshes[i].joint_count = 0;
        meshes[i].primitives_without_material_count = 0;

        ki = json_find_key(&json_meshes[i], "primitives");
        log_print_error_if(ki == Max_u32, "mesh.primitives must be defined");
        tmp = eac + gltf_mesh_parse_primitives(&json_meshes[i].values[ki].arr, extra_attrs + eac, alloc, &meshes[i]);
        for(; eac < tmp; ++eac)
            extra_attrs[eac].mesh = i; // this out of bounds write is fine, I am allocating +1

        tmp = json_find_key(&json_meshes[i], "weights");
        ki = tmp & max32_if_true(tmp != Max_u32);
        meshes[i].weight_count = json_meshes[i].values[ki].arr.len & max32_if_true(tmp != Max_u32);
        meshes[i].weights = sallocate(alloc, *meshes[i].weights, meshes[i].weight_count);

        log_print_error_if(meshes[i].weight_count > GLTF_MORPH_WEIGHT_COUNT, "meshes[%u].weight_count exceeds GLTF_MORPH_WEIGHT_COUNT");
        for(tmp = 0; tmp < meshes[i].weight_count; ++tmp)
            meshes[i].weights[tmp] = json_meshes[i].values[ki].arr.nums[tmp];
    }
    *extra_attr_count = eac;
}

static void gltf_parse_nodes(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *j_nodes = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    assert(cnt <=  GLTF_MAX_NODE_COUNT);
    assert(cnt <  (GLTF_U64_NODE_MASK<<6));

    g->node_count = cnt;
    g->nodes = sallocate(alloc, *g->nodes, cnt);
    gltf_node *nodes = g->nodes;

    uint64 mn[GLTF_MAX_NODE_COUNT>>6]; // meshed node masks
    uint16 ic[GLTF_MAX_MESH_COUNT]; // mesh instance counts
    memset(mn, 0, sizeof(mn));
    memset(ic, 0, sizeof(ic));

    float *mat4;
    uint64 one = 1;

    uint i,ki,mat,trs;
    for(i=0; i < cnt; ++i) {
        mat = 0;
        trs = 0;
        bool t = 0;
        bool r = 0;
        bool s = 0;

        nodes[i].flags = 0;

        ki = json_find_key(&j_nodes[i], "camera");
        nodes[i].camera = ki != Max_u32 ? j_nodes[i].values[ki].num : Max_u32;

        ki = json_find_key(&j_nodes[i], "children");
        nodes[i].child_count = 0;
        if (ki != Max_u32) {
            nodes[i].child_count = j_nodes[i].values[ki].arr.len;
            nodes[i].children = sallocate(alloc, *nodes[i].children, nodes[i].child_count);
            for(tmp=0;tmp<nodes[i].child_count;++tmp)
                nodes[i].children[tmp] = j_nodes[i].values[ki].arr.nums[tmp];
        }

        ki = json_find_key(&j_nodes[i], "skin");
        nodes[i].skin = ki != Max_u32 ? j_nodes[i].values[ki].num : Max_u32;

        // @Note json.nums are stored as doubles.
        ki = json_find_key(&j_nodes[i], "matrix");
        if (ki != Max_u32) {
            mat = true;
            nodes[i].mat.m[0]  = j_nodes[i].values[ki].arr.nums[0];
            nodes[i].mat.m[1]  = j_nodes[i].values[ki].arr.nums[4];
            nodes[i].mat.m[2]  = j_nodes[i].values[ki].arr.nums[8];
            nodes[i].mat.m[3]  = j_nodes[i].values[ki].arr.nums[12];
            nodes[i].mat.m[4]  = j_nodes[i].values[ki].arr.nums[1];
            nodes[i].mat.m[5]  = j_nodes[i].values[ki].arr.nums[5];
            nodes[i].mat.m[6]  = j_nodes[i].values[ki].arr.nums[9];
            nodes[i].mat.m[7]  = j_nodes[i].values[ki].arr.nums[13];
            nodes[i].mat.m[8]  = j_nodes[i].values[ki].arr.nums[2];
            nodes[i].mat.m[9]  = j_nodes[i].values[ki].arr.nums[6];
            nodes[i].mat.m[10] = j_nodes[i].values[ki].arr.nums[10];
            nodes[i].mat.m[11] = j_nodes[i].values[ki].arr.nums[14];
            nodes[i].mat.m[12] = j_nodes[i].values[ki].arr.nums[3];
            nodes[i].mat.m[13] = j_nodes[i].values[ki].arr.nums[7];
            nodes[i].mat.m[14] = j_nodes[i].values[ki].arr.nums[11];
            nodes[i].mat.m[15] = j_nodes[i].values[ki].arr.nums[15];
        }

        ki = json_find_key(&j_nodes[i], "mesh");
        if (ki != Max_u32) {
            nodes[i].mesh = j_nodes[i].values[ki].num;

            ic[nodes[i].mesh]++;
            mn[nodes[i].mesh>>6] |= one << (nodes[i].mesh & 63);

            if (nodes[i].skin != Max_u32)
                g->meshes[nodes[i].mesh].joint_count = g->skins[nodes[i].skin].joint_count;
        } else {
            nodes[i].mesh = Max_u32;
        }

        ki = json_find_key(&j_nodes[i], "rotation");
        if (ki != Max_u32) {
            log_print_error_if(mat, "either node.matrix or node.trs can be defined, not both.");
            trs = 1;
            r = 1;
            nodes[i].trs.r.x = j_nodes[i].values[ki].arr.nums[0];
            nodes[i].trs.r.y = j_nodes[i].values[ki].arr.nums[1];
            nodes[i].trs.r.z = j_nodes[i].values[ki].arr.nums[2];
            nodes[i].trs.r.w = j_nodes[i].values[ki].arr.nums[3];
        }
        ki = json_find_key(&j_nodes[i], "scale");
        if (ki != Max_u32) {
            log_print_error_if(mat, "either node.matrix or node.trs can be defined, not both.");
            trs = 1;
            s = 1;
            nodes[i].trs.s.x = j_nodes[i].values[ki].arr.nums[0];
            nodes[i].trs.s.y = j_nodes[i].values[ki].arr.nums[1];
            nodes[i].trs.s.z = j_nodes[i].values[ki].arr.nums[2];
        }
        ki = json_find_key(&j_nodes[i], "translation");
        if (ki != Max_u32) {
            log_print_error_if(mat, "either node.matrix or node.trs can be defined, not both.");
            trs = 1;
            t = 1;
            nodes[i].trs.t.x = j_nodes[i].values[ki].arr.nums[0];
            nodes[i].trs.t.y = j_nodes[i].values[ki].arr.nums[1];
            nodes[i].trs.t.z = j_nodes[i].values[ki].arr.nums[2];
        }

        nodes[i].flags |= GLTF_NODE_MATRIX_BIT & max_if(mat);
        nodes[i].flags |= GLTF_NODE_TRS_BIT & max_if(trs);

        ki = json_find_key(&j_nodes[i], "weights");
        if (ki != Max_u32) {
            nodes[i].weight_count = j_nodes[i].values[ki].arr.len;
            nodes[i].weights = sallocate(alloc, *nodes[i].weights, nodes[i].weight_count);
            for(tmp=0;tmp<nodes[i].weight_count;++tmp)
                nodes[i].weights[tmp] = j_nodes[i].values[ki].arr.nums[tmp];
        } else if (nodes[i].mesh != Max_u32 && g->meshes[nodes[i].mesh].weight_count) {
            nodes[i].weight_count = g->meshes[nodes[i].mesh].weight_count;
            nodes[i].weights = g->meshes[nodes[i].mesh].weights;
        }

        vector vt = {0,0,0};
        vector vr = {0,0,0,1};
        vector vs = {1,1,1};
        memcpy_if(&nodes[i].trs.t, &vt, sizeof(vt), !t && !mat);
        memcpy_if(&nodes[i].trs.r, &vr, sizeof(vr), !r && !mat);
        memcpy_if(&nodes[i].trs.s, &vs, sizeof(vs), !s && !mat);
    }
}

inline static void gltf_sampler_translate_filter_mipmap(uint opt, bool min, gltf_sampler_filter *f, gltf_sampler_mipmap_mode *m) {
    switch(opt) {
    case 9728: // NEAREST
        *f = GLTF_SAMPLER_FILTER_NEAREST;
        *m = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 9729: // LINEAR
        *f = GLTF_SAMPLER_FILTER_LINEAR;
        *m = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 9984: // NEAREST_MIPMAP_NEAREST
        log_print_error_if(!min, "sampler filter option is only valid for minification");
        *f = GLTF_SAMPLER_FILTER_NEAREST;
        *m = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 9985: // LINEAR_MIPMAP_NEAREST
        log_print_error_if(!min, "sampler filter option is only valid for minification");
        *f = GLTF_SAMPLER_FILTER_LINEAR;
        *m = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 9986: // NEAREST_MIPMAP_LINEAR
        log_print_error_if(!min, "sampler filter option is only valid for minification");
        *f = GLTF_SAMPLER_FILTER_NEAREST;
        *m = GLTF_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    case 9987: // LINEAR_MIPMAP_LINEAR
        log_print_error_if(!min, "sampler filter option is only valid for minification");
        *f = GLTF_SAMPLER_FILTER_LINEAR;
        *m = GLTF_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    default:
        log_print_error("unrecognised sampler filter");
        *f = GLTF_SAMPLER_FILTER_NEAREST;
        *m = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    }
}

inline static gltf_sampler_address_mode gltf_sampler_translate_wrap(uint opt) {
    switch(opt) {
    case 33071: // CLAMP_TO_EDGE
        return GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 33648: // MIRRORED_REPEAT
        return GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 10497: // REPEAT
        return GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
    default:
        log_print_error("unrecognised sampler wrap");
        return GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static void gltf_parse_samplers(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *j_samplers = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->sampler_count = cnt;
    g->samplers = sallocate(alloc, *g->samplers, cnt);
    gltf_sampler *samplers = g->samplers;
    gltf_sampler_mipmap_mode dummy;
    uint i,ki;
    for(i=0; i < cnt;++i) {
        ki = json_find_key(j_samplers, "magFilter");
        if (ki != Max_u32)
            gltf_sampler_translate_filter_mipmap(j_samplers[i].values[ki].num, false, &samplers[i].mag_filter, &dummy);
        else
            samplers[i].mag_filter = GLTF_SAMPLER_FILTER_NEAREST;

        ki = json_find_key(j_samplers, "minFilter");
        if (ki != Max_u32) {
            gltf_sampler_translate_filter_mipmap(j_samplers[i].values[ki].num, true, &samplers[i].min_filter, &samplers[i].mipmap_mode);
        } else {
            samplers[i].min_filter = GLTF_SAMPLER_FILTER_NEAREST;
            samplers[i].mipmap_mode = GLTF_SAMPLER_MIPMAP_MODE_NEAREST;
        }

        ki = json_find_key(j_samplers, "wrapS");
        samplers[i].wrap_u = ki != Max_u32 ? gltf_sampler_translate_wrap(j_samplers[i].values[ki].num) : GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
        ki = json_find_key(j_samplers, "wrapT");
        samplers[i].wrap_v = ki != Max_u32 ? gltf_sampler_translate_wrap(j_samplers[i].values[ki].num) : GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static void gltf_parse_scenes(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *j_scenes = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->scene_count = cnt;
    g->scenes = sallocate(alloc, *g->scenes, cnt);
    gltf_scene *scenes = g->scenes;

    uint ki = json_find_key(&j->obj, "scene");
    g->scene = ki != Max_u32 ? j->obj.values[ki].num : Max_u32;

    uint i,tmp2;
    char *ptr;
    for(i=0; i < cnt; ++i) {
        ki = json_find_key(&j_scenes[i], "nodes");
        if (ki != Max_u32) {
            scenes[i].node_count = j_scenes[i].values[ki].arr.len;
            scenes[i].nodes = sallocate(alloc, *scenes->nodes, scenes[i].node_count);
            for(tmp2=0;tmp2<scenes[i].node_count;++tmp2)
                scenes[i].nodes[tmp2] = j_scenes[i].values[ki].arr.nums[tmp2];
        }

        ki = json_find_key(&j_scenes[i], "name");
        if (ki != Max_u32) {
            tmp = j_scenes[i].values[ki].str.len;
            ptr = allocate(alloc, GLTF_MAX_URI_LEN);
            memcpy(ptr, j_scenes[i].values[ki].str.cstr, tmp);
            scenes[i].name.cstr = ptr;
            scenes[i].name.len = tmp;
        }
    }
}

static void gltf_parse_skins(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *j_skins = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->skin_count = cnt;
    g->skins = sallocate(alloc, *g->skins, cnt);
    gltf_skin *skins = g->skins;

    assert(g->skin_count < (GLTF_U64_SKIN_MASK<<6));

    uint i,ki,i2;
    for(i=0; i < cnt; ++i) {
        ki = json_find_key(&j_skins[i], "joints");
        log_print_error_if(ki == Max_u32, "skin.joints must be defined");
        skins[i].joint_count = j_skins[i].values[ki].arr.len;
        skins[i].joints = sallocate(alloc, *skins->joints, skins[i].joint_count);
        log_print_error_if(skins[i].joint_count > GLTF_JOINT_COUNT,
                "vertex shader supports %i joint, model uses %u", GLTF_JOINT_COUNT, skins[i].joint_count);
        for(i2=0; i2 < skins[i].joint_count; ++i2)
            skins[i].joints[i2] = j_skins[i].values[ki].arr.nums[i2];

        tmp = json_find_key(&j_skins[i], "inverseBindMatrices");
        ki = tmp & max32_if_true(tmp != Max_u32);
        skins[i].inverse_bind_matrices = (uint64)j_skins[i].values[ki].num | max64_if_true(tmp == Max_u32);
        tmp = json_find_key(&j_skins[i], "skeleton");
        ki = tmp & max32_if_true(tmp != Max_u32);
        skins[i].skeleton = (uint64)j_skins[i].values[ki].num | max64_if_true(tmp == Max_u32);
    }
}

static void gltf_parse_textures(uint index, json *j, allocator *alloc, gltf *g)
{
    uint tmp = index;
    tmp = max64_if_false(tmp == Max_u32);
    index &= tmp;
    json_object *j_textures = j->obj.values[index].arr.objs;

    uint cnt = j->obj.values[index].arr.len & tmp;
    g->texture_count = cnt;
    g->textures = sallocate(alloc, *g->textures, cnt);
    gltf_texture *textures = g->textures;

    uint i,ki;
    for(i=0; i < cnt; ++i) {
        ki = json_find_key(&j_textures[i], "sampler");
        textures[i].sampler = ki != Max_u32 ? j_textures[i].values[ki].num : Max_u32;
        ki = json_find_key(&j_textures[i], "source");
        textures[i].source = ki != Max_u32 ? j_textures[i].values[ki].num : Max_u32;
    }
}

#if TEST
static void test_gltf_accessors(test_suite *suite, gltf *g);
static void test_gltf_animations(test_suite *suite, gltf *g);
static void test_gltf_buffers(test_suite *suite, gltf *g);
static void test_gltf_buffer_views(test_suite *suite, gltf *g);
static void test_gltf_cameras(test_suite *suite, gltf *g);
static void test_gltf_images(test_suite *suite, gltf *g);
static void test_gltf_materials(test_suite *suite, gltf *g);
static void test_gltf_meshes(test_suite *suite, gltf *g);
static void test_gltf_nodes(test_suite *suite, gltf *g);
static void test_gltf_samplers(test_suite *suite, gltf *g);
static void test_gltf_scenes(test_suite *suite, gltf *g);
static void test_gltf_skins(test_suite *suite, gltf *g);
static void test_gltf_textures(test_suite *suite, gltf *g);

void test_gltf(test_suite *suite)
{
    struct allocation mem_used;
    gltf g = parse_gltf("test_gltf.gltf", NULL, NULL, suite->alloc, suite->alloc, &mem_used);

    assert(g.accessor_count == 4 && "Incorrect Accessor Count");
    test_gltf_accessors(suite, &g);
    assert(g.animation_count == 4 && "Incorrect Animation Count");
    test_gltf_animations(suite, &g);
    assert(g.buffer_count == 3 && "Incorrect Buffer Count");
    test_gltf_buffers(suite, &g);
    assert(g.buffer_view_count == 4 && "Incorrect Buffer View Count");
    test_gltf_buffer_views(suite, &g);
    assert(g.camera_count == 3 && "Incorrect Camera View Count");
    test_gltf_cameras(suite, &g);
    assert(g.image_count == 3 && "Incorrect Image Count");
    test_gltf_images(suite, &g);
    assert(g.material_count == 2 && "Incorrect Material Count");
    test_gltf_materials(suite, &g);
    assert(g.mesh_count == 2 && "Incorrect Mesh Count");
    test_gltf_meshes(suite, &g);
    assert(g.node_count == 7 && "Incorrect Node Count");
    test_gltf_nodes(suite, &g);
    assert(g.sampler_count == 3 && "Incorrect Sampler Count");
    test_gltf_samplers(suite, &g);
    assert(g.scene_count == 3 && "Incorrect Scene Count");
    test_gltf_scenes(suite, &g);
    assert(g.skin_count == 4 && "Incorrect Skin Count");
    test_gltf_skins(suite, &g);
    assert(g.texture_count == 4 && "Incorrect Texture Count");
    test_gltf_textures(suite, &g);

    deallocate(suite->alloc, mem_used.data);
}

static void test_gltf_accessors(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("gltf_accessor", false, false);

    gltf_accessor *accessor = &g->accessors[0];
    TEST_EQ("accessor[0].type", accessor->flags & GLTF_ACCESSOR_TYPE_BITS, GLTF_ACCESSOR_TYPE_SCALAR_BIT, false);
    TEST_EQ("accessor[0].component_type", accessor->flags & GLTF_ACCESSOR_COMPONENT_TYPE_BITS, GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT, false);
    TEST_EQ("accessor[0].buffer_view", accessor->buffer_view, 1, false);
    TEST_EQ("accessor[0].byte_offset", accessor->byte_offset, (uint64)100, false);
    TEST_EQ("accessor[0].count", accessor->count, 12636, false);
    TEST_EQ("accessor[0].max[0]", accessor->max_min.max[0], 4212, false);
    TEST_EQ("accessor[0].min[0]", accessor->max_min.min[0], 0, false);

    accessor = &g->accessors[1];
    TEST_EQ("accessor[1].type", accessor->flags & GLTF_ACCESSOR_TYPE_BITS, GLTF_ACCESSOR_TYPE_MAT4_BIT, false);
    TEST_EQ("accessor[1].component_type", accessor->flags & GLTF_ACCESSOR_COMPONENT_TYPE_BITS, GLTF_ACCESSOR_COMPONENT_TYPE_FLOAT_BIT, false);
    TEST_EQ("accessor[1].buffer_view", accessor->buffer_view, 2, false);
    TEST_EQ("accessor[1].byte_offset", accessor->byte_offset, (uint64)200, false);
    TEST_EQ("accessor[1].count", accessor->count, 2399, false);

    TEST_FEQ("accessor[1].max_min.max[0]",  accessor->max_min.max[0],  0.9971418380737304    , false);
    TEST_FEQ("accessor[1].max_min.max[1]",  accessor->max_min.max[1],  -4.371139894487897e-8 , false);
    TEST_FEQ("accessor[1].max_min.max[2]",  accessor->max_min.max[2],  0.9996265172958374    , false);
    TEST_FEQ("accessor[1].max_min.max[3]",  accessor->max_min.max[3],  0                     , false);
    TEST_FEQ("accessor[1].max_min.max[4]",  accessor->max_min.max[4],  4.3586464215650273e-8 , false);
    TEST_FEQ("accessor[1].max_min.max[5]",  accessor->max_min.max[5],  1                     , false);
    TEST_FEQ("accessor[1].max_min.max[6]",  accessor->max_min.max[6],  4.3695074225524884e-8 , false);
    TEST_FEQ("accessor[1].max_min.max[7]",  accessor->max_min.max[7],  0                     , false);
    TEST_FEQ("accessor[1].max_min.max[8]",  accessor->max_min.max[8],  0.9999366402626038    , false);
    TEST_FEQ("accessor[1].max_min.max[9]",  accessor->max_min.max[9],  0                     , false);
    TEST_FEQ("accessor[1].max_min.max[10]", accessor->max_min.max[10], 0.9971418380737304    , false);
    TEST_FEQ("accessor[1].max_min.max[11]", accessor->max_min.max[11], 0                     , false);
    TEST_FEQ("accessor[1].max_min.max[12]", accessor->max_min.max[12], 1.1374080181121828    , false);
    TEST_FEQ("accessor[1].max_min.max[13]", accessor->max_min.max[13], 0.44450080394744873   , false);
    TEST_FEQ("accessor[1].max_min.max[14]", accessor->max_min.max[14], 1.0739599466323853    , false);
    TEST_FEQ("accessor[1].max_min.max[15]", accessor->max_min.max[15], 1                     , false);

    TEST_FEQ("accessor[1].min[0]",  accessor->max_min.min[0],  -0.9999089241027832    , false);
    TEST_FEQ("accessor[1].min[1]",  accessor->max_min.min[1],  -4.371139894487897e-8  , false);
    TEST_FEQ("accessor[1].min[2]",  accessor->max_min.min[2],  -0.9999366402626038    , false);
    TEST_FEQ("accessor[1].min[3]",  accessor->max_min.min[3],  0                      , false);
    TEST_FEQ("accessor[1].min[4]",  accessor->max_min.min[4],  -4.3707416352845037e-8 , false);
    TEST_FEQ("accessor[1].min[5]",  accessor->max_min.min[5],  1                      , false);
    TEST_FEQ("accessor[1].min[6]",  accessor->max_min.min[6],  -4.37086278282095e-8   , false);
    TEST_FEQ("accessor[1].min[7]",  accessor->max_min.min[7],  0                      , false);
    TEST_FEQ("accessor[1].min[8]",  accessor->max_min.min[8],  -0.9996265172958374    , false);
    TEST_FEQ("accessor[1].min[9]",  accessor->max_min.min[9],  0                      , false);
    TEST_FEQ("accessor[1].min[10]", accessor->max_min.min[10], -0.9999089241027832    , false);
    TEST_FEQ("accessor[1].min[11]", accessor->max_min.min[11], 0                      , false);
    TEST_FEQ("accessor[1].min[12]", accessor->max_min.min[12], -1.189831018447876     , false);
    TEST_FEQ("accessor[1].min[13]", accessor->max_min.min[13], -0.45450031757354736   , false);
    TEST_FEQ("accessor[1].min[14]", accessor->max_min.min[14], -1.058603048324585     , false);
    TEST_FEQ("accessor[1].min[15]", accessor->max_min.min[15], 1                      , false);

    accessor = &g->accessors[2];
    TEST_EQ("accessor[1].type", accessor->flags & GLTF_ACCESSOR_TYPE_BITS, GLTF_ACCESSOR_TYPE_VEC3_BIT, false);
    TEST_EQ("accessor[1].component_type", accessor->flags & GLTF_ACCESSOR_COMPONENT_TYPE_BITS, GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_INT_BIT, false);
    TEST_EQ("accessor[2].buffer_view", accessor->buffer_view, 3, false);
    TEST_EQ("accessor[2].byte_offset", accessor->byte_offset, (uint64)300, false);
    TEST_EQ("accessor[2].count", accessor->count, 12001, false);

    TEST_EQ("accessor[2].sparse_count", accessor->sparse.count, 10, false);
    TEST_EQ("accessor[2].indices_comp_type", (int)accessor->sparse.indices.component_type, GLTF_ACCESSOR_COMPONENT_TYPE_UNSIGNED_SHORT_BIT, false);
    TEST_EQ("accessor[2].indices_buffer_view", accessor->sparse.indices.buffer_view, 7, false);
    TEST_EQ("accessor[2].values_buffer_view", accessor->sparse.values.buffer_view, 4, false);
    TEST_EQ("accessor[2].indices_byte_offset", accessor->sparse.indices.byte_offset, (uint64)8888, false);
    TEST_EQ("accessor[2].values_byte_offset", accessor->sparse.values.byte_offset, (uint64)9999, false);

    END_TEST_MODULE();
}

static void test_gltf_animations(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Accessor", false, false);

    gltf_animation *animations = g->animations;

    gltf_animation *animation = &animations[0];
    TEST_EQ("animation[0].target_count", animation->target_count, 3, false);
    TEST_EQ("animation[0].sampler_count", animation->sampler_count, 3, false);

    TEST_EQ("animation[0].targets[0].sampler", animation->targets[0].samplers[1],  0, false);
    TEST_EQ("animation[0].targets[0].node",    animation->targets[0].node,  1, false);
    TEST_EQ("animation[0].targets[0].path",    animation->targets[0].path_mask, GLTF_ANIMATION_PATH_ROTATION_BIT, false);

    TEST_EQ("animation[0].targets[1].sampler", animation->targets[1].samplers[2],  1, false);
    TEST_EQ("animation[0].targets[1].node",    animation->targets[1].node,  2, false);
    TEST_EQ("animation[0].targets[1].path",    animation->targets[1].path_mask, GLTF_ANIMATION_PATH_SCALE_BIT, false);

    TEST_EQ("animation[0].targets[2].sampler", animation->targets[2].samplers[0],  2, false);
    TEST_EQ("animation[0].targets[2].node",    animation->targets[2].node,  3, false);
    TEST_EQ("animation[0].targets[2].path",    animation->targets[2].path_mask, GLTF_ANIMATION_PATH_TRANSLATION_BIT,false);

    TEST_EQ("animation[0].samplers[0].input",         animation->samplers[0].input,  888, false);
    TEST_EQ("animation[0].samplers[0].output",        animation->samplers[0].output, 5, false);
    TEST_EQ("animation[0].samplers[0].interpolation", animation->samplers[0].interpolation, GLTF_ANIMATION_INTERPOLATION_LINEAR, false);
    TEST_EQ("animation[0].samplers[1].input",         animation->samplers[1].input,  4, false);
    TEST_EQ("animation[0].samplers[1].output",        animation->samplers[1].output, 6, false);
    TEST_EQ("animation[0].samplers[1].interpolation", animation->samplers[1].interpolation, GLTF_ANIMATION_INTERPOLATION_CUBICSPLINE, false);
    TEST_EQ("animation[0].samplers[2].input",         animation->samplers[2].input,  4, false);
    TEST_EQ("animation[0].samplers[2].output",        animation->samplers[2].output, 7, false);
    TEST_EQ("animation[0].samplers[2].interpolation", animation->samplers[2].interpolation, GLTF_ANIMATION_INTERPOLATION_STEP, false);

    animation = &animations[1];
    TEST_EQ("animation[1].target_count", animation->target_count, 2, false);
    TEST_EQ("animation[1].sampler_count", animation->sampler_count, 2, false);

    TEST_EQ("animation[1].targets[0].sampler", animation->targets[0].samplers[1],  0, false);
    TEST_EQ("animation[1].targets[0].node",    animation->targets[0].node,  0, false);
    TEST_EQ("animation[1].targets[0].path",    animation->targets[0].path_mask, GLTF_ANIMATION_PATH_ROTATION_BIT, false);

    TEST_EQ("animation[1].targets[1].sampler", animation->targets[1].samplers[1],  1, false);
    TEST_EQ("animation[1].targets[1].node",    animation->targets[1].node,  1, false);
    TEST_EQ("animation[1].targets[1].path",    animation->targets[1].path_mask, GLTF_ANIMATION_PATH_ROTATION_BIT, false);

    TEST_EQ("animation[1].samplers[0].input",         animation->samplers[0].input,  0, false);
    TEST_EQ("animation[1].samplers[0].output",        animation->samplers[0].output, 1, false);
    TEST_EQ("animation[1].samplers[0].interpolation", animation->samplers[0].interpolation, GLTF_ANIMATION_INTERPOLATION_LINEAR, false);
    TEST_EQ("animation[1].samplers[1].input",         animation->samplers[1].input,  2, false);
    TEST_EQ("animation[1].samplers[1].output",        animation->samplers[1].output, 3, false);
    TEST_EQ("animation[1].samplers[1].interpolation", animation->samplers[1].interpolation, GLTF_ANIMATION_INTERPOLATION_LINEAR, false);

    animation = &animations[2];
    TEST_EQ("animation[2].target_count", animation->target_count, 2, false);
    TEST_EQ("animation[2].sampler_count", animation->sampler_count, 1, false);

    TEST_EQ("animation[2].targets[0].sampler", animation->targets[0].samplers[0],  1000, false);
    TEST_EQ("animation[2].targets[0].node",    animation->targets[0].node,  2000, false);
    TEST_EQ("animation[2].targets[0].path",    animation->targets[0].path_mask, GLTF_ANIMATION_PATH_TRANSLATION_BIT,false);

    TEST_EQ("animation[2].targets[1].sampler", animation->targets[1].samplers[3],  799, false);
    TEST_EQ("animation[2].targets[1].node",    animation->targets[1].node,  899, false);
    TEST_EQ("animation[2].targets[1].path",    animation->targets[1].path_mask, GLTF_ANIMATION_PATH_WEIGHTS_BIT, false);

    TEST_EQ("animation[2].samplers[0].input",         animation->samplers[0].input,  676, false);
    TEST_EQ("animation[2].samplers[0].output",        animation->samplers[0].output, 472, false);
    TEST_EQ("animation[2].samplers[0].interpolation", animation->samplers[0].interpolation, GLTF_ANIMATION_INTERPOLATION_STEP, false);

    animation = &animations[3];
    TEST_EQ("animation[3].target_count", animation->target_count, 1, false);
    TEST_EQ("animation[3].sampler_count", animation->sampler_count, 2, false);

    TEST_EQ("animation[3].targets[0].node",    animation->targets[0].node, 27, false);
    TEST_EQ("animation[3].targets[0].path",    animation->targets[0].path_mask, GLTF_ANIMATION_PATH_TRANSLATION_BIT | GLTF_ANIMATION_PATH_ROTATION_BIT | GLTF_ANIMATION_PATH_WEIGHTS_BIT, false);
    TEST_EQ("animation[3].targets[0].sampler", animation->targets[0].samplers[0],  4, false);
    TEST_EQ("animation[3].targets[0].sampler", animation->targets[0].samplers[1], 24, false);
    TEST_EQ("animation[3].targets[0].sampler", animation->targets[0].samplers[3], 31, false);

    TEST_EQ("animation[3].samplers[0].input",         animation->samplers[0].input,  999, false);
    TEST_EQ("animation[3].samplers[0].output",        animation->samplers[0].output, 753, false);
    TEST_EQ("animation[3].samplers[0].interpolation", animation->samplers[0].interpolation, GLTF_ANIMATION_INTERPOLATION_LINEAR, false);

    TEST_EQ("animation[3].samplers[1].input",         animation->samplers[1].input,  4, false);
    TEST_EQ("animation[3].samplers[1].output",        animation->samplers[1].output, 6, false);
    TEST_EQ("animation[3].samplers[1].interpolation", animation->samplers[1].interpolation, GLTF_ANIMATION_INTERPOLATION_LINEAR, false);

    END_TEST_MODULE();
}

static void test_gltf_buffers(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("gltf_buffer", false, false);

    gltf_buffer *buffer = g->buffers;
       TEST_EQ("buffers[0].byteLength", buffer->byte_length, (uint64)10001, false);
    TEST_STREQ("buffers[0].uri", buffer->uri.cstr, "duck1.bin", false);

    buffer = &g->buffers[1]; // (Gltf_Buffer*)((uint8*)buffer + buffer->stride);
    TEST_EQ("buffers[1].byteLength", buffer->byte_length, (uint64)10002, false);
    TEST_STREQ("buffers[1].uri", buffer->uri.cstr, "duck2.bin", false);

    buffer = &g->buffers[2]; // (Gltf_Buffer*)((uint8*)buffer + buffer->stride);
    TEST_EQ("buffers[2].byteLength", buffer->byte_length, (uint64)10003, false);
    TEST_STREQ("buffers[2].uri", buffer->uri.cstr,   "duck3.bin", false);

    END_TEST_MODULE();
}

static void test_gltf_buffer_views(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Buffer_Views", false, false);

    gltf_buffer_view *view = &g->buffer_views[0];// buffer_views;
    TEST_EQ("buffer_views[0].buffer",           view->buffer, 1, false);
    TEST_EQ("buffer_views[0].byte_offset", view->byte_offset, (uint64)2, false);
    TEST_EQ("buffer_views[0].byte_length", view->byte_length, (uint64)25272, false);
    TEST_EQ("buffer_views[0].byte_stride", view->byte_stride, 0, false);
    TEST_EQ("buffer_views[0].buffer_type", view->flags & GLTF_BUFFER_VIEW_BUFFER_TYPE_BITS, GLTF_BUFFER_VIEW_INDEX_BUFFER_BIT, false);

    view = &g->buffer_views[1]; // (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[1].buffer",           view->buffer, 6, false);
    TEST_EQ("buffer_views[1].byte_offset", view->byte_offset, (uint64)25272, false);
    TEST_EQ("buffer_views[1].byte_length", view->byte_length, (uint64)76768, false);
    TEST_EQ("buffer_views[1].byte_stride", view->byte_stride, 32, false);
    TEST_EQ("buffer_views[1].buffer_type", view->flags & GLTF_BUFFER_VIEW_BUFFER_TYPE_BITS, GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT, false);

    view = &g->buffer_views[2]; // (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[2].buffer",           view->buffer,  9999, false);
    TEST_EQ("buffer_views[2].byte_offset", view->byte_offset,  (uint64)6969, false);
    TEST_EQ("buffer_views[2].byte_length", view->byte_length,  (uint64)99907654, false);
    TEST_EQ("buffer_views[2].byte_stride", view->byte_stride,  0, false);
    TEST_EQ("buffer_views[2].buffer_type", view->flags & GLTF_BUFFER_VIEW_BUFFER_TYPE_BITS, GLTF_BUFFER_VIEW_VERTEX_BUFFER_BIT, false);

    view = &g->buffer_views[3]; // (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[3].buffer",           view->buffer, 9, false);
    TEST_EQ("buffer_views[3].byte_offset", view->byte_offset, (uint64)25272, false);
    TEST_EQ("buffer_views[3].byte_length", view->byte_length, (uint64)76768, false);
    TEST_EQ("buffer_views[3].byte_stride", view->byte_stride, 32, false);
    TEST_EQ("buffer_views[3].buffer_type", view->flags & GLTF_BUFFER_VIEW_BUFFER_TYPE_BITS, GLTF_BUFFER_VIEW_INDEX_BUFFER_BIT, false);

    END_TEST_MODULE();
}

static void test_gltf_cameras(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Camera", false, false);

    gltf_camera *camera = &g->cameras[0];
    TEST_FEQ("cameras[0].flags",        camera->flags & GLTF_CAMERA_TYPE_BITS, GLTF_CAMERA_PERSPECTIVE_BIT, false);
    TEST_FEQ("cameras[0].aspect_ratio", camera->perspective.aspect_ratio, 1.5, false);
    TEST_FEQ("cameras[0].yfov",         camera->perspective.yfov, 0.646464, false);
    TEST_FEQ("cameras[0].zfar",         camera->perspective.zfar, 99.8, false);
    TEST_FEQ("cameras[0].znear",        camera->perspective.znear, 0.01, false);

    camera = &g->cameras[1]; // (Gltf_Camera*)((uint8*)       camera + camera->stride);
    TEST_FEQ("cameras[1].flags",        camera->flags & GLTF_CAMERA_TYPE_BITS, GLTF_CAMERA_PERSPECTIVE_BIT, false);
    TEST_FEQ("cameras[1].aspect_ratio", camera->perspective.aspect_ratio, 1.9, false);
    TEST_FEQ("cameras[1].yfov",         camera->perspective.yfov, 0.797979,  false);
    TEST_FEQ("cameras[1].zfar",         camera->perspective.zfar, Max_f32,  false);
    TEST_FEQ("cameras[1].znear",        camera->perspective.znear, 0.02,  false);

    camera = &g->cameras[2]; // (Gltf_Camera*)((uint8*)       camera + camera->stride);
    TEST_FEQ("cameras[2].flags", camera->flags & GLTF_CAMERA_TYPE_BITS, GLTF_CAMERA_ORTHOGRAPHIC_BIT, false);
    TEST_FEQ("cameras[2].xmag",  camera->orthographic.xmag, 1.822, false);
    TEST_FEQ("cameras[2].ymag",  camera->orthographic.ymag, 0.489, false);
    TEST_FEQ("cameras[2].znear", camera->orthographic.zfar, 1.4, false);
    TEST_FEQ("cameras[2].znear", camera->orthographic.znear, 0.01, false);

    END_TEST_MODULE();
}

static void test_gltf_images(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Image", false, false);

    gltf_image *image = &g->images[0];
    TEST_STREQ("images[0].uri", image->uri.cstr, "duckCM.png", false);

    image = &g->images[1]; // (Gltf_Image*)((u8*)image + image->stride);
    TEST_EQ("images[1].flags", image->flags & GLTF_IMAGE_MIME_TYPE_BITS, GLTF_IMAGE_JPEG_BIT, false);
    TEST_EQ("images[1].bufferView", image->buffer_view, 14, false);
    TEST_PTREQ("images[1].uri.cstr", image->uri.cstr, NULL, false);
    TEST_EQ("images[1].uri.len", image->uri.len, 0, false);

    image = &g->images[2]; // (Gltf_Image*)((u8*)image + image->stride);
    TEST_STREQ("images[2].uri", image->uri.cstr, "duck_but_better.jpeg", false);

    END_TEST_MODULE();
}

static void test_gltf_materials(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Material", false, false);

    gltf_material *material = &g->materials[0];

    TEST_EQ("materials[0].base_color_texture",         material->base_color.texture,             1, false);
    TEST_EQ("materials[0].base_color_texcoord",             material->base_color.texcoord,         1, false);
    TEST_EQ("materials[0].metallic_roughness_texture", material->metallic_roughness.texture,     2, false);
    TEST_EQ("materials[0].metallic_roughness_texcoord",     material->metallic_roughness.texcoord, 1, false);
    TEST_EQ("materials[0].normal_texture",             material->normal.texture,                 3, false);
    TEST_EQ("materials[0].normal_texcoord",                 material->normal.texcoord,             1, false);

    TEST_FEQ("materials[0].metallic_factor",      material->uniforms.metallic_factor    , 1   , false);
    TEST_FEQ("materials[0].roughness_factor",     material->uniforms.roughness_factor   , 1   , false);
    TEST_FEQ("materials[0].normal_scale",         material->uniforms.normal_scale       , 2   , false);

    TEST_FEQ("materials[0].base_color_factor[0]", material->uniforms.base_color_factor[0] ,  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[1]", material->uniforms.base_color_factor[1] ,  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[2]", material->uniforms.base_color_factor[2] ,  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[3]", material->uniforms.base_color_factor[3] ,  1.0, false);

    TEST_FEQ("materials[0].emissive_factor[0]", material->uniforms.emissive_factor[0] ,  0.2, false);
    TEST_FEQ("materials[0].emissive_factor[1]", material->uniforms.emissive_factor[1] ,  0.1, false);
    TEST_FEQ("materials[0].emissive_factor[2]", material->uniforms.emissive_factor[2] ,  0.0, false);

    // Material[1]
    material = &g->materials[1]; // (Gltf_Material*)((u8*)material + material->stride);

    TEST_EQ("materials[1].base_color_texture",         material->base_color.texture,             3, false);
    TEST_EQ("materials[1].base_color_texcoord",             material->base_color.texcoord,         4, false);
    TEST_EQ("materials[1].metallic_roughness_texture", material->metallic_roughness.texture,     8, false);
    TEST_EQ("materials[1].metallic_roughness_texcoord",     material->metallic_roughness.texcoord, 8, false);
    TEST_EQ("materials[1].normal_texture",             material->normal.texture,                 12, false);
    TEST_EQ("materials[1].normal_texcoord",                 material->normal.texcoord,             11, false);
    TEST_EQ("materials[1].emissive_texture",           material->emissive.texture,               3, false);
    TEST_EQ("materials[1].emissive_texcoord",               material->emissive.texcoord,           56070, false);
    TEST_EQ("materials[1].occlusion_texture",          material->occlusion.texture,              79, false);
    TEST_EQ("materials[1].occlusion_texcoord",              material->occlusion.texcoord,          9906, false);

    TEST_FEQ("materials[1].metallic_factor",      material->uniforms.metallic_factor    , 5.0   , false);
    TEST_FEQ("materials[1].roughness_factor",     material->uniforms.roughness_factor   , 6.0   , false);
    TEST_FEQ("materials[1].normal_scale",         material->uniforms.normal_scale       , 1.0   , false);
    TEST_FEQ("materials[1].occlusion_strength",   material->uniforms.occlusion_strength , 0.679 , false);

    TEST_FEQ("materials[1].base_color_factor[0]", material->uniforms.base_color_factor[0] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[1]", material->uniforms.base_color_factor[1] ,  4.5, false);
    TEST_FEQ("materials[1].base_color_factor[2]", material->uniforms.base_color_factor[2] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[3]", material->uniforms.base_color_factor[3] ,  1.0, false);

    TEST_FEQ("materials[1].emissive_factor[0]", material->uniforms.emissive_factor[0] , 11.2, false);
    TEST_FEQ("materials[1].emissive_factor[1]", material->uniforms.emissive_factor[1] ,  0.1, false);
    TEST_FEQ("materials[1].emissive_factor[2]", material->uniforms.emissive_factor[2] ,  0.0, false);

    END_TEST_MODULE();
}

static void test_gltf_meshes(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("gltf_mesh", false, false);

    gltf_mesh *mesh = &g->meshes[0];
    TEST_EQ("mesh[0].weight_count", mesh->weight_count, 2, false);
    TEST_EQ("mesh[0].primitive_count", mesh->primitive_count, 2, false);

    TEST_FEQ("mesh[0].weights[0]", mesh->weights[0], 0, false);
    TEST_FEQ("mesh[0].weights[1]", mesh->weights[1], 0.5, false);

    gltf_mesh_primitive *prim = &mesh->primitives[0];
    TEST_EQ("mesh[0].primitives[0]", prim->indices, 21, false);
    TEST_EQ("mesh[0].primitives[0]", prim->material, 3, false);
    TEST_EQ("mesh[0].primitives[0]", prim->topology, 1, false);
    TEST_EQ("mesh[0].primitives[0]", prim->attribute_count, 5, false);
    TEST_EQ("mesh[0].primitives[0]", prim->target_count, 0, false);
    gltf_mesh_primitive_attribute *attr = &prim->attributes[1];
    TEST_EQ("mesh[0].primitives[0].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[0].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[0].primitives[0].attributes[1]", attr->accessor, 23, false);
    attr = &prim->attributes[0];
    TEST_EQ("mesh[0].primitives[0].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[0].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[0].primitives[0].attributes[0]", attr->accessor, 22, false);
    attr = &prim->attributes[2];
    TEST_EQ("mesh[0].primitives[0].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[0].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[0].primitives[0].attributes[2]", attr->accessor, 24, false);
    attr = &prim->attributes[3];
    TEST_EQ("mesh[0].primitives[0].attributes[3]", attr->n, 0, false);
    TEST_EQ("mesh[0].primitives[0].attributes[3]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[0].primitives[0].attributes[3]", attr->accessor, 25, false);
    attr = &prim->attributes[4];
    TEST_EQ("mesh[0].primitives[0].attributes[4]", attr->n, 1, false);
    TEST_EQ("mesh[0].primitives[0].attributes[4]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[0].primitives[0].attributes[4]", attr->accessor, 25, false);

    prim = &mesh->primitives[1];
    TEST_EQ("mesh[0].primitives[1]", prim->indices, 31, false);
    TEST_EQ("mesh[0].primitives[1]", prim->material, 33, false);
    TEST_EQ("mesh[0].primitives[1]", prim->topology, 3, false);
    TEST_EQ("mesh[0].primitives[1]", prim->attribute_count, 5, false);
    TEST_EQ("mesh[0].primitives[1]", prim->target_count, 2, false);
    attr = &prim->attributes[1];
    TEST_EQ("mesh[0].primitives[1].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[0].primitives[1].attributes[1]", attr->accessor, 33, false);
    attr = &prim->attributes[0];
    TEST_EQ("mesh[0].primitives[1].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[0].primitives[1].attributes[0]", attr->accessor, 32, false);
    attr = &prim->attributes[2];
    TEST_EQ("mesh[0].primitives[1].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[0].primitives[1].attributes[2]", attr->accessor, 34, false);
    attr = &prim->attributes[3];
    TEST_EQ("mesh[0].primitives[1].attributes[3]", attr->n, 0, false);
    TEST_EQ("mesh[0].primitives[1].attributes[3]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[0].primitives[1].attributes[3]", attr->accessor, 35, false);
    attr = &prim->attributes[4];
    TEST_EQ("mesh[0].primitives[1].attributes[4]", attr->n, 1, false);
    TEST_EQ("mesh[0].primitives[1].attributes[4]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[0].primitives[1].attributes[4]", attr->accessor, 35, false);

    gltf_mesh_primitive_morph_target *target = &prim->morph_targets[0];
    TEST_EQ("mesh[0].primitives[1].targets[0].attribute_count", target->attribute_count, 3, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[0]", attr->accessor, 33, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[1]", attr->accessor, 32, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[0].primitives[1].targets[0].attributes[2]", attr->accessor, 34, false);

    target = &prim->morph_targets[1];
    TEST_EQ("mesh[0].primitives[1].targets[1]", target->attribute_count, 3, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[0]", attr->accessor, 43, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[1]", attr->n, 0, false);
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR, false);
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[1]", attr->accessor, 2, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[2]", attr->n, 1, false);
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR, false);
    // TEST_EQ("mesh[0].primitives[1].targets[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR_ALPHA, false); // outdated test, but may revert to this behaviour
    TEST_EQ("mesh[0].primitives[1].targets[1].attributes[2]", attr->accessor, 3, false);

    mesh = &g->meshes[1];
    TEST_EQ("mesh[1].weight_count", mesh->weight_count, 2, false);
    TEST_EQ("mesh[1].primitive_count", mesh->primitive_count, 3, false);

    TEST_FEQ("mesh[1].weights[0]", mesh->weights[0], 0, false);
    TEST_FEQ("mesh[1].weights[1]", mesh->weights[1], 0.5, false);

    prim = &mesh->primitives[0];
    TEST_EQ("mesh[1].primitives[0]", prim->indices, 11, false);
    TEST_EQ("mesh[1].primitives[0]", prim->material, 13, false);
    TEST_EQ("mesh[1].primitives[0]", prim->topology, 2, false);
    TEST_EQ("mesh[1].primitives[0]", prim->attribute_count, 4, false);
    TEST_EQ("mesh[1].primitives[0]", prim->target_count, 0, false);
    attr = &prim->attributes[1];
    TEST_EQ("mesh[1].primitives[0].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[0].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[0].attributes[0]", attr->accessor, 13, false);
    attr = &prim->attributes[0];
    TEST_EQ("mesh[1].primitives[0].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[0].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[0].attributes[1]", attr->accessor, 12, false);
    attr = &prim->attributes[2];
    TEST_EQ("mesh[1].primitives[0].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[0].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[1].primitives[0].attributes[2]", attr->accessor, 14, false);
    attr = &prim->attributes[3];
    TEST_EQ("mesh[1].primitives[0].attributes[3]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[0].attributes[3]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[1].primitives[0].attributes[3]", attr->accessor, 15, false);

    prim = &mesh->primitives[1];
    TEST_EQ("mesh[1].primitives[1]", prim->indices, 11, false);
    TEST_EQ("mesh[1].primitives[1]", prim->material, 13, false);
    TEST_EQ("mesh[1].primitives[1]", prim->topology, 3, false);
    TEST_EQ("mesh[1].primitives[1]", prim->attribute_count, 6, false);
    TEST_EQ("mesh[1].primitives[1]", prim->target_count, 2, false);
    attr = &prim->attributes[1];
    TEST_EQ("mesh[1].primitives[1].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[1].attributes[0]", attr->accessor, 13, false);
    attr = &prim->attributes[0];
    TEST_EQ("mesh[1].primitives[1].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[1].attributes[1]", attr->accessor, 12, false);
    attr = &prim->attributes[2];
    TEST_EQ("mesh[1].primitives[1].attributes[2]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS, false);
    TEST_EQ("mesh[1].primitives[1].attributes[2]", attr->accessor, 14, false);
    attr = &prim->attributes[4];
    TEST_EQ("mesh[1].primitives[1].attributes[3]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[1].attributes[3]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS, false);
    TEST_EQ("mesh[1].primitives[1].attributes[3]", attr->accessor, 15, false);
    attr = &prim->attributes[3];
    TEST_EQ("mesh[1].primitives[1].attributes[4]", attr->n, 1, false);
    TEST_EQ("mesh[1].primitives[1].attributes[4]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS, false);
    TEST_EQ("mesh[1].primitives[1].attributes[4]", attr->accessor, 14, false);
    attr = &prim->attributes[5];
    TEST_EQ("mesh[1].primitives[1].attributes[5]", attr->n, 1, false);
    TEST_EQ("mesh[1].primitives[1].attributes[5]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS, false);
    TEST_EQ("mesh[1].primitives[1].attributes[5]", attr->accessor, 15, false);

    target = &prim->morph_targets[0];
    TEST_EQ("mesh[1].primitives[1].targets[0].attribute_count", target->attribute_count, 3, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[0]", attr->accessor, 13, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[1]", attr->accessor, 12, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[1].primitives[1].targets[0].attributes[2]", attr->accessor, 14, false);

    target = &prim->morph_targets[1];
    TEST_EQ("mesh[1].primitives[1].targets[1]", target->attribute_count, 3, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[0]", attr->accessor, 23, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[1]", attr->accessor, 22, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[1].primitives[1].targets[1].attributes[2]", attr->accessor, 24, false);

    prim = &mesh->primitives[2];
    TEST_EQ("mesh[1].primitives[2]", prim->indices, 1, false);
    TEST_EQ("mesh[1].primitives[2]", prim->material, 3, false);
    TEST_EQ("mesh[1].primitives[2]", prim->topology, 0, false);
    TEST_EQ("mesh[1].primitives[2]", prim->attribute_count, 5, false);
    TEST_EQ("mesh[1].primitives[2]", prim->target_count, 2, false);
    attr = &prim->attributes[1];
    TEST_EQ("mesh[1].primitives[2].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[2].attributes[0]", attr->accessor, 3, false);
    attr = &prim->attributes[0];
    TEST_EQ("mesh[1].primitives[2].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[2].attributes[1]", attr->accessor, 2, false);
    attr = &prim->attributes[2];
    TEST_EQ("mesh[1].primitives[2].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[1].primitives[2].attributes[2]", attr->accessor, 4, false);
    attr = &prim->attributes[3];
    TEST_EQ("mesh[1].primitives[2].attributes[3]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[2].attributes[3]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[1].primitives[2].attributes[3]", attr->accessor, 5, false);
    attr = &prim->attributes[4];
    TEST_EQ("mesh[1].primitives[2].attributes[4]", attr->n, 1, false);
    TEST_EQ("mesh[1].primitives[2].attributes[4]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEXCOORD, false);
    TEST_EQ("mesh[1].primitives[2].attributes[4]", attr->accessor, 5, false);

    target = &prim->morph_targets[0];
    TEST_EQ("mesh[0].primitives[2].targets[0].attribute_count", target->attribute_count, 3, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[0]", attr->accessor, 3, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[1]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[1]", attr->accessor, 2, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[2]", attr->n, 0, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS, false);
    TEST_EQ("mesh[1].primitives[2].targets[0].attributes[2]", attr->accessor, 4, false);

    target = &prim->morph_targets[1];
    TEST_EQ("mesh[1].primitives[2].targets[1]", target->attribute_count, 3, false);
    attr = &target->attributes[1];
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[0]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[0]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[0]", attr->accessor, 9, false);
    attr = &target->attributes[0];
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[1]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[1]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[1]", attr->accessor, 7, false);
    attr = &target->attributes[2];
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[2]", attr->n, Max_u32, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[2]", attr->type, GLTF_MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("mesh[1].primitives[2].targets[1].attributes[2]", attr->accessor, 6, false);

    END_TEST_MODULE();
}

static void test_gltf_nodes(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Node", false, false);

    // @Todo shuffle the nodes around in the test file

    gltf_node *node = &g->nodes[0];
    TEST_EQ("nodes[0].camera", node->camera, 1, false);

    TEST_FEQ("nodes[0].matrix[0]",  node->mat.m[0],  -0.99975    , false);
    TEST_FEQ("nodes[0].matrix[1]",  node->mat.m[1],   0.00167596 , false);
    TEST_FEQ("nodes[0].matrix[2]",  node->mat.m[2],  -0.0223165  , false);
    TEST_FEQ("nodes[0].matrix[3]",  node->mat.m[3],  -0.0115543  , false);
    TEST_FEQ("nodes[0].matrix[4]",  node->mat.m[4],  -0.00679829 , false);
    TEST_FEQ("nodes[0].matrix[5]",  node->mat.m[5],   0.927325   , false);
    TEST_FEQ("nodes[0].matrix[6]",  node->mat.m[6],   0.374196   , false);
    TEST_FEQ("nodes[0].matrix[7]",  node->mat.m[7],   0.194711   , false);
    TEST_FEQ("nodes[0].matrix[8]",  node->mat.m[8],   0.0213218  , false);
    TEST_FEQ("nodes[0].matrix[9]",  node->mat.m[9],   0.374254   , false);
    TEST_FEQ("nodes[0].matrix[10]", node->mat.m[10], -0.927081   , false);
    TEST_FEQ("nodes[0].matrix[11]", node->mat.m[11], -0.478297   , false);
    TEST_FEQ("nodes[0].matrix[12]", node->mat.m[12],  0          , false);
    TEST_FEQ("nodes[0].matrix[13]", node->mat.m[13],  0          , false);
    TEST_FEQ("nodes[0].matrix[14]", node->mat.m[14],  0          , false);
    TEST_FEQ("nodes[0].matrix[15]", node->mat.m[15],  1          , false);

    TEST_FEQ("nodes[0].weights[0]", node->weights[0], 0.5, false);
    TEST_FEQ("nodes[0].weights[1]", node->weights[1], 0.6, false);
    TEST_FEQ("nodes[0].weights[2]", node->weights[2], 0.7, false);
    TEST_FEQ("nodes[0].weights[3]", node->weights[3], 0.8, false);

    node = &g->nodes[1]; // (Gltf_Node*)((u8*)node + node->stride);
    TEST_FEQ("nodes[1].rotation.x", node->trs.r.x, 0, false);
    TEST_FEQ("nodes[1].rotation.y", node->trs.r.y, 0, false);
    TEST_FEQ("nodes[1].rotation.z", node->trs.r.z, 0, false);
    TEST_FEQ("nodes[1].rotation.w", node->trs.r.w, 1, false);

    TEST_FEQ("nodes[1].translation.x", node->trs.t.x, -17.7082, false);
    TEST_FEQ("nodes[1].translation.y", node->trs.t.y, -11.4156, false);
    TEST_FEQ("nodes[1].translation.z", node->trs.t.z,   2.0922, false);

    TEST_FEQ("nodes[1].scale.x", node->trs.s.x, 1, false);
    TEST_FEQ("nodes[1].scale.y", node->trs.s.y, 1, false);
    TEST_FEQ("nodes[1].scale.z", node->trs.s.z, 1, false);

    node = &g->nodes[2]; // (Gltf_Node*)((u8*)node + node->stride);
    TEST_EQ("nodes[2].children[0]", node->children[0], 1, false);
    TEST_EQ("nodes[2].children[1]", node->children[1], 2, false);
    TEST_EQ("nodes[2].children[2]", node->children[2], 3, false);
    TEST_EQ("nodes[2].children[3]", node->children[3], 4, false);

    END_TEST_MODULE();
}

static void test_gltf_samplers(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Sampler", false, false);

    gltf_sampler *sampler = &g->samplers[0];
    TEST_EQ("samplers[0].mipmap_mode", sampler->mipmap_mode, 1, false);
    TEST_EQ("samplers[0].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[0].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[0].wrap_u",     sampler->wrap_u,     1, false);
    TEST_EQ("samplers[0].wrap_v",     sampler->wrap_v,     0, false);

    sampler = &g->samplers[1]; // (Gltf_Sampler*)((u8*)sampler + sampler->stride);
    TEST_EQ("samplers[1].mipmap_mode", sampler->mipmap_mode, 0, false);
    TEST_EQ("samplers[1].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[1].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[1].wrap_u",     sampler->wrap_u,     0, false);
    TEST_EQ("samplers[1].wrap_v",     sampler->wrap_v,     1, false);

    sampler = &g->samplers[2]; // (Gltf_Sampler*)((u8*)sampler + sampler->stride);
    TEST_EQ("samplers[2].mipmap_mode", sampler->mipmap_mode, 1, false);
    TEST_EQ("samplers[2].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[2].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[2].wrap_u",     sampler->wrap_u,     1, false);
    TEST_EQ("samplers[2].wrap_v",     sampler->wrap_v,     0, false);

    END_TEST_MODULE();
}

static void test_gltf_scenes(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Scene", false, false);

    TEST_EQ("scene", g->scene, 99, false);

    gltf_scene *scene = &g->scenes[0];
    TEST_STREQ("scenes[0].nodes[0]", scene->name.cstr, "first", false);
    TEST_EQ("scenes[0].nodes[1]", scene->nodes[1], 1, false);
    TEST_EQ("scenes[0].nodes[2]", scene->nodes[2], 2, false);
    TEST_EQ("scenes[0].nodes[3]", scene->nodes[3], 3, false);
    TEST_EQ("scenes[0].nodes[4]", scene->nodes[4], 4, false);

    scene = &g->scenes[1]; // (Gltf_Scene*)((u8*)scene + scene->stride);
    TEST_STREQ("scenes[1].nodes[0]", scene->name.cstr, "second", false);
    TEST_EQ("scenes[1].nodes[0]", scene->nodes[0], 5, false);
    TEST_EQ("scenes[1].nodes[1]", scene->nodes[1], 6, false);
    TEST_EQ("scenes[1].nodes[2]", scene->nodes[2], 7, false);
    TEST_EQ("scenes[1].nodes[3]", scene->nodes[3], 8, false);
    TEST_EQ("scenes[1].nodes[4]", scene->nodes[4], 9, false);

    scene = &g->scenes[2]; // (Gltf_Scene*)((u8*)scene + scene->stride);
    TEST_STREQ("scenes[2].nodes[0]", scene->name.cstr, "third", false);
    TEST_EQ("scenes[2].nodes[0]", scene->nodes[0], 10, false);
    TEST_EQ("scenes[2].nodes[1]", scene->nodes[1], 11, false);
    TEST_EQ("scenes[2].nodes[2]", scene->nodes[2], 12, false);
    TEST_EQ("scenes[2].nodes[3]", scene->nodes[3], 13, false);
    TEST_EQ("scenes[2].nodes[4]", scene->nodes[4], 14, false);

    END_TEST_MODULE();
}

static void test_gltf_skins(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Skin", false, false);

    gltf_skin *skin = &g->skins[0];
    TEST_EQ("skins[0].inverse_bind_matrices", skin->inverse_bind_matrices, 0, false);
    TEST_EQ("skins[0].skeleton",              skin->skeleton,              1, false);
    TEST_EQ("skins[0].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[0].joints[0]",             skin->joints[0],             1, false);
    TEST_EQ("skins[0].joints[1]",             skin->joints[1],             2, false);

    skin = &g->skins[1]; // (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[1].inverse_bind_matrices", skin->inverse_bind_matrices, 1, false);
    TEST_EQ("skins[1].skeleton",              skin->skeleton,              2, false);
    TEST_EQ("skins[1].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[1].joints[0]",             skin->joints[0],             3, false);
    TEST_EQ("skins[1].joints[1]",             skin->joints[1],             4, false);

    skin = &g->skins[2]; // (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[2].inverse_bind_matrices", skin->inverse_bind_matrices, 2, false);
    TEST_EQ("skins[2].skeleton",              skin->skeleton,              3, false);
    TEST_EQ("skins[2].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[2].joints[0]",             skin->joints[0],             5, false);
    TEST_EQ("skins[2].joints[1]",             skin->joints[1],             6, false);

    skin = &g->skins[3]; // (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[3].inverse_bind_matrices", skin->inverse_bind_matrices, 3, false);
    TEST_EQ("skins[3].skeleton",              skin->skeleton,              4, false);
    TEST_EQ("skins[3].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[3].joints[0]",             skin->joints[0],             7, false);
    TEST_EQ("skins[3].joints[1]",             skin->joints[1],             8, false);

    END_TEST_MODULE();
}

static void test_gltf_textures(test_suite *suite, gltf *g)
{
    BEGIN_TEST_MODULE("Gltf_Texture", false, false);

    gltf_texture *texture = &g->textures[0];
    TEST_EQ("textures[0].sampler", texture->sampler,       0, false);
    TEST_EQ("textures[0].source",  texture->source,  1, false);

    texture = &g->textures[1]; // (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[1].sampler", texture->sampler,       2, false);
    TEST_EQ("textures[1].source",  texture->source,  3, false);

    texture = &g->textures[2]; // (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[2].sampler", texture->sampler,       4, false);
    TEST_EQ("textures[2].source",  texture->source,  5, false);

    texture = &g->textures[3]; // (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[3].sampler", texture->sampler,       6, false);
    TEST_EQ("textures[3].source",  texture->source,  7, false);

    END_TEST_MODULE();
}
#endif

