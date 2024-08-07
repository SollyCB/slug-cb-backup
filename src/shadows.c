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

// split a frustum in to count sub-frusta (fit to cascade)
void partition_frustum_c(struct frustum *f, uint count, struct frustum *sf)
{
    float scale = 1.0 / count;
    
    // This function is a little jank, but this is the simplest way that jumped to mind. The only overhead is ugliness.
    for(uint i=0; i < count; ++i) {
        sf[i] = (struct frustum) {
            .tl_far = add_vector(scale_vector(sub_vector(f->tl_far, f->tl_near), (i + 1) * scale), f->tl_near),
            .tr_far = add_vector(scale_vector(sub_vector(f->tr_far, f->tr_near), (i + 1) * scale), f->tr_near),
            .bl_far = add_vector(scale_vector(sub_vector(f->bl_far, f->bl_near), (i + 1) * scale), f->bl_near),
            .br_far = add_vector(scale_vector(sub_vector(f->br_far, f->br_near), (i + 1) * scale), f->br_near),
        };
    }

    sf[0].tl_near = f->tl_near;
    sf[0].tr_near = f->tr_near;
    sf[0].bl_near = f->bl_near;
    sf[0].br_near = f->br_near;

    for(uint i=1; i < count; ++i) {
        sf[i].tl_near = sf[i-1].tl_far;
        sf[i].tr_near = sf[i-1].tr_far;
        sf[i].bl_near = sf[i-1].bl_far;
        sf[i].br_near = sf[i-1].br_far;
    }
}

// split a frustum in to count sub-frusta (fit to scene)
void partition_frustum_s(struct frustum *f, uint count, struct frustum *sf)
{
    float scale = 1.0 / count;
    
    for(uint i=0; i < count; ++i) {
        sf[i] = (struct frustum) {
            .tl_near = f->tl_near,
            .tr_near = f->tr_near,
            .bl_near = f->bl_near,
            .br_near = f->br_near,

            .tl_far = add_vector(scale_vector(sub_vector(f->tl_far, f->tl_near), (i + 1) * scale), f->tl_near),
            .tr_far = add_vector(scale_vector(sub_vector(f->tr_far, f->tr_near), (i + 1) * scale), f->tr_near),
            .bl_far = add_vector(scale_vector(sub_vector(f->bl_far, f->bl_near), (i + 1) * scale), f->bl_near),
            .br_far = add_vector(scale_vector(sub_vector(f->br_far, f->br_near), (i + 1) * scale), f->br_near),
        };
    }
}
