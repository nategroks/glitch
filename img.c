#include <limits.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"

struct mem_reader {
    const unsigned char *buf;
    size_t len;
    size_t pos;
};

static void
png_read_mem(png_structp png, png_bytep out, png_size_t len)
{
    struct mem_reader *r = png_get_io_ptr(png);

    if (!r || r->pos + len > r->len)
        png_error(png, "png mem underrun");

    memcpy(out, r->buf + r->pos, len);
    r->pos += len;
}

static int
decode_rgba(png_structp png, png_infop info, unsigned char **out_rgba, int *w, int *h)
{
    png_uint_32 width, height;
    int bit_depth, color_type;
    png_size_t rowbytes;
    unsigned char *pixels, *row;

    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
    if (!width || !height || width > (png_uint_32)INT_MAX || height > (png_uint_32)INT_MAX)
        return 0;

    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_set_interlace_handling(png);
    png_read_update_info(png, info);

    rowbytes = png_get_rowbytes(png, info);
    pixels = malloc(rowbytes * height);
    if (!pixels)
        return 0;

    row = pixels;
    for (png_uint_32 y = 0; y < height; ++y) {
        png_read_row(png, row, NULL);
        row += rowbytes;
    }
    png_read_end(png, info);

    *out_rgba = pixels;
    *w = (int)width;
    *h = (int)height;
    return 1;
}

int
img_load_rgba(const char *path, unsigned char **out_rgba, int *w, int *h)
{
    png_structp png;
    png_infop info;
    FILE *fp;
    int ok = 0;

    if (!path || !out_rgba || !w || !h)
        return 0;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
        goto done;

    if (setjmp(png_jmpbuf(png)))
        goto done;

    png_init_io(png, fp);
    png_read_info(png, info);
    ok = decode_rgba(png, info, out_rgba, w, h);

done:
    if (png || info)
        png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return ok;
}

int
img_load_rgba_mem(const unsigned char *buf, int len, unsigned char **out_rgba, int *w, int *h)
{
    png_structp png;
    png_infop info;
    struct mem_reader reader;
    int ok = 0;

    if (!buf || len <= 8 || !out_rgba || !w || !h)
        return 0;

    if (png_sig_cmp((png_bytep)buf, 0, 8) != 0)
        return 0;

    reader.buf = buf;
    reader.len = (size_t)len;
    reader.pos = 0;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
        return 0;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        return 0;
    }

    png_set_read_fn(png, &reader, png_read_mem);
    png_read_info(png, info);
    ok = decode_rgba(png, info, out_rgba, w, h);
    png_destroy_read_struct(&png, &info, NULL);

    return ok;
}

void
img_free(unsigned char *p)
{
    free(p);
}

int
img_write_png(const char *path, int w, int h, const unsigned char *rgba)
{
    png_structp png;
    png_infop info;
    FILE *fp;
    int ok = 0;

    if (!path || !rgba || w <= 0 || h <= 0)
        return 0;

    fp = fopen(path, "wb");
    if (!fp)
        return 0;

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
        goto done;

    if (setjmp(png_jmpbuf(png)))
        goto done;

    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    for (int y = 0; y < h; ++y)
        png_write_row(png, (png_bytep)(rgba + (size_t)w * 4 * y));

    png_write_end(png, NULL);
    ok = 1;

done:
    if (png || info)
        png_destroy_write_struct(&png, &info);
    fclose(fp);
    return ok;
}
