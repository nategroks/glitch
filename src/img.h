#ifndef GLITCH_IMG_H
#define GLITCH_IMG_H

#include <stdint.h>

int img_load_rgba(const char *path, unsigned char **out_rgba, int *w, int *h);
int img_load_rgba_mem(const unsigned char *buf, int len, unsigned char **out_rgba, int *w, int *h);
void img_free(unsigned char *p);
int img_write_png(const char *path, int w, int h, const unsigned char *rgba);

#endif
