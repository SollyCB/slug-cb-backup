#ifndef SOL_IMAGE_H_INCLUDE_GUARD_
#define SOL_IMAGE_H_INCLUDE_GUARD_

// @Todo define stb_malloc, realloc, etc.
#include "external/stb_image.h"

#include "defs.h"

struct image {
    int x,y;
    uchar *data;
    uint miplevels;
};

static inline uint32 calc_mips(uint32 x, uint32 y) {
    return log2(max(x, y)) + 1;
}

struct image load_image(const char *uri);

void free_image(struct image *img);

static inline size_t image_size(struct image *image) {
    return image->x * image->y * 4;
}

#endif // include guard
