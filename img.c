#include <stdlib.h>
#include "img.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

__attribute__((unused)) static void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply) { (void)flag_true_if_should_unpremultiply; }
__attribute__((unused)) static void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert) { (void)flag_true_if_should_convert; }
__attribute__((unused)) static void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip) { (void)flag_true_if_should_flip; }

int img_load_rgba(const char *path, unsigned char **out_rgba, int *w, int *h)
{
    int comp;
    unsigned char *p;

    if (!out_rgba || !w || !h)
        return 0;

    p = stbi_load(path, w, h, &comp, 4);
    if (!p)
        return 0;

    *out_rgba = p;
    return 1;
}

int img_load_rgba_mem(const unsigned char *buf, int len, unsigned char **out_rgba, int *w, int *h)
{
    int comp;
    unsigned char *p;

    if (!buf || len <= 0 || !out_rgba || !w || !h)
        return 0;

    p = stbi_load_from_memory(buf, len, w, h, &comp, 4);
    if (!p)
        return 0;

    *out_rgba = p;
    return 1;
}

void img_free(unsigned char *p)
{
    stbi_image_free(p);
}

int img_write_png(const char *path, int w, int h, const unsigned char *rgba)
{
    if (!path || !rgba || w <= 0 || h <= 0)
        return 0;
    return stbi_write_png(path, w, h, 4, rgba, w * 4) != 0;
}
