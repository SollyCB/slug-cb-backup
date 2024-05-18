#include "image.h"
#include "log.h"

struct image load_image(const char *uri) {
    struct image img;
    int n;
    img.data = stbi_load(uri, &img.x, &img.y, &n, STBI_rgb_alpha);
    log_print_error_if(!img.data, "failed to load image %s", uri);
    img.miplevels = calc_mips(img.x, img.y);
    return img;
}

void free_image(struct image *img) {
    stbi_image_free(img->data);
    memset(img, 0, sizeof(*img));
}
