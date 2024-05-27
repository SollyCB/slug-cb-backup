#ifndef SOL_SHADOWS_H_INCLUDE_GUARD_
#define SOL_SHADOWS_H_INCLUDE_GUARD_

#include "math.h"
#include "gltf.h"

struct frustum {
    vector tl_near, tr_near, bl_near, br_near;
    vector tl_far,  tr_far,  bl_far,  br_far;
};

struct bounding_box {
    vector p[8];
};

void scene_bounding_box(uint count, matrix *positions, gltf *models, struct bounding_box *bb);
struct minmax near_far(struct minmax x, struct minmax y, struct bounding_box *bb);
void minmax_frustum_points(struct frustum *f, matrix *space, struct minmax *minmax_x, struct minmax *minmax_y);
void get_frustum(float fov, float near, float far, struct frustum *ret);

#endif // include guard
