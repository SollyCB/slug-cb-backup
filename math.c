#include "math.h"

static inline vector vector2_ua(float *from)
{
    return vector2(from[0], from[1]);
}

static inline vector vector3_ua(float *from)
{
    return vector3(from[0], from[1], from[2]);
}

static inline void store_vector3_ua(float *to, vector from)
{
    memcpy(to, &from.x, sizeof(from.x) * 3);
}

void calc_vertex_normals(
    uint   index_count,
    uint  *indices,
    uint   vertex_count,
    float *vertices,
    float *ret_normals)
{
    smemset(ret_normals, 0, *ret_normals, vertex_count*3);
    vector p[3];
    uint i,j;
    for(i = 0; i < index_count; i += 3) {
        for(j = 0; j < 3; ++j)
            p[j] = vector3_ua(&vertices[indices[i+j] * 3]);

        p[1] = sub_vector(p[0], p[1]);
        p[2] = sub_vector(p[0], p[2]);
        p[1] = cross(p[2], p[0]);

        for(j = 0; j < 3; ++j)
            store_vector3_ua(&ret_normals[indices[i+j] * 3],
                    add_vector(vector3_ua(&ret_normals[indices[i+j] * 3]), p[0]));
    }
    for(i = 0; i < vertex_count; i++)
        store_vector3_ua(&ret_normals[i*3],
                normalize(vector3_ua(&ret_normals[i*3])));
}

void calc_vertex_tangents(
    uint       index_count,
    uint      *indices,
    uint       vertex_count,
    float     *vertices,
    float     *normals,
    float     *texcoords,
    allocator *alloc,
    vector    *ret_tangents)
{
    vector *tan1 = sallocate(alloc, *tan1, 2 * vertex_count);
    vector *tan2 = tan1 + vertex_count;
    smemset(tan1, 0, *tan1, 2 * vertex_count);

    vector v3[3];
    vector v2[3];

    uint i,j;
    for (i = 0; i < index_count; i += 3) {
        for(j = 0; j < 3; ++j)
            v3[j] = vector3_ua(&vertices[indices[i+j]*3]);
        for(j = 0; j < 3; ++j)
            v2[j] = vector2_ua(&texcoords[indices[i+j]*2]);

        float x1 = v3[1].x - v3[0].x;
        float x2 = v3[2].x - v3[0].x;
        float y1 = v3[1].y - v3[0].y;
        float y2 = v3[2].y - v3[0].y;
        float z1 = v3[1].z - v3[0].z;
        float z2 = v3[2].z - v3[0].z;
        float s1 = v2[1].x - v2[0].x;
        float s2 = v2[2].x - v2[0].x;
        float t1 = v2[1].y - v2[0].y;
        float t2 = v2[2].y - v2[0].y;
        float r = 1.0 / (s1 * t2 - s2 * t1);

        v3[0] = (vector) {
            (t2 * x1 - t1 * x2) * r,
            (t2 * y1 - t1 * y2) * r,
            (t2 * z1 - t1 * z2) * r,
        };
        v3[1] = (vector) {
            (s1 * x2 - s2 * x1) * r,
            (s1 * y2 - s2 * y1) * r,
            (s1 * z2 - s2 * z1) * r,
        };

        for(j = 0; j < 3; ++j)
            tan1[indices[i+j]] = add_vector(v3[0], tan1[indices[i+j]]);
        for(j = 0; j < 3; ++j)
            tan2[indices[i+j]] = add_vector(v3[1], tan1[indices[i+j]]);
    }
    for(i = 0; i < vertex_count; i++) {
        v3[0] = vector3_ua(&normals[i*3]);
        ret_tangents[i] = gram_schmidt(v3[0], tan1[i]);
        ret_tangents[i].w = tangent_handedness(v3[0], tan1[i], tan2[i]);
    }
}

