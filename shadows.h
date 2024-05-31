#ifndef SOL_SHADOWS_H_INCLUDE_GUARD_
#define SOL_SHADOWS_H_INCLUDE_GUARD_

#include "math.h"
#include "gltf.h"

struct frustum {
    vector tl_near, tr_near, bl_near, br_near;
    vector tl_far,  tr_far,  bl_far,  br_far;
};

static inline void frustum_to_box(struct frustum *f, struct box *b)
{
    *b = (struct box) {
        .p = {
            f->bl_near, f->bl_far, f->br_far, f->br_near,
            f->tl_near, f->tl_far, f->tr_far, f->tr_near,
        },
    };
}

static inline void scene_bounding_box(struct box *bb)
{
    float max_x =  10;
    float max_y =  10;
    float max_z =  10;

    float min_x = -10;
    float min_y =   0;
    float min_z = -10;

    bb->p[0] = vector4(min_x, min_y, min_z, 1);
    bb->p[1] = vector4(min_x, min_y, max_z, 1);
    bb->p[2] = vector4(max_x, min_y, max_z, 1);
    bb->p[3] = vector4(max_x, min_y, min_z, 1);

    bb->p[4] = vector4(min_x, max_y, min_z, 1);
    bb->p[5] = vector4(min_x, max_y, max_z, 1);
    bb->p[6] = vector4(max_x, max_y, max_z, 1);
    bb->p[7] = vector4(max_x, max_y, min_z, 1);
}

struct minmax near_far(struct minmax x, struct minmax y, struct box *bb);
void minmax_frustum_points(struct frustum *f, matrix *space, struct minmax *minmax_x, struct minmax *minmax_y);
void perspective_frustum(float fov, float ar, float near, float far, struct frustum *ret);
void ortho_frustum(float l, float r, float t, float b, float n, float f, struct frustum *ret);

#endif // include guard
