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

void scene_bounding_box(uint count, matrix *positions, gltf *models, struct box *bb);
struct minmax near_far(struct minmax x, struct minmax y, struct box *bb);
void minmax_frustum_points(struct frustum *f, matrix *space, struct minmax *minmax_x, struct minmax *minmax_y);
void perspective_frustum(float fov, float ar, float near, float far, struct frustum *ret);
void ortho_frustum(float l, float r, float t, float b, float n, float f, struct frustum *ret);

#endif // include guard
