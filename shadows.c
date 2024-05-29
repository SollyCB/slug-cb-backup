#include "shadows.h"

void scene_bounding_box(uint count, matrix *positions, gltf *models, struct box *bb)
{
    memset(bb, 0, sizeof(*bb));

    vector cmin = vector3(0,0,0);
    vector cmax = vector3(0,0,0);
    float minlen = 0;
    float maxlen = 0;

    for(uint i=0; i < count; ++i) {
        for(uint j=0; j < models[i].mesh_count; ++j)
            for(uint k=0; k < models[i].meshes[j].primitive_count; ++k) {
                gltf_accessor *a = &models[i].accessors[models[i].meshes[j].primitives[k].attributes[0].accessor];
                log_print_error_if(feq(a->max_min.max[0], Max_f32) || feq(a->max_min.max[0], Max_f32),
                                   "gltf mesh primitve position attribute has invalid max_min");

                vector min = vector3(a->max_min.min[0], a->max_min.min[1], a->max_min.min[2]);
                min = mul_matrix_vector(&positions[i], min);

                if (vector_len(min) > minlen) {
                    minlen = vector_len(min);
                    cmin = min;
                }

                vector max = vector3(a->max_min.max[0], a->max_min.max[1], a->max_min.max[2]);
                max = mul_matrix_vector(&positions[i], max);

                if (vector_len(max) > maxlen) {
                    maxlen = vector_len(max);
                    cmax = max;
                }
            }
    }

    bb->p[0] = vector3(cmin.x, cmin.y, cmin.z);
    bb->p[1] = vector3(cmin.x, cmin.y, cmax.z);
    bb->p[2] = vector3(cmax.x, cmin.y, cmax.z);
    bb->p[3] = vector3(cmax.x, cmin.y, cmin.z);
    bb->p[4] = vector3(cmin.x, cmax.y, cmin.z);
    bb->p[5] = vector3(cmin.x, cmax.y, cmax.z);
    bb->p[6] = vector3(cmax.x, cmax.y, cmax.z);
    bb->p[7] = vector3(cmax.x, cmax.y, cmin.z);
}

// The four corners of a frustum
void get_frustum(float fov, float near, float far, struct frustum *ret)
{
    float e = focal_length(fov);
    float a = fov / 2;

    float den_x = sqrtf(powf(e, 2) + 1);
    float den_y = sqrtf(powf(e, 2) + powf(a, 2));
    float x = e / den_x;
    float y = e / den_y;
    float zx = -1 / den_x;
    float zy = -a / den_y;

    vector pn = vector4( 0,  0, -1, -near);
    vector pf = vector4( 0,  0,  1,  far);
    vector pl = vector4( x,  0, zx,  0);
    vector pr = vector4(-x,  0, zx,  0);
    vector pb = vector4( 0,  y, zy,  0);
    vector pt = vector4( 0, -y, zy,  0); // @Note +y is up

    ret->tl_near = intersect_three_planes(pn, pl, pt);
    ret->tr_near = intersect_three_planes(pn, pr, pt);
    ret->bl_near = intersect_three_planes(pn, pl, pb);
    ret->br_near = intersect_three_planes(pn, pr, pb);

    ret->tl_far = intersect_three_planes(pf, pl, pt);
    ret->tr_far = intersect_three_planes(pf, pr, pt);
    ret->bl_far = intersect_three_planes(pf, pl, pb);
    ret->br_far = intersect_three_planes(pf, pr, pb);

    ret->tl_near.w = 1;
    ret->tr_near.w = 1;
    ret->bl_near.w = 1;
    ret->br_near.w = 1;

    ret->tl_far.w = 1;
    ret->tr_far.w = 1;
    ret->bl_far.w = 1;
    ret->br_far.w = 1;
}

void minmax_frustum_points(struct frustum *f, matrix *space, struct minmax *minmax_x, struct minmax *minmax_y)
{
    vector p[8];
    vector *q = (vector*)f;
    for(uint i=0; i < 8; ++i)
        p[i] = mul_matrix_vector(space, q[i]);

    struct minmax x = {Max_f32,-Max_f32};
    struct minmax y = {Max_f32,-Max_f32};

    for(uint i=0; i < 8; ++i) {
        if (p[i].x < x.min)
            x.min = p[i].x;
        if (p[i].x > x.max)
            x.max = p[i].x;
        if (p[i].y < y.min)
            y.min = p[i].y;
        if (p[i].y > y.max)
            y.max = p[i].y;
    }
    assert(x.min < Max_f32 && x.max > -Max_f32 &&
           y.min < Max_f32 && y.max > -Max_f32);

    *minmax_x = x;
    *minmax_y = y;
}

// min max x and y, scene bounding box (credit dx-sdk-samples for the implementation)
struct minmax near_far(struct minmax x, struct minmax y, struct box *bb)
{
    /* uint SDK_VERSION_indices[] = { // Idk if this is best statically initialized, but probably.
        0,1,2,  1,2,3,
        4,5,6,  5,6,7,
        0,2,4,  2,4,6,
        1,3,5,  3,5,7,
        0,1,4,  1,4,5,
        2,3,6,  3,6,7,
    }; */

    // @Note These indices differ from the order used in the SDK example. I do not know
    // if theirs match my bounding box ordering. The SDK indices are above
    uint indices[] = {
        0,1,2,  2,3,0,
        0,1,5,  0,5,4,
        0,4,3,  3,7,4,
        1,5,2,  2,6,5,
        3,7,6,  6,2,3,
        4,5,6,  6,7,4,
    };

    float edges[] = {x.min,x.max,
                     y.min,y.max};

    float near = Max_f32;
    float far  = -Max_f32;

    struct {
        vector p[3];
    } triangles[16];

    #define cull(t, tc) do {t.p[0].w = 1;t = triangles[tc-1]; tc--;} while(0)

    // @DebugOnly Just for asserts
    #define uncull(t)   (t.p[0].w = 0)
    #define isculled(t) ((uint)t.p[0].w != 0)

    for(uint i=0; i < 12; ++i) {
        triangles[0].p[0] = bb->p[indices[i * 3 + 0]];
        triangles[0].p[1] = bb->p[indices[i * 3 + 1]];
        triangles[0].p[2] = bb->p[indices[i * 3 + 2]];
        uncull(triangles[0]);

        uint tc = 1; // triangle count

        for(uint j=0; j < 4; ++j) {
            for(uint k=0; k < tc; ++k) {
                assert(!isculled(triangles[k]));

                uint pc = 0; // pass count - points inside plane
                {
                    uint8 passes[3];
                    memset(passes, 0, sizeof(passes));
                    for(uint l=0; l < 3; ++l) {
                        // while j < 2, check x, else check y; if j % 2 check max, else check min
                        passes[l] += vector_i(triangles[k].p[l], (j >> 1)) > edges[j] && !(j & 1);
                        passes[l] += vector_i(triangles[k].p[l], (j >> 1)) < edges[j] &&  (j & 1);
                        assert(passes[l] < 2);
                        pc += passes[l];
                    }

                    struct pair_uint shfl[] = {
                        {0, 1},
                        {1, 2},
                        {0, 1},
                    };
                    for(uint l=0; l < 3; ++l) {
                        vector tmp = triangles[k].p[shfl[l].a];

                        set_vector_if(&triangles[k].p[shfl[l].a], &triangles[k].p[shfl[l].b],
                                      !passes[shfl[l].a] && passes[shfl[l].b]);
                        set_vector_if(&triangles[k].p[shfl[l].b], &tmp,
                                      !passes[shfl[l].a] && passes[shfl[l].b]);

                        passes[shfl[l].a] = true;
                        passes[shfl[l].b] = false;
                    }
                }

                enum {
                    ALL_OUT, ONE_IN, TWO_IN, ALL_IN
                };
                switch(pc) {
                case ALL_OUT:
                {
                    cull(triangles[k], tc);
                    break;
                }
                case ONE_IN:
                {
                    vector p,q,r;
                    p = triangles[k].p[0];
                    q = sub_vector(triangles[k].p[1], p);
                    r = sub_vector(triangles[k].p[2], p);

                    float s = edges[j] - vector_i(p, j >> 1); // if j < 2, check x, else y
                    q = add_vector(p, scale_vector(q, s / vector_i(q, j >> 1)));
                    r = add_vector(p, scale_vector(r, s / vector_i(r, j >> 1)));

                    triangles[k].p[1] = q;
                    triangles[k].p[2] = r;
                    uncull(triangles[k]);

                    break;
                }
                case TWO_IN:
                {
                    triangles[tc] = triangles[k+1];
                    memcpy(&triangles[k+1], &triangles[k], sizeof(*triangles));

                    vector p,q,r;
                    r = triangles[k].p[2];
                    p = sub_vector(triangles[k].p[0], r);
                    q = sub_vector(triangles[k].p[1], r);

                    float s = edges[j] - vector_i(r, j >> 1);
                    {
                        float scale = s / vector_i(p, j >> 1);
                        p = add_vector(p, scale_vector(p, scale));

                        triangles[k+1].p[0] = triangles[k].p[0];
                        triangles[k+1].p[1] = triangles[k].p[1];
                        triangles[k+1].p[2] = p;
                    }
                    {
                        float scale = s / vector_i(q, j >> 1);
                        q = add_vector(q, scale_vector(q, scale));

                        // note the different point indices
                        triangles[k].p[0] = triangles[k+1].p[1];
                        triangles[k].p[1] = triangles[k+1].p[2];
                        triangles[k].p[2] = q;
                    }

                    uncull(triangles[k+0]);
                    uncull(triangles[k+1]);
                    tc++;
                    k++;

                    break;
                }
                case ALL_IN:
                {
                    uncull(triangles[k]);
                    break;
                }
                default:
                    log_print_error("Invalid case");
                }
            }
        }
        for(uint j=0; j < tc; ++j) {
            assert(!isculled(triangles[j]));
            for(uint k=0; k < 3; ++k) {
                if (near > triangles[j].p[k].z)
                    near = triangles[j].p[k].z;
                if (far < triangles[j].p[k].z)
                    far = triangles[j].p[k].z;
            }
        }
    }
    return (struct minmax) {near, far};
}

