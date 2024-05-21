#include "math.h"

struct frustum {
    vector tl_near, tr_near, bl_near, br_near;
    vector tl_far,  tr_far,  bl_far,  br_far;
};

// The four corners of a frustum
static inline void get_frustum(float fov, float near, float far, struct frustum *ret)
{
    float e = focal_length(fov);
    float a = fov / 2;
    float b = atanf(a / e);

    float den_x = sqrtf(powf(e, 2) + 1);
    float den_y = sqrtf(powf(e, 2) + powf(a, 2));
    float x = e / den_x;
    float y = e / den_y;
    float zx = -1 / den_x;
    float zy = -a / den_y;

    vector pn = vector4( 0, 0, -1, -near);
    vector pf = vector4( 0, 0,  1,  far);
    vector pl = vector4( x, 0, zx,  0);
    vector pr = vector4(-x, 0, zx,  0);
    vector pb = vector4( y, 0, zy,  0);
    vector pt = vector4(-y, 0, zy,  0); // @Note +y is up

    ret->tl_near = intersect_three_planes(pn, pl, pt);
    ret->tr_near = intersect_three_planes(pn, pr, pt);
    ret->bl_near = intersect_three_planes(pn, pl, pb);
    ret->br_near = intersect_three_planes(pn, pr, pb);

    ret->tl_far = intersect_three_planes(pf, pl, pt);
    ret->tr_far = intersect_three_planes(pf, pr, pt);
    ret->bl_far = intersect_three_planes(pf, pl, pb);
    ret->br_far = intersect_three_planes(pf, pr, pb);
}

static inline void minmax_frustum_points(struct frustum *f, matrix *space,
                                         struct pair_float *minmax_x,
                                         struct pair_float *minmax_y)
{
    vector p[8];
    vector *q = (vector*)f;
    for(uint i=0; i < 8; ++i)
        p[i] = mul_matrix_vector(space, q[i]);

    struct pair_float x = {Max_f32,-Max_f32};
    struct pair_float y = {Max_f32,-Max_f32};

    for(uint i=0; i < 8; ++i) {
        if (p[i].x < x.a)
            x.a = p[i].x;
        if (p[i].x > x.b)
            x.b = p[i].x;
        if (p[i].y < y.a)
            y.a = p[i].y;
        if (p[i].y > y.b)
            y.b = p[i].y;
    }
    assert(x.a < Max_f32 && x.b > -Max_f32 &&
           y.a < Max_f32 && y.b > -Max_f32);

    *minmax_x = x;
    *minmax_y = y;
}

// min max x and y, scene bounding box (credit dx-sdk-samples for the implementation)
static struct pair_float near_far(struct pair_float minmax_x, struct pair_float minmax_y, vector bb[8])
{
    static uint indices[] = { // Idk if this is best statically initialized, but probably.
        0,1,2,  1,2,3,
        4,5,6,  5,6,7,
        0,2,4,  2,4,6,
        1,3,5,  3,5,7,
        0,1,4,  1,4,5,
        2,3,6,  3,6,7,
    };

    float edges[] = {minmax_x.a,minmax_x.b,
                     minmax_y.a,minmax_y.b};

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
        triangles[0].p[0] = bb[indices[i * 3 + 0]];
        triangles[0].p[1] = bb[indices[i * 3 + 1]];
        triangles[0].p[2] = bb[indices[i * 3 + 2]];
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
    return (struct pair_float) {near, far};
}

