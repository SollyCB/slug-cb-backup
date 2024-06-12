#include "shadows.h"
#include "math.h"

// The four corners of a frustum
void perspective_frustum(float fov, float ar, float near, float far, struct frustum *ret)
{
    float e = focal_length(fov);
    float a = ar;

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

void ortho_frustum(float l, float r, float b, float t, float n, float f, struct frustum *ret)
{
    vector pn = vector4( 0,  0, -1, dot(vector3( 0,  0,  1), vector3(0,  0,  n)));
    vector pf = vector4( 0,  0,  1, dot(vector3( 0,  0, -1), vector3(0,  0,  f)));
    vector pl = vector4( 1,  0,  0, dot(vector3(-1,  0,  0), vector3(l,  0,  0)));
    vector pr = vector4(-1,  0,  0, dot(vector3( 1,  0,  0), vector3(r,  0,  0)));
    vector pb = vector4( 0,  1,  0, dot(vector3( 0, -1,  0), vector3(0,  b,  0)));
    vector pt = vector4( 0, -1,  0, dot(vector3( 0,  1,  0), vector3(0,  t,  0)));

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
    assert(!feq(x.min, x.max));

    *minmax_x = x;
    *minmax_y = y;
}

#if 1
struct minmax near_far(struct minmax x, struct minmax y, struct box *b)
{
    vector planes[] = {
        vector4( 1, 0, 0, dot(vector3(-1, 0, 0), vector3(x.min,     0, 0))),
        vector4(-1, 0, 0, dot(vector3( 1, 0, 0), vector3(x.max,     0, 0))),
        vector4( 0, 1, 0, dot(vector3( 0,-1, 0), vector3(    0, y.min, 0))),
        vector4( 0,-1, 0, dot(vector3( 0, 1, 0), vector3(    0, y.max, 0))),
    };

    struct pair_uint idx[] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };

    struct minmax nf = {Max_f32, -Max_f32};

    vector p;
    for(uint i=0; i < carrlen(idx); ++i) {
        vector s = b->p[idx[i].a];
        vector v = normalize(sub_vector(b->p[idx[i].b], b->p[idx[i].a]));

        s.w = 1;
        v.w = 0;

        for(uint j=0; j < carrlen(planes); ++j) {
            if (intersect_line_plane(planes[j], s, v, &p) == INTERSECT) {
                if (b->p[idx[i].a].z < nf.min)
                    nf.min = b->p[idx[i].a].z;
                if (b->p[idx[i].a].z > nf.max)
                    nf.max = b->p[idx[i].a].z;
                if (b->p[idx[i].b].z < nf.min)
                    nf.min = b->p[idx[i].b].z;
                if (b->p[idx[i].b].z > nf.max)
                    nf.max = b->p[idx[i].b].z;
            }
        }
    }
    assert(nf.max > -Max_f32 && nf.min < Max_f32);
    return nf;
}
#else
struct minmax near_far(struct minmax x, struct minmax y, struct box *b)
{
    struct { vector l,q; } f_planes[] = {
        {.l = vector4( 1, 0, 0, dot(vector3(-1, 0, 0), vector3(x.min,     0, 0))), .q = vector3(x.min,     0, 0)},
        {.l = vector4(-1, 0, 0, dot(vector3( 1, 0, 0), vector3(x.max,     0, 0))), .q = vector3(x.max,     0, 0)},
        {.l = vector4( 0, 1, 0, dot(vector3( 0,-1, 0), vector3(    0, y.min, 0))), .q = vector3(    0, y.min, 0)},
        {.l = vector4( 0,-1, 0, dot(vector3( 0, 1, 0), vector3(    0, y.max, 0))), .q = vector3(    0, y.max, 0)},
    };

    struct { vector l,q; } b_planes[] = {
        {
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[1], b->p[0])), sub_vector(b->p[4], b->p[0]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[1], b->p[0])), sub_vector(b->p[4], b->p[0]))), b->p[0])),
            .q = b->p[0],
        }, // left
        { 
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[4], b->p[0])), sub_vector(b->p[3], b->p[0]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[4], b->p[0])), sub_vector(b->p[3], b->p[0]))), b->p[0])),
            .q = b->p[0], }, // near
        {
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[3], b->p[0])), sub_vector(b->p[1], b->p[0]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[3], b->p[0])), sub_vector(b->p[1], b->p[0]))), b->p[0])),
            .q = b->p[0],
        }, // bottom
        {
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[7], b->p[6])), sub_vector(b->p[5], b->p[6]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[7], b->p[6])), sub_vector(b->p[5], b->p[6]))), b->p[6])),
            .q = b->p[6],
        }, // top
        {
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[2], b->p[6])), sub_vector(b->p[7], b->p[6]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[2], b->p[6])), sub_vector(b->p[7], b->p[6]))), b->p[6])),
            .q = b->p[6],
        }, // right
        {
            .l = vector3_w(    normalize(cross(normalize(sub_vector(b->p[5], b->p[6])), sub_vector(b->p[2], b->p[6]))),
                           dot(normalize(cross(normalize(sub_vector(b->p[5], b->p[6])), sub_vector(b->p[2], b->p[6]))), b->p[6])),
            .q = b->p[6], 
        }, // far
    };

    struct minmax nf = {Max_f32, -Max_f32};

    for(uint i=0; i < carrlen(f_planes); ++i) {
        for(uint j=0; j < carrlen(b_planes); ++j) {
            vector p = intersect_two_planes_point(f_planes[i].l, b_planes[i].l, f_planes[i].q, b_planes[i].q);
            if (p.z < nf.min)
                nf.min = p.z;
            if (p.z > nf.max)
                nf.max = p.z;
        }
    }

    assert(nf.max > -Max_f32 && nf.min < Max_f32);
    return nf;
}
#endif

#if 0
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

    float near =  Max_f32;
    float far  = -Max_f32;

    struct {
        vector p[3];
    } triangles[16];

    // @DebugOnly Just for asserts
    #define uncull(t)   (t.p[0].w = 0)
    #define isculled(t) ((uint)t.p[0].w != 0)

    for(uint i=0; i < 12; ++i) {
        triangles[0].p[0] = bb->p[indices[i * 3 + 0]];
        triangles[0].p[1] = bb->p[indices[i * 3 + 1]];
        triangles[0].p[2] = bb->p[indices[i * 3 + 2]];
        uncull(triangles[0]);

        uint tc = 1; // triangle count

        // Using a variable external to the macro (tc) but that is fine here.
        #define cull(t) do {t.p[0].w = 1;t = triangles[tc-1]; tc--;} while(0)

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
                    assert(pc <= 3);

                    struct pair_uint shfl[] = {
                        {0, 1},
                        {1, 2},
                        {0, 1},
                    };
                    for(uint l=0; l < 3; ++l) {
                        vector tmp = triangles[k].p[shfl[l].a];

                        if (!passes[shfl[l].a] && passes[shfl[l].b]) {
                            triangles[k].p[shfl[l].a] = triangles[k].p[shfl[l].b];
                            triangles[k].p[shfl[l].b] = tmp;
                        }

                        passes[shfl[l].a] = true;
                        passes[shfl[l].b] = false;
                    }
                }

                enum { ALL_OUT, ONE_IN, TWO_IN, ALL_IN };

                switch(pc) {
                case ALL_OUT:
                {
                    cull(triangles[k]);
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

                    triangles[k].p[1] = r;
                    triangles[k].p[2] = q;
                    uncull(triangles[k]);

                    break;
                }
                case TWO_IN:
                {
                    triangles[tc] = triangles[k+1];

                    vector p,q,r;
                    r = triangles[k].p[2];
                    p = sub_vector(triangles[k].p[0], r);
                    q = sub_vector(triangles[k].p[1], r);

                    float s = edges[j] - vector_i(r, j >> 1);
                    {
                        p = add_vector(p, scale_vector(p, s / vector_i(p, j >> 1)));

                        triangles[k+1].p[0] = triangles[k].p[0];
                        triangles[k+1].p[1] = triangles[k].p[1];
                        triangles[k+1].p[2] = p;
                    }
                    {
                        q = add_vector(q, scale_vector(q, s / vector_i(q, j >> 1)));

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
#endif

