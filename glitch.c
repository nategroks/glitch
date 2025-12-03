/*
 * glitch 1.3
 * animated fork of sxl 1.1 by stx4
 *
 * Original project: sxl (MIT-licensed)
 * This modified version is also released under the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <math.h>
#include <ctype.h>

#ifndef MINIMAL_BUILD
#include <curl/curl.h>
#endif

#define XXH_INLINE_ALL
#include "xxhash.h"
#include "img.h"

#include "colors.h"
#include "shape.h"   /* kept for future use (sprites / logos) */

#define FRAMES         1
#define LINES          10
#define MAX_STATS      10
#define SHAPE_COLS     12   /* left glyph field */
#define JITTER_PCT     20
#define FRAME_TOP_ROW  1    /* first content row (1-based) after leading newline */
#define IMG_PAD_LEFT   0
#define IMG_PAD_RIGHT  0
#define IMG_COL        ((SHAPE_COLS - IMG_DRAW_WIDTH) / 2)  /* center over banner */
#define IMG_ROW        0    /* row (0-based), align with banner top */
#define IMG_WIDTH      17   /* image width in cells */
#define IMG_DRAW_WIDTH 18   /* nominal kitty render width (cells) */
#define IMG_PAD        1   /* padding on both sides of image lane */
#define IMG_DRAW_HEIGHT 5  /* nominal kitty render height (cells) */
#define IMG_DRAW_SHIFT 0    /* no extra shift */
#define IMG_GAP        0   /* base gap; actual gap computed at runtime */
#define ENTROPY_TARGET_BYTES 4096
#define ENTROPY_ADD_BYTES    512

#define MASK_RECT     0
#define MASK_ELLIPSE  1
#define MASK_DIAMOND  2
#define MASK_TRI_UP   3
#define MASK_TRI_DOWN 4
#define MASK_TRI_LEFT 5
#define MASK_TRI_RIGHT 6
#define MASK_TRAP_UP  7
#define MASK_TRAP_DOWN 8
#define MASK_TRAP_LEFT 9
#define MASK_TRAP_RIGHT 10
#define MASK_HOURGLASS 11
#define MASK_HEX      12
#define MASK_OCT      13
#define MASK_CHEV_UP  14
#define MASK_CHEV_DOWN 15
#define MASK_CHEV_LEFT 16
#define MASK_CHEV_RIGHT 17
#define MASK_WAVE     18
#define MASK_NOTCH_TOP 19
#define MASK_NOTCH_BOTTOM 20
#define MASK_PILL     21
#define MASK_STAIRS_UP 22
#define MASK_STAIRS_DOWN 23
#define MASK_COUNT    24

#define COLOR_CODE_LEN 32

static char g_bg_codes[4][COLOR_CODE_LEN];
static char g_fg_codes[5][COLOR_CODE_LEN]; /* DIS, KER, UPT, MEM, PIPE */
static char g_palette_source[1024];
static const char *g_bg_defaults[4] = { BG1, BG2, BG3, BG4 };
static const char *g_fg_defaults[5] = { FG_DIS, FG_KER, FG_UPT, FG_MEM, FG_PIPE };
static size_t g_entropy_current = 0;
static char g_color_config_path[512];

static const char *bg_code(int idx) {
    if (idx < 0 || idx >= 4) return "";
    return g_bg_codes[idx][0] ? g_bg_codes[idx] : g_bg_defaults[idx];
}

static const char *fg_code(int idx) {
    if (idx < 0 || idx >= 5) return "";
    return g_fg_codes[idx][0] ? g_fg_codes[idx] : g_fg_defaults[idx];
}

static int readable_png(const char *path);
static void read_ip_addr(char *out, size_t out_sz);
static void read_disk_usage(char *out, size_t out_sz);
static void read_open_ports(char *out, size_t out_sz);

static ssize_t urandom_fill(void *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return n;
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static void rng_seed(uint64_t seed) {
    if (seed == 0) seed = 0xfeedbeefcafef00dULL;
    rng_state ^= seed;
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
}

static uint32_t rng_u32(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

static int rng_range(int max) {
    if (max <= 0) return 0;
    return (int)(rng_u32() % (uint32_t)max);
}

static void fallback_rand(void *buf, size_t len) {
    unsigned char *b = (unsigned char *)buf;
    for (size_t i = 0; i < len; ++i) {
        b[i] = (unsigned char)(rng_u32() & 0xFF);
    }
}


static void get_random_bytes(void *buf, size_t len) {
    if (urandom_fill(buf, len) != (ssize_t)len) {
        fallback_rand(buf, len);
    }
}

static void gen_entropy_bytes(size_t len) {
    unsigned char tmp[4096];
    while (len > 0) {
        size_t chunk = (len > sizeof(tmp)) ? sizeof(tmp) : len;
        get_random_bytes(tmp, chunk);
        fwrite(tmp, 1, chunk, stdout);
        len -= chunk;
    }
}

static void gen_passphrase(int len) {
    const char *alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*?-_=+:/";
    size_t a_len = strlen(alphabet);
    unsigned char buf[256];
    if (len > (int)sizeof(buf)) len = (int)sizeof(buf);
    get_random_bytes(buf, (size_t)len);
    for (int i = 0; i < len; ++i) {
        putchar(alphabet[buf[i] % a_len]);
    }
    putchar('\n');
}

static void write_entropy_file(const char *path, size_t len) {
    unsigned char *buf = (unsigned char *)malloc(len);
    if (!buf) return;
    get_random_bytes(buf, len);
    char tmp_path[1024];
    size_t need = strlen(path) + 4;
    if (need >= sizeof(tmp_path)) {
        free(buf);
        return;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *f = fopen(tmp_path, "wb");
    if (f) {
        fwrite(buf, 1, len, f);
        fclose(f);
        rename(tmp_path, path);
    }
    free(buf);
}

static void entropy_cache(const char *home, size_t len) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.config/glitch/entropy.bin", home);

    struct stat st;
    int need_refresh = 1;
    if (stat(path, &st) == 0) {
        time_t now = time(NULL);
        if (st.st_size == (off_t)len && (now - st.st_mtime) < (time_t)(30 * 24 * 3600)) {
            need_refresh = 0;
        }
    }
    if (need_refresh) {
        write_entropy_file(path, len);
    }

    FILE *f = fopen(path, "rb");
    if (!f) return;
    unsigned char buf[4096];
    size_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        size_t n = fread(buf, 1, chunk, f);
        if (n == 0) break;
        fwrite(buf, 1, n, stdout);
        remaining -= n;
    }
    fclose(f);
}

static size_t update_entropy_progress(const char *home, size_t add_bytes, size_t target_bytes) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.config/glitch/entropy.progress", home);
    time_t now = time(NULL);
    size_t current = 0;
    struct stat st;
    if (stat(path, &st) == 0) {
        double age = difftime(now, st.st_mtime);
        if (age < 30.0 * 24 * 3600) {
            FILE *f = fopen(path, "r");
            if (f) {
                if (fscanf(f, "%zu", &current) != 1) {
                    current = 0;
                }
                fclose(f);
            }
        }
    }

    current += add_bytes;
    if (current >= target_bytes) {
        current = 0;
    }

    char tmp[1024];
    size_t needed = strlen(path) + 4;
    if (needed >= sizeof(tmp)) return current;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (f) {
        fprintf(f, "%zu\n", current);
        fclose(f);
        rename(tmp, path);
    }
    return current;
}

static void format_entropy_bar(char *out, size_t out_sz, size_t current, size_t target) {
    if (target == 0) {
        snprintf(out, out_sz, "n/a");
        return;
    }
    double pct = (double)current / (double)target;
    if (pct > 1.0) pct = 1.0;
    int bars = (int)(pct * 10.0 + 0.5);
    if (bars > 10) bars = 10;
    char bar[16];
    int idx = 0;
    bar[idx++] = '[';
    for (int i = 0; i < 10; ++i) {
        bar[idx++] = (i < bars) ? '#' : '-';
    }
    bar[idx++] = ']';
    bar[idx] = '\0';
    double kb = (double)current / 1024.0;
    double kb_target = (double)target / 1024.0;
    snprintf(out, out_sz, "%s %.1f/%.1f KiB", bar, kb, kb_target);
}

static void
print_img_gap_line(int palette_idx, int row, int total_rows, int width, int have_image)
{
	int radius, cx, cy, dy, dy2, x, inside;
	const char *color;

	if (width <= 0) {
		return;
	}
	if (!have_image) {
		printf("%*s", width, "");
		return;
	}

	radius = (width < total_rows ? width : total_rows) / 2;
	cx = width / 2;
	cy = total_rows / 2;
	dy = (row - cy);
	dy2 = dy * dy;
	color = bg_code(palette_idx);

	for (x = 0; x < width; ++x) {
		int dx = x - cx;
		inside = (dx * dx + dy2) <= (radius * radius);
		if (inside && color && *color) {
			printf("%s ", color);
			printf(F_RESET);
		} else {
			printf(" ");
		}
	}
}


static int hex_to_rgb_triplet(const char *hex, int *r, int *g, int *b) {
    if (!hex || strlen(hex) < 7 || hex[0] != '#') return 0;
    char buf[3] = {0};

    buf[0] = hex[1]; buf[1] = hex[2];
    *r = (int)strtol(buf, NULL, 16);

    buf[0] = hex[3]; buf[1] = hex[4];
    *g = (int)strtol(buf, NULL, 16);

    buf[0] = hex[5]; buf[1] = hex[6];
    *b = (int)strtol(buf, NULL, 16);

    return 1;
}

static void make_color_code(char *out, size_t out_size, int is_bg, int r, int g, int b) {
    snprintf(out, out_size, "\033[%d;2;%d;%d;%dm", is_bg ? 48 : 38, r, g, b);
}

static void clear_palette(void) {
    for (int i = 0; i < 4; ++i) {
        g_bg_codes[i][0] = '\0';
    }
    for (int i = 0; i < 5; ++i) {
        g_fg_codes[i][0] = '\0';
    }
    g_palette_source[0] = '\0';
}

static void bg_to_fg(const char *bg, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!bg || !*bg) return;
    if (strncmp(bg, "\033[48;", 6) == 0) {
        snprintf(out, out_sz, "\033[38;%s", bg + 6);
    }
}

static const char *color_config_path(char *buf, size_t buf_sz) {
    if (g_color_config_path[0]) {
        return g_color_config_path;
    }
    const char *home = getenv("HOME");
    if (!home || !*home || !buf || buf_sz == 0) return NULL;
    snprintf(buf, buf_sz, "%s/.config/glitch/color.config", home);
    return buf;
}

typedef struct {
    char key[16];
    char label[8];
    char value[128];
} StatEntry;

typedef struct {
    int net_images;
    int fetch_count;
    int fetch_max;
    char fetch_source[32];
    char local_dir[512];
    int stats_count;
    char stats_keys[MAX_STATS][32];
} AppConfig;

static const char *default_variant_dir(char *buf, size_t buf_size);

static void apply_palette_entry(const char *key, const char *val) {
    int r, g, b;
    if (!key || !val) return;
    if (!hex_to_rgb_triplet(val, &r, &g, &b)) return;

    if (strcmp(key, "BG1") == 0) {
        make_color_code(g_bg_codes[0], sizeof(g_bg_codes[0]), 1, r, g, b);
    } else if (strcmp(key, "BG2") == 0) {
        make_color_code(g_bg_codes[1], sizeof(g_bg_codes[1]), 1, r, g, b);
    } else if (strcmp(key, "BG3") == 0) {
        make_color_code(g_bg_codes[2], sizeof(g_bg_codes[2]), 1, r, g, b);
    } else if (strcmp(key, "BG4") == 0) {
        make_color_code(g_bg_codes[3], sizeof(g_bg_codes[3]), 1, r, g, b);
    } else if (strcmp(key, "FG_DIS") == 0) {
        make_color_code(g_fg_codes[0], sizeof(g_fg_codes[0]), 0, r, g, b);
    } else if (strcmp(key, "FG_KER") == 0) {
        make_color_code(g_fg_codes[1], sizeof(g_fg_codes[1]), 0, r, g, b);
    } else if (strcmp(key, "FG_UPT") == 0) {
        make_color_code(g_fg_codes[2], sizeof(g_fg_codes[2]), 0, r, g, b);
    } else if (strcmp(key, "FG_MEM") == 0) {
        make_color_code(g_fg_codes[3], sizeof(g_fg_codes[3]), 0, r, g, b);
    } else if (strcmp(key, "FG_PIPE") == 0) {
        make_color_code(g_fg_codes[4], sizeof(g_fg_codes[4]), 0, r, g, b);
    } else if (strcmp(key, "PRIMARY") == 0) {
        snprintf(g_palette_source, sizeof(g_palette_source), "%s", val);
    }
}

static int load_palette_from_config(void) {
    char path_buf[512];
    const char *path = color_config_path(path_buf, sizeof(path_buf));
    if (!path || !*path) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        char *nl = strchr(val, '\n');
        if (nl) *nl = '\0';
        apply_palette_entry(key, val);
    }

    fclose(f);
    return 1;
}

static double lum_rgb(int r, int g, int b)
{
	double rf = r / 255.0;
	double gf = g / 255.0;
	double bf = b / 255.0;
	double fr = (rf <= 0.03928) ? rf / 12.92 : pow((rf + 0.055) / 1.055, 2.4);
	double fg = (gf <= 0.03928) ? gf / 12.92 : pow((gf + 0.055) / 1.055, 2.4);
	double fb = (bf <= 0.03928) ? bf / 12.92 : pow((bf + 0.055) / 1.055, 2.4);

	return 0.2126 * fr + 0.7152 * fg + 0.0722 * fb;
}

static void mix_rgb(int r1, int g1, int b1, int r2, int g2, int b2, double t,
		    int *or_, int *og, int *ob)
{
	*or_ = (int)round(r1 * (1.0 - t) + r2 * t);
	*og  = (int)round(g1 * (1.0 - t) + g2 * t);
	*ob  = (int)round(b1 * (1.0 - t) + b2 * t);
}

static void rgb_to_hex(int r, int g, int b, char *out, size_t out_sz)
{
	snprintf(out, out_sz, "#%02x%02x%02x", r, g, b);
}

static int sample_palette_from_rgba(const unsigned char *rgba, int w, int h,
				    uint32_t out_bg[4])
{
	struct Bucket {
		uint64_t sum_r, sum_g, sum_b, count;
	};
	struct Pick {
		uint32_t color;
		uint64_t count;
		double lum;
	};
	struct Bucket buckets[4096];
	struct Pick picks[4];
	int stride, max_dim, x, y, i, j, k, key;
	const unsigned char *row, *p;
	uint64_t cnt;
	int r, g, b;
	double l;

	if (!rgba || w <= 0 || h <= 0)
		return 0;

	memset(buckets, 0, sizeof(buckets));

	stride = 1;
	max_dim = (w > h) ? w : h;
	if (max_dim > 96) {
		stride = max_dim / 96;
		if (stride < 1)
			stride = 1;
	}

	for (y = 0; y < h; y += stride) {
		row = rgba + (size_t)y * w * 4;
		for (x = 0; x < w; x += stride) {
			p = row + (size_t)x * 4;
			if (p[3] < 10)
				continue;
			r = p[0];
			g = p[1];
			b = p[2];
			key = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
			struct Bucket *bk = &buckets[key];
			bk->sum_r += (uint64_t)r;
			bk->sum_g += (uint64_t)g;
			bk->sum_b += (uint64_t)b;
			bk->count++;
		}
	}

	for (i = 0; i < 4; ++i) {
		picks[i].color = 0;
		picks[i].count = 0;
		picks[i].lum = 0.0;
	}

	for (i = 0; i < 4096; ++i) {
		if (buckets[i].count == 0)
			continue;
		cnt = buckets[i].count;
		r = (int)(buckets[i].sum_r / cnt);
		g = (int)(buckets[i].sum_g / cnt);
		b = (int)(buckets[i].sum_b / cnt);
		l = lum_rgb(r, g, b);
		for (j = 0; j < 4; ++j) {
			if (cnt > picks[j].count) {
				for (k = 3; k > j; --k)
					picks[k] = picks[k - 1];
				picks[j].color = ((uint32_t)r << 16) |
						 ((uint32_t)g << 8) | (uint32_t)b;
				picks[j].count = cnt;
				picks[j].lum = l;
				break;
			}
		}
	}

	if (picks[0].count == 0)
		return 0;

	for (i = 1; i < 4; ++i) {
		if (picks[i].count == 0)
			picks[i] = picks[0];
	}

	for (i = 0; i < 4; ++i) {
		for (j = i + 1; j < 4; ++j) {
			if (picks[i].lum > picks[j].lum) {
				struct Pick tmp = picks[i];
				picks[i] = picks[j];
				picks[j] = tmp;
			}
		}
	}

	for (i = 0; i < 4; ++i) {
		r = (picks[i].color >> 16) & 0xFF;
		g = (picks[i].color >> 8) & 0xFF;
		b = picks[i].color & 0xFF;
		l = lum_rgb(r, g, b);
		if (l < 0.08) {
			mix_rgb(r, g, b, 255, 255, 255, 0.5, &r, &g, &b);
		} else if (l > 0.92) {
			mix_rgb(r, g, b, 0, 0, 0, 0.4, &r, &g, &b);
		} else if (l < 0.12) {
			mix_rgb(r, g, b, 255, 255, 255, 0.35, &r, &g, &b);
		}
		out_bg[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
	}

	return 1;
}


static int load_palette_from_image(const char *image_path) {
    if (!image_path || !*image_path) return 0;

    clear_palette();

    if (getenv("GLITCH_DEBUG")) {
        fprintf(stderr, "[glitch] palette: load from %s\n", image_path);
    }

    int w = 0, h = 0;
    unsigned char *rgba = NULL;
    if (!img_load_rgba(image_path, &rgba, &w, &h)) {
        if (getenv("GLITCH_DEBUG")) {
            fprintf(stderr, "[glitch] palette: load failed for %s\n", image_path);
        }
        return 0;
    }

    uint32_t bg[4];
    int ok = sample_palette_from_rgba(rgba, w, h, bg);
    img_free(rgba);
    if (!ok) {
        if (getenv("GLITCH_DEBUG")) {
            fprintf(stderr, "[glitch] palette: sampling failed for %s\n", image_path);
        }
        return 0;
    }

    /* derive foregrounds */
    char config_lines[16][256];
    char hex[16];
    for (int i = 0; i < 4; ++i) {
        int r = (bg[i] >> 16) & 0xFF;
        int g = (bg[i] >> 8) & 0xFF;
        int b = bg[i] & 0xFF;
        rgb_to_hex(r, g, b, hex, sizeof(hex));
        snprintf(config_lines[i], sizeof(config_lines[i]), "BG%d=%s", i + 1, hex);
        char key[8];
        snprintf(key, sizeof(key), "BG%d", i + 1);
        apply_palette_entry(key, hex);
    }

    for (int i = 0; i < 4; ++i) {
        int r = (bg[i] >> 16) & 0xFF;
        int g = (bg[i] >> 8) & 0xFF;
        int b = bg[i] & 0xFF;
        double l = lum_rgb(r, g, b);
        int ar = (l < 0.55) ? 255 : 0;
        int ag = (l < 0.55) ? 255 : 0;
        int ab = (l < 0.55) ? 255 : 0;
        double factor = (l < 0.55) ? 0.45 : 0.35;
        mix_rgb(r, g, b, ar, ag, ab, factor, &r, &g, &b);
        rgb_to_hex(r, g, b, hex, sizeof(hex));
        const char *label = (i == 0) ? "DIS" : (i == 1) ? "KER" : (i == 2) ? "UPT" : "MEM";
        snprintf(config_lines[4 + i], sizeof(config_lines[0]), "FG_%s=%s", label, hex);
        char key[16];
        snprintf(key, sizeof(key), "FG_%s", label);
        apply_palette_entry(key, hex);
    }

    /* pipe color based on darkest bg */
    int pr = (bg[0] >> 16) & 0xFF;
    int pg = (bg[0] >> 8) & 0xFF;
    int pb = bg[0] & 0xFF;
    mix_rgb(pr, pg, pb, 255, 255, 255, 0.3, &pr, &pg, &pb);
    rgb_to_hex(pr, pg, pb, hex, sizeof(hex));
    snprintf(config_lines[8], sizeof(config_lines[0]), "FG_PIPE=%s", hex);
    apply_palette_entry("FG_PIPE", hex);
    snprintf(config_lines[9], sizeof(config_lines[0]), "PRIMARY=%.240s", image_path);
    snprintf(g_palette_source, sizeof(g_palette_source), "%s", image_path);

    const char *home = getenv("HOME");
    if (home && *home) {
        char cfg_path[512];
        snprintf(cfg_path, sizeof(cfg_path), "%s/.config/glitch/color.config", home);
        FILE *out = fopen(cfg_path, "w");
        if (out) {
            fprintf(out, "# ~/.config/glitch/color.config\n");
            fprintf(out, "# Auto-generated from %s\n", image_path);
            for (int i = 0; i < 10; ++i) {
                fprintf(out, "%s\n", config_lines[i]);
            }
            fclose(out);
        }
    }

    return 1;
}

static void init_palette(const char *image_path) {
    clear_palette();

    /* Try palette from the selected image first */
    if (image_path && access(image_path, R_OK) == 0) {
        if (getenv("GLITCH_DEBUG")) {
            fprintf(stderr, "[glitch] palette: sampling %s\n", image_path);
        }
        if (load_palette_from_image(image_path)) {
            return;
        }
    }

    /* Fallback to user config */
    if (load_palette_from_config()) {
        return;
    }

    /* Final fallback: keep compile-time defaults */
}

static int choose_random_variant(char *name_buf, size_t name_sz, char *path_buf, size_t path_sz) {
    const char *custom = getenv("GLITCH_VARIANT_DIR");
    char dir_path[1024];
    if (custom && *custom) {
        snprintf(dir_path, sizeof(dir_path), "%s", custom);
    } else {
        const char *home = getenv("HOME");
        if (!home || !*home) return 0;
        snprintf(dir_path, sizeof(dir_path), "%s/.config/glitch/variants", home);
    }

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    const char *pngs[256];
    int count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < (int)(sizeof(pngs) / sizeof(pngs[0]))) {
        size_t len = strlen(ent->d_name);
        if (len < 5) continue;
        if (strcmp(ent->d_name + len - 4, ".png") != 0) continue;
        char full[1024];
        int written = snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        if (written <= 0 || written >= (int)sizeof(full)) continue;
        if (!readable_png(full)) continue;
        pngs[count++] = strdup(ent->d_name);
    }
    closedir(dir);

    if (count == 0) return 0;

    int idx = rng_range(count);
    snprintf(name_buf, name_sz, "%s", pngs[idx]);
    int written = snprintf(path_buf, path_sz, "%s/%s", dir_path, pngs[idx]);
    if (written <= 0 || (size_t)written >= path_sz) {
        for (int i = 0; i < count; ++i) {
            free((void *)pngs[i]);
        }
        return 0;
    }

    for (int i = 0; i < count; ++i) {
        free((void *)pngs[i]);
    }
    return 1;
}

typedef struct {
	unsigned char *data;
	size_t len;
	size_t cap;
} MemBuf;

static size_t
curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t total = size * nmemb;
	MemBuf *b = (MemBuf *)userdata;
	size_t new_cap;
	unsigned char *p;

	if (total == 0)
		return 0;
	if (b->len + total + 1 > b->cap) {
		new_cap = (b->cap == 0) ? (total + 1024) : (b->cap * 2 + total);
		p = realloc(b->data, new_cap);
		if (!p)
			return 0;
		b->data = p;
		b->cap = new_cap;
	}
	memcpy(b->data + b->len, ptr, total);
	b->len += total;
	b->data[b->len] = 0;
	return total;
}

#ifdef MINIMAL_BUILD
static int
download_url(const char *url, MemBuf *out)
{
	(void)url;
	(void)out;
	return 0;
}
#else
static int
download_url(const char *url, MemBuf *out)
{
	CURL *curl;
	CURLcode rc;
	long timeout = 15L;

	curl = curl_easy_init();
	if (!curl)
		return 0;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "glitch-fetch/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	return rc == CURLE_OK;
}
#endif

static int
crop_square_rgba(const unsigned char *src, int w, int h,
		 unsigned char **out, int *side_out)
{
	int side, x0, y0, y;
	size_t bytes;
	unsigned char *dst;
	const unsigned char *row;

	side = (w < h) ? w : h;
	if (side <= 0)
		return 0;
	x0 = (w - side) / 2;
	y0 = (h - side) / 2;
	bytes = (size_t)side * side * 4;
	dst = (unsigned char *)malloc(bytes);
	if (!dst)
		return 0;
	for (y = 0; y < side; ++y) {
		row = src + ((y0 + y) * w + x0) * 4;
		memcpy(dst + (size_t)y * side * 4, row, (size_t)side * 4);
	}
	*out = dst;
	if (side_out)
		*side_out = side;
	return 1;
}

static int
ensure_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return 1;
		return 0;
	}
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static int
prune_variants_dir(const char *dir, int max_files)
{
	struct Entry {
		char path[1024];
		time_t mtime;
	};
	struct Entry ents[512], tmp;
	struct dirent *ent;
	DIR *d;
	int count, i, j, removed;
	size_t len;
	char full[1024];
	struct stat st;

	if (max_files <= 0)
		return 0;
	d = opendir(dir);
	if (!d)
		return 0;
	count = 0;
	while ((ent = readdir(d)) != NULL &&
	       count < (int)(sizeof(ents) / sizeof(ents[0]))) {
		len = strlen(ent->d_name);
		if (len < 5 || strcmp(ent->d_name + len - 4, ".png") != 0)
			continue;
		snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
		if (stat(full, &st) != 0)
			continue;
		snprintf(ents[count].path, sizeof(ents[count].path), "%s", full);
		ents[count].mtime = st.st_mtime;
		count++;
	}
	closedir(d);
	if (count <= max_files)
		return 0;
	for (i = 0; i < count; ++i) {
		for (j = i + 1; j < count; ++j) {
			if (ents[i].mtime > ents[j].mtime) {
				tmp = ents[i];
				ents[i] = ents[j];
				ents[j] = tmp;
			}
		}
	}
	removed = 0;
	for (i = 0; i < count - max_files; ++i) {
		unlink(ents[i].path);
		removed++;
	}
	return removed;
}

static int
save_variant_png(const unsigned char *rgba, int side, const char *dir,
		 char *out_path, size_t out_sz)
{
	uint64_t h;
	char fname[256];
	char full[1024];

	if (!rgba || side <= 0 || !dir)
		return 0;
	if (!ensure_dir(dir))
		return 0;
	h = XXH3_64bits(rgba, (size_t)side * side * 4);
	snprintf(fname, sizeof(fname), "img-%016llx.png", (unsigned long long)h);
	snprintf(full, sizeof(full), "%s/%s", dir, fname);
	if (access(full, R_OK) == 0) {
		if (out_path && out_sz > 0)
			snprintf(out_path, out_sz, "%s", full);
		return 1;
	}
	if (!img_write_png(full, side, side, rgba))
		return 0;
	if (out_path && out_sz > 0)
		snprintf(out_path, out_sz, "%s", full);
	return 1;
}

static int
fetch_one_image(const AppConfig *cfg, const char *dir)
{
#ifdef MINIMAL_BUILD
	(void)cfg;
	(void)dir;
	return 0;
#else
	char url[256];
	const char *src;
	MemBuf buf = {0};
	unsigned char *rgba = NULL;
	unsigned char *square = NULL;
	int w = 0, h = 0, side = 0;
	int ok, saved;
	unsigned int seed;

	src = (cfg && cfg->fetch_source[0]) ? cfg->fetch_source : "picsum";
	if (strcmp(src, "unsplash") == 0) {
		snprintf(url, sizeof(url), "https://source.unsplash.com/random/900x900");
	} else {
		seed = rng_u32();
		snprintf(url, sizeof(url), "https://picsum.photos/seed/%08x/900/900", seed);
	}
	if (!download_url(url, &buf)) {
		free(buf.data);
		return 0;
	}
	ok = img_load_rgba_mem(buf.data, (int)buf.len, &rgba, &w, &h);
	free(buf.data);
	if (!ok || !rgba)
		return 0;

	ok = crop_square_rgba(rgba, w, h, &square, &side);
	img_free(rgba);
	if (!ok)
		return 0;

	saved = save_variant_png(square, side, dir, NULL, 0);
	free(square);
	return saved;
#endif
}

static void
run_fetcher(const AppConfig *cfg)
{
#ifdef MINIMAL_BUILD
	(void)cfg;
	return;
#else
	char dir_buf[512];
	const char *dir;
	int i;

	if (!cfg || !cfg->net_images)
		return;
	dir = default_variant_dir(dir_buf, sizeof(dir_buf));
	if (!dir)
		return;
	ensure_dir(dir);
	for (i = 0; i < cfg->fetch_count; ++i)
		fetch_one_image(cfg, dir);
	prune_variants_dir(dir, cfg->fetch_max);
#endif
}

static const char *label_for_key(const char *key) {
    if (!key) return "stat";
    if (strcmp(key, "distro") == 0 || strcmp(key, "dis") == 0) return "dis";
    if (strcmp(key, "kernel") == 0 || strcmp(key, "ker") == 0) return "ker";
    if (strcmp(key, "uptime") == 0 || strcmp(key, "upt") == 0) return "upt";
    if (strcmp(key, "mem") == 0 || strcmp(key, "memory") == 0) return "mem";
    if (strcmp(key, "host") == 0) return "hst";
    if (strcmp(key, "user") == 0) return "usr";
    if (strcmp(key, "shell") == 0) return "shl";
    if (strcmp(key, "cpu") == 0) return "cpu";
    return key;
}

static void build_stats(const AppConfig *cfg,
                        const char *distro,
                        const char *kernel,
                        const char *uptime_buf,
                        const char *mem_buf,
                        const char *cpu_name,
                        const struct utsname *un,
                        StatEntry *out,
                        int *out_count) {
    const char *home = getenv("HOME");
    char entropy_buf[64] = {0};
    if (home && *home) {
        size_t cur = update_entropy_progress(home, ENTROPY_ADD_BYTES, ENTROPY_TARGET_BYTES);
        g_entropy_current = cur;
        format_entropy_bar(entropy_buf, sizeof(entropy_buf), cur, ENTROPY_TARGET_BYTES);
    } else {
        snprintf(entropy_buf, sizeof(entropy_buf), "n/a");
        g_entropy_current = 0;
    }

    char ip_buf[64] = {0};
    char disk_buf[64] = {0};
    char ports_buf[32] = {0};
    int ip_ready = 0, disk_ready = 0, ports_ready = 0;

    int count = 0;
    int max = (cfg && cfg->stats_count > 0) ? cfg->stats_count : 4;
    for (int i = 0; i < max && count < MAX_STATS; ++i) {
        const char *key = (cfg && cfg->stats_keys[i][0]) ? cfg->stats_keys[i] : NULL;
        if (!key) continue;
        StatEntry *st = &out[count];
        snprintf(st->key, sizeof(st->key), "%s", key);
        const char *lbl = label_for_key(key);
        snprintf(st->label, sizeof(st->label), "%s", lbl);
        for (size_t li = 0; li < strlen(st->label); ++li) {
            st->label[li] = (char)toupper((unsigned char)st->label[li]);
        }

        if (strcmp(key, "distro") == 0 || strcmp(key, "dis") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", distro);
        } else if (strcmp(key, "kernel") == 0 || strcmp(key, "ker") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", kernel);
        } else if (strcmp(key, "uptime") == 0 || strcmp(key, "upt") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", uptime_buf);
        } else if (strcmp(key, "mem") == 0 || strcmp(key, "memory") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", mem_buf);
        } else if (strcmp(key, "host") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", un->nodename);
        } else if (strcmp(key, "user") == 0) {
            const char *u = getenv("USER");
            snprintf(st->value, sizeof(st->value), "%s", u ? u : "unknown");
        } else if (strcmp(key, "shell") == 0) {
            const char *s = getenv("SHELL");
            snprintf(st->value, sizeof(st->value), "%s", s ? s : "unknown");
        } else if (strcmp(key, "cpu") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", cpu_name);
        } else if (strcmp(key, "ip") == 0) {
            if (!ip_ready) {
                read_ip_addr(ip_buf, sizeof(ip_buf));
                ip_ready = 1;
            }
            snprintf(st->value, sizeof(st->value), "%s", ip_buf);
        } else if (strcmp(key, "disk") == 0) {
            if (!disk_ready) {
                read_disk_usage(disk_buf, sizeof(disk_buf));
                disk_ready = 1;
            }
            snprintf(st->value, sizeof(st->value), "%s", disk_buf);
        } else if (strcmp(key, "ports") == 0) {
            if (!ports_ready) {
                read_open_ports(ports_buf, sizeof(ports_buf));
                ports_ready = 1;
            }
            snprintf(st->value, sizeof(st->value), "%s open", ports_buf);
        } else if (strcmp(key, "entropy") == 0) {
            snprintf(st->value, sizeof(st->value), "%s", entropy_buf);
        } else {
            snprintf(st->value, sizeof(st->value), "n/a");
        }
        count++;
    }
    if (count == 0) {
        snprintf(out[0].label, sizeof(out[0].label), "stat");
        snprintf(out[0].value, sizeof(out[0].value), "n/a");
        count = 1;
    }
    *out_count = count;
}

static void read_cpu_name(char *out, size_t out_sz) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        snprintf(out, out_sz, "cpu");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                char *nl = strchr(colon, '\n');
                if (nl) *nl = '\0';
                char *ghz = strstr(colon, "GHz");
                if (ghz && ghz > colon) {
                    char *at = strrchr(colon, '@');
                    if (at && at < ghz) *at = '\0';
                    *(ghz - 1) = '\0'; /* trim space before GHz */
                }
                snprintf(out, out_sz, "%s", colon);
                fclose(f);
                return;
            }
        }
    }
    fclose(f);
    snprintf(out, out_sz, "cpu");
}

static void
read_ip_addr(char *out, size_t out_sz)
{
    FILE *fp;
    char line[256];

    fp = popen("timeout 1 ip -4 addr show scope global 2>/dev/null", "r");
    if (!fp)
        fp = popen("ip -4 addr show scope global 2>/dev/null", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *p = strstr(line, "inet ");
            char *end;
            if (!p)
                continue;
            p += 5;
            while (*p == ' ')
                p++;
            end = p;
            while (*end && *end != '/' && *end != ' ' && *end != '\n')
                end++;
            *end = '\0';
            if (*p) {
                snprintf(out, out_sz, "%s", p);
                pclose(fp);
                return;
            }
        }
        pclose(fp);
    }

    fp = popen("hostname -I 2>/dev/null", "r");
    if (!fp) {
        snprintf(out, out_sz, "n/a");
        return;
    }
    if (!fgets(out, (int)out_sz, fp)) {
        snprintf(out, out_sz, "n/a");
    } else {
        char *sp = strchr(out, ' ');
        char *nl = strchr(out, '\n');
        if (sp) *sp = '\0';
        if (nl) *nl = '\0';
        if (out[0] == '\0') snprintf(out, out_sz, "n/a");
    }
    pclose(fp);
}

static void read_disk_usage(char *out, size_t out_sz) {
    struct statvfs vfs;
    if (statvfs("/", &vfs) != 0) {
        snprintf(out, out_sz, "n/a");
        return;
    }
    double total = (double)vfs.f_blocks * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
    double free = (double)vfs.f_bfree * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
    double used = total - free;
    snprintf(out, out_sz, "%.1f/%.1f GiB", used, total);
}

static void
read_open_ports(char *out, size_t out_sz)
{
	struct PortCount { int port; int count; } ports[512];
	int port_count = 0;
	FILE *fp;
	char line[256];

	fp = popen("timeout 1 ss -H -tan 2>/dev/null", "r");
	if (!fp)
		fp = popen("ss -H -tan 2>/dev/null", "r");
	if (!fp) {
		snprintf(out, out_sz, "n/a");
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *tok, *save = NULL;
		int field = 0;
		char local[128] = {0};
		int port = 0;

		for (tok = strtok_r(line, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
			if (*tok == '\0')
				continue;
			if (field == 3) {
				snprintf(local, sizeof(local), "%s", tok);
				break;
			}
			field++;
		}
		if (local[0] == '\0')
			continue;

		char *last_colon = strrchr(local, ':');
		if (!last_colon || !*(last_colon + 1))
			continue;
		port = atoi(last_colon + 1);
		if (port <= 0)
			continue;

		int found = 0;
		for (int i = 0; i < port_count; ++i) {
			if (ports[i].port == port) {
				ports[i].count++;
				found = 1;
				break;
			}
		}
		if (!found && port_count < (int)(sizeof(ports) / sizeof(ports[0]))) {
			ports[port_count].port = port;
			ports[port_count].count = 1;
			port_count++;
		}
	}
	pclose(fp);

	/* sort by count desc, then port asc */
	for (int i = 0; i < port_count; ++i) {
		for (int j = i + 1; j < port_count; ++j) {
			if (ports[j].count > ports[i].count ||
			    (ports[j].count == ports[i].count && ports[j].port < ports[i].port)) {
				struct PortCount tmp = ports[i];
				ports[i] = ports[j];
				ports[j] = tmp;
			}
		}
	}

	char buf[128] = {0};
	size_t pos = 0;
	int limit = port_count < 5 ? port_count : 5;
	for (int i = 0; i < limit; ++i) {
		if (pos && pos < sizeof(buf) - 1)
			buf[pos++] = ',';
		int n = snprintf(buf + pos, sizeof(buf) - pos, "%d", ports[i].port);
		if (n > 0)
			pos += (size_t)n;
	}

	if (pos == 0)
		snprintf(out, out_sz, "none");
	else
		snprintf(out, out_sz, "%s", buf);
}











typedef struct {
    const char *name;          /* GLITCH_NOISE mode name */
    const char *chars;         /* charset used for jitter / style */
    const char *rows[LINES];   /* 4-line base template (tiny glyph) */
} NoiseSet;

/* Noise modes: numeric/symbolic + slash/vein styles */
static const NoiseSet noise_sets[] = {
    /* default – raw static, no fixed template */
    {
        "default",
        "@#%*&",
        { NULL, NULL, NULL, NULL }
    },

    /* signal – tower / antenna with S/E markers */
    {
        "signal",
        "10SE",
        {
            "        1S1     ",
            "       10101    ",
            "       10101    ",
            "        1E1     "
        }
    },

    /* ritual – altar / ring with S/E poles */
    {
        "ritual",
        "10SE",
        {
            "     11111111   ",
            "   1S1000001E1  ",
            "   1E1000001S1  ",
            "     11111111   "
        }
    },

    /* gate – rectangular portal */
    {
        "gate",
        "10",
        {
            "      111111    ",
            "     1    01    ",
            "     1 111 1    ",
            "      111111    "
        }
    },

    /* drift – diagonal fading trace */
    {
        "drift",
        "10",
        {
            " 1              ",
            "  10            ",
            "    101         ",
            "       1011     "
        }
    },

    /* crown – crown-shaped glyph */
    {
        "crown",
        "10",
        {
            "   1  1  1      ",
            "    10101       ",
            "   1111111      ",
            "    10101       "
        }
    },

    /* chill – soft wave */
    {
        "chill",
        " .:*",
        {
            "   .:*.  .:*.   ",
            "  .   *:   *:   ",
            "   *:.   *:.    ",
            "    .  *:  .    "
        }
    },

    /* matrix – tiny 01 rain block */
    {
        "matrix",
        "01",
        {
            "   01010101     ",
            "   10101010     ",
            "   01010101     ",
            "   10101010     "
        }
    },

    /* --------- VEIN / SLASH FAMILY --------- */

    /* veil – hanging veins / threads */
    {
        "veil",
        "|/",
        {
            "  |  |  |  |    ",
            "  | /| /| /|    ",
            "   |  |  |      ",
            "   | /| /|      "
        }
    },

    /* slashfall – heavy forward slashes */
    {
        "slashfall",
        "/",
        {
            " //// //// //// ",
            "  //// //// ////",
            " //// //// //// ",
            "  //// //// ////"
        }
    },

    /* backwash – heavy backslashes */
    {
        "backwash",
        "\\\\",
        {
            " \\\\\\\\ \\\\\\\\ \\\\\\\\ ",
            "\\\\\\\\ \\\\\\\\ \\\\\\\\  ",
            " \\\\\\\\ \\\\\\\\ \\\\\\\\ ",
            "\\\\\\\\ \\\\\\\\ \\\\\\\\  "
        }
    },

    /* rail – alternating dashes */
    {
        "rail",
        "-_",
        {
            " --- --- --- ---",
            "--- --- --- --- ",
            " --- --- --- ---",
            "--- --- --- --- "
        }
    },

    /* stripe01 – dense 1/0 stripes */
    {
        "stripe01",
        "10",
        {
            " 10101010101010 ",
            "0101010101010101",
            " 10101010101010 ",
            "0101010101010101"
        }
    },

    /* pulse01 – pulsing 1 blocks */
    {
        "pulse01",
        "10",
        {
            " 1111    1111   ",
            "   11  11   11  ",
            " 1111    1111   ",
            "   11  11   11  "
        }
    },

    /* zed – zzz ladders */
    {
        "zed",
        "zZ",
        {
            " zzzz zzzz zzzz ",
            "    zzzz zzzz   ",
            " zzzz zzzz zzzz ",
            "    zzzz zzzz   "
        }
    },

    /* xo – alternating x/o */
    {
        "xo",
        "xo",
        {
            " xoxoxoxoxoxoxo ",
            " oxoxoxoxoxoxox ",
            " xoxoxoxoxoxoxo ",
            " oxoxoxoxoxoxox "
        }
    },

    /* arrows – dense symbol spray */
    {
        "arrows",
        "@><?",
        {
            " @@@>><<? ? ?>><<@",
            " ?>><<@@@>><<? ? ?",
            " @@@>><<? ? ?>><<@",
            " ?>><<@@@>><<? ? ?"
        }
    },

    /* valve – branching channels, more crossflow */
    {
        "valve",
        "|/_",
        {
            "  |/_  |/_  |   ",
            "   |/_   |/_    ",
            "    |/_   |/_   ",
            "     |/_   |/_  "
        }
    },

    /* tangle – dense crossing slashes, knotted */
    {
        "tangle",
        "|/\\",
        {
            "  |/\\  |/\\  |  ",
            "   /|\\ /|\\ /| ",
            "    |/\\  |/\\  ",
            "     /|\\  /|\\ "
        }
    },

    /* artery – vertical trunks with diagonal offshoots */
    {
        "artery",
        "|/",
        {
            "    |    |      ",
            "   |/   |/      ",
            "   |    |/      ",
            "   |/      /    "
        }
    },

    /* capillary – fine parallel lines and small bends */
    {
        "capillary",
        "|/",
        {
            "  |/|/|/|/|/    ",
            "   | | | | |    ",
            "  |/|/|/|/|/    ",
            "   | | | | |    "
        }
    },

    /* cascade – angled flowing slashes */
    {
        "cascade",
        "/",
        {
            "      /        ",
            "     //        ",
            "    ///        ",
            "   ////        "
        }
    },

    /* lattice – rigid vertical / horizontal mix, vascular grid */
    {
        "lattice",
        "|-",
        {
            "   |-|-|-|-|    ",
            "   | | | | |    ",
            "   |-|-|-|-|    ",
            "   | | | | |    "
        }
    },

    /* braid – interleaved, alternating diagonals */
    {
        "braid",
        "/",
        {
            "  / / / / /    ",
            "   / / / /     ",
            "  / / / / /    ",
            "   / / / /     "
        }
    },

    /* vine – creeping diagonal up the side */
    {
        "vine",
        "/|",
        {
            "         /|     ",
            "        / |     ",
            "       /  |     ",
            "      /   |     "
        }
    },

    /* warp – criss-crossing channels */
    {
        "warp",
        "/|",
        {
            "   /| /| /|    ",
            "    |/ |/ |/   ",
            "   /| /| /|    ",
            "    |/ |/ |/   "
        }
    },

    /* fissure – cracked vertical line */
    {
        "fissure",
        "/|",
        {
            "        |      ",
            "       /|      ",
            "        |/     ",
            "         |     "
        }
    },

    /* branch – forked split */
    {
        "branch",
        "/|",
        {
            "        /      ",
            "       /|      ",
            "      / |      ",
            "        |      "
        }
    },

    /* sluice – long diagonal channel */
    {
        "sluice",
        "/",
        {
            "      /        ",
            "     /         ",
            "    /          ",
            "   /           "
        }
    },

    /* weave – checker of slashes & pillars */
    {
        "weave",
        "|/",
        {
            "  |/ |/ |/ |/  ",
            "   |  |  |  |  ",
            "  |/ |/ |/ |/  ",
            "   |  |  |  |  "
        }
    },

    /* --------- OTHERS --------- */

    /* storm – branching lightning */
    {
        "storm",
        "/\\|_",
        {
            "        /\\     ",
            "       /  \\_   ",
            "      /\\  /\\   ",
            "        \\\\_/   "
        }
    },

    /* pulse – concentric blip */
    {
        "pulse",
        "10",
        {
            "       111      ",
            "      10001     ",
            "       111      ",
            "        1       "
        }
    },

    /* grid – tight blocky mesh */
    {
        "grid",
        "#+",
        {
            "  #+#+#+#+#     ",
            "  +# +# +# +    ",
            "  #+#+#+#+#     ",
            "  +# +# +# +    "
        }
    },

    /* z-shape */
    {
        "zshape",
        "zZ",
        {
            " zzzzzzzzzzz   ",
            "        zzz    ",
            "      zzz      ",
            " zzzzzzzzzzz   "
        }
    },

    /* q-shape */
    {
        "qshape",
        "Q0",
        {
            "   QQQQQQQQ    ",
            "  QQ      QQ   ",
            "  QQ    Q QQ   ",
            "   QQQQQQQQQ   "
        }
    },

    /* hourglass */
    {
        "hourshape",
        "H8",
        {
            "  HHHHHHHHHH   ",
            "    HHHHHH     ",
            "    HHHHHH     ",
            "  HHHHHHHHHH   "
        }
    },

    /* cross */
    {
        "cross",
        "+X",
        {
            "     ++        ",
            "  ++++++++     ",
            "     ++        ",
            "     ++        "
        }
    },

    /* apple-ish */
    {
        "apple",
        "@0",
        {
            "    @@ @@      ",
            "   @0@0@0@     ",
            "   @00000@     ",
            "    @000@      "
        }
    },

    /* echo – ripples outward */
    {
        "echo",
        "10",
        {
            "       1        ",
            "      101       ",
            "     10001      ",
            "    1000001     "
        }
    },

    /* spine – vertical backbone */
    {
        "spine",
        "|1",
        {
            "         |      ",
            "        1|1     ",
            "         |      ",
            "        1|1     "
        }
    },

    /* halo – ring above a line */
    {
        "halo",
        "10",
        {
            "      11111     ",
            "     1  0 1     ",
            "      11111     ",
            "        1       "
        }
    },

    /* flare – asymmetric burst */
    {
        "flare",
        "*+",
        {
            "        *       ",
            "      *+*+*     ",
            "       +*+      ",
            "        *       "
        }
    },

    /* shard – jagged diagonal */
    {
        "shard",
        "/\\",
        {
            "       /        ",
            "      /\\       ",
            "        \\\\     ",
            "         \\\\    "
        }
    },

    /* tunnel – inset rectangles */
    {
        "tunnel",
        "10",
        {
            "     1111111    ",
            "     1 101 1    ",
            "     1 100 1    ",
            "     1111111    "
        }
    },

    /* static – TV snow chunk */
    {
        "static",
        "@#%*+",
        {
            "  @#%*+@#%*+    ",
            "  %*+@#%*+@#    ",
            "  #+@%*#+@%*    ",
            "  *+@#%*+@#%    "
        }
    },

    /* orbit – small core with satellites */
    {
        "orbit",
        "10",
        {
            "        1       ",
            "      1   1     ",
            "       111      ",
            "      1   1     "
        }
    },

    /* husk – broken box */
    {
        "husk",
        "10",
        {
            "     111111     ",
            "     1    01    ",
            "     10   1     ",
            "     111111     "
        }
    },

    /* core – compact solid block */
    {
        "core",
        "10",
        {
            "      1111      ",
            "      1001      ",
            "      1001      ",
            "      1111      "
        }
    },

    { NULL, NULL, { NULL, NULL, NULL, NULL } }
};

static const NoiseSet *active_noise = NULL;
static char user_char = '\0';
static unsigned long g_frame_counter = 0;
static int g_mask_shape = MASK_ELLIPSE;

static int noise_set_count(void) {
    int count = 0;
    while (noise_sets[count].name != NULL) {
        count++;
    }
    return count;
}

static const NoiseSet *pick_noise_by_hash(const char *seed) {
    int count = noise_set_count();
    if (count == 0) return NULL;

    unsigned long h = 5381;
    if (seed && *seed) {
        const unsigned char *p = (const unsigned char *)seed;
        while (*p) {
            h = ((h << 5) + h) + *p; /* djb2 */
            p++;
        }
    } else {
        h = (unsigned long)rng_u32();
    }

    return &noise_sets[h % count];
}

static void init_mask_shape(void) {
    g_mask_shape = rng_range(MASK_COUNT); /* rotate polygon per run */
}

static void set_default_config(AppConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->net_images = 1;
    cfg->fetch_count = 2;
    cfg->fetch_max = 50;
    snprintf(cfg->fetch_source, sizeof(cfg->fetch_source), "picsum");
    cfg->stats_count = 4;
    snprintf(cfg->stats_keys[0], sizeof(cfg->stats_keys[0]), "distro");
    snprintf(cfg->stats_keys[1], sizeof(cfg->stats_keys[1]), "kernel");
    snprintf(cfg->stats_keys[2], sizeof(cfg->stats_keys[2]), "uptime");
    snprintf(cfg->stats_keys[3], sizeof(cfg->stats_keys[3]), "mem");
}

static void load_app_config(AppConfig *cfg) {
    set_default_config(cfg);
    char path[512];
    const char *home = getenv("HOME");
    if (!home || !*home) return;
    if (!g_color_config_path[0]) {
        snprintf(g_color_config_path, sizeof(g_color_config_path),
                 "%s/.config/glitch/color.config", home);
    }
    snprintf(path, sizeof(path), "%s/.config/glitch/glitch.config", home);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        char *nl = strchr(val, '\n');
        if (nl) *nl = '\0';

        if (strcmp(key, "NET_IMAGES") == 0) {
            cfg->net_images = atoi(val) != 0;
        } else if (strcmp(key, "FETCH_SOURCE") == 0) {
            snprintf(cfg->fetch_source, sizeof(cfg->fetch_source), "%s", val);
        } else if (strcmp(key, "FETCH_COUNT") == 0) {
            int c = atoi(val);
            if (c > 0 && c <= 10) cfg->fetch_count = c;
        } else if (strcmp(key, "FETCH_MAX") == 0) {
            int m = atoi(val);
            if (m > 0 && m <= 200) cfg->fetch_max = m;
        } else if (strcmp(key, "LOCAL_IMAGES_DIR") == 0) {
            snprintf(cfg->local_dir, sizeof(cfg->local_dir), "%s", val);
        } else if (strcmp(key, "COLOR_CONFIG") == 0) {
            if (*val) {
                snprintf(g_color_config_path, sizeof(g_color_config_path), "%s", val);
            }
        } else if (strcmp(key, "STATS") == 0) {
            int count = 0;
            char *tok = strtok(val, ",");
            while (tok && count < MAX_STATS) {
                while (*tok == ' ' || *tok == '\t') tok++;
                if (*tok) {
                    snprintf(cfg->stats_keys[count], sizeof(cfg->stats_keys[count]), "%s", tok);
                    count++;
                }
                tok = strtok(NULL, ",");
            }
            if (count > 0) {
                cfg->stats_count = count;
            }
        }
    }
    fclose(f);

    if (cfg->local_dir[0]) {
        setenv("GLITCH_VARIANT_DIR", cfg->local_dir, 1);
    }
}

static const char *default_variant_dir(char *buf, size_t buf_size) {
    const char *dir = getenv("GLITCH_VARIANT_DIR");
    if (dir && *dir) {
        snprintf(buf, buf_size, "%s", dir);
        return buf;
    }

    const char *home = getenv("HOME");
    if (!home || !*home) return NULL;

    snprintf(buf, buf_size, "%s/.config/glitch/variants", home);
    return buf;
}

static int variant_exists(const char *dir, const char *noise_name) {
    if (!dir || !noise_name) return 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.png", dir, noise_name);
    return access(path, R_OK) == 0 && readable_png(path);
}

/* Pick a variant (noise + matching PNG) from ~/.config/glitch/variants.
 * - If GLITCH_VARIANT matches a PNG, it wins and also drives GLITCH_NOISE.
 * - If GLITCH_NOISE is already set, we only try to attach a matching PNG.
 * - Otherwise, we pick a random available variant and set GLITCH_NOISE.
 * Returns 1 if it set img_buf to a readable image path, 0 otherwise.
 */
static int select_variant_image(int noise_locked, char *img_buf, size_t buf_size) {
    char dir_buf[512];
    const char *variant_dir = default_variant_dir(dir_buf, sizeof(dir_buf));
    if (!variant_dir) return 0;

    const char *forced_variant = getenv("GLITCH_VARIANT");
    const char *forced_noise   = getenv("GLITCH_NOISE");

    if (forced_variant && *forced_variant && variant_exists(variant_dir, forced_variant)) {
        snprintf(img_buf, buf_size, "%s/%s.png", variant_dir, forced_variant);
        setenv("GLITCH_NOISE", forced_variant, 1);
        return 1;
    }

    if (forced_noise && *forced_noise && noise_locked && variant_exists(variant_dir, forced_noise)) {
        snprintf(img_buf, buf_size, "%s/%s.png", variant_dir, forced_noise);
        return 1;
    }

    const char *candidates[64];
    int count = 0;

    for (int i = 0; noise_sets[i].name != NULL && count < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (variant_exists(variant_dir, noise_sets[i].name)) {
            candidates[count++] = noise_sets[i].name;
        }
    }

    if (count == 0 || noise_locked) {
        return 0;
    }

    const char *choice = candidates[rng_range(count)];
    snprintf(img_buf, buf_size, "%s/%s.png", variant_dir, choice);
    setenv("GLITCH_NOISE", choice, 1);
    return 1;
}

static void init_noise_mode(void) {
    const char *mode = getenv("GLITCH_NOISE");

    if (!mode || !*mode) {
        const NoiseSet *pick = pick_noise_by_hash(NULL);
        active_noise = pick ? pick : &noise_sets[0];
        return;
    }

    for (int i = 0; noise_sets[i].name != NULL; ++i) {
        if (strcmp(mode, noise_sets[i].name) == 0) {
            active_noise = &noise_sets[i];
            return;
        }
    }

    /* unknown mode -> deterministic pick from the catalogue */
    const NoiseSet *pick = pick_noise_by_hash(mode);
    active_noise = pick ? pick : &noise_sets[0];
}

static void init_symbol(void) {
    const char *sym = getenv("GLITCH_CHAR");
    if (sym && *sym) {
        user_char = *sym;      /* first byte only */
    }
}

static int term_columns(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    const char *env = getenv("COLUMNS");
    if (env) {
        int c = atoi(env);
        if (c > 0) return c;
    }
    return 80;
}

static void print_entropy_footer(int img_gap) {
    const char *label = "ENTROPY:";
    const char *bar_color = bg_code(0); /* darkest palette entry */
    if (!bar_color || !*bar_color) bar_color = BG1;
    char bar_color_fg[COLOR_CODE_LEN];
    bg_to_fg(bar_color, bar_color_fg, sizeof(bar_color_fg));
    const int label_field = 6; /* matches stat label width */
    const int pipe_col = 1 + label_field + 2; /* space + label + gap */
    int pad = SHAPE_COLS + img_gap;
    int cols = term_columns();
    int label_len = (int)strlen(label);
    int sep_spaces = pipe_col - (1 + label_len);
    if (sep_spaces < 0) sep_spaces = 0;

    double pct = (double)g_entropy_current / (double)ENTROPY_TARGET_BYTES;
    if (pct > 1.0) pct = 1.0;
    int percent = (int)(pct * 100.0 + 0.5);
    size_t remaining = (g_entropy_current >= ENTROPY_TARGET_BYTES) ? 0 : (ENTROPY_TARGET_BYTES - g_entropy_current);
    int runs_left = (int)((remaining + ENTROPY_ADD_BYTES - 1) / ENTROPY_ADD_BYTES);
    if (runs_left < 0) runs_left = 0;
    int eta_sec = runs_left; /* coarse estimate: one run ~ one unit */

    char suffix[64];
    (void)eta_sec;
    snprintf(suffix, sizeof(suffix), " %3d%%", percent);
    int suffix_len = (int)strlen(suffix);

    /* keep headroom so the bar doesn't wrap */
    int prefix = 1 + label_len + sep_spaces + 3; /* space + label + sep + "|_ " */
    int available = cols - pad - prefix - 2 - suffix_len; /* minus [] and suffix */
    if (available < 6) return;
    int bar_cells = available / 6; /* shorten further */
    if (bar_cells < 6) bar_cells = available < 6 ? available : 6;
    if (bar_cells > available) bar_cells = available;

    int filled = (int)(pct * (double)bar_cells + 0.5);
    if (filled > bar_cells) filled = bar_cells;

    const char *palette_bg[4] = { bg_code(0), bg_code(1), bg_code(2), bg_code(3) };
    char palette_fg[4][COLOR_CODE_LEN];
    for (int i = 0; i < 4; ++i) {
        if (!palette_bg[i] || !*palette_bg[i]) {
            palette_bg[i] = palette_bg[0] ? palette_bg[0] : BG1;
        }
        bg_to_fg(palette_bg[i], palette_fg[i], sizeof(palette_fg[i]));
    }

    int label_pad = pad; /* match stat indent */
    if (label_pad < 0) label_pad = 0;
    for (int i = 0; i < label_pad; ++i) putchar(' ');
    const char *sep_fg = (bar_color_fg[0] != '\0') ? bar_color_fg : fg_code(3);
    printf(" %s\033[2m%s%*s", fg_code(3), label, sep_spaces, "");
    if (sep_fg && *sep_fg) printf("%s", sep_fg);
    printf("|\033[0m\033[2m_\033[0m \033[2m[\033[0m");
    for (int i = 0; i < bar_cells; ++i) {
        int filled_cell = (i < filled);
        if (filled_cell) {
            int idx = rng_range(4);
            const char *cbg = palette_bg[idx];
            const char *cfg = palette_fg[idx];
            if ((!cbg || !*cbg) && bar_color && *bar_color) cbg = bar_color;
            if (!cfg || !*cfg) cfg = fg_code(idx % 4);
            if ((!cfg || !*cfg) && bar_color_fg[0]) cfg = bar_color_fg;
            if (cbg && *cbg) printf("%s", cbg);
            if (cfg && *cfg) printf("%s", cfg);
            printf("\033[2m█\033[0m  "); /* dim block, keep spacing */
        } else {
            printf("   ");       /* match cell width when empty */
        }
    }
    printf(F_RESET "\033[2m]\033[0m \033[2m%s\033[0m\n", suffix);
}

/*
 * Fill one glyph row:
 * - If the mode has a template, use it as a mask.
 * - If user_char is set, use it for non-space cells and animate them
 *   deterministically using g_frame_counter so veins appear to "flow".
 * - Otherwise, use the mode's charset with mild random jitter.
 */
static void fill_noise_row(int line, char *out, int width) {
    if (!active_noise) {
        active_noise = &noise_sets[0];
    }

    const char *chars = active_noise->chars;
    if (!chars || !*chars) {
        chars = "@#%*&";
    }
    int charset_len = (int)strlen(chars);

    const char *tmpl = NULL;
    if (line >= 0 && line < LINES) {
        tmpl = active_noise->rows[line];
    }

    /* No template: full noise field */
    if (!tmpl) {
        for (int i = 0; i < width; ++i) {
            if (user_char) {
                int phase = (int)((g_frame_counter + line + i) % 5);
                out[i] = (phase < 3) ? user_char : ' ';
            } else {
                out[i] = chars[rng_range(charset_len)];
            }
        }
        out[width] = '\0';
        return;
    }

    /* Start with the template silhouette */
    int len = (int)strlen(tmpl);
    if (len > width) len = width;

    memcpy(out, tmpl, len);
    for (int i = len; i < width; ++i) {
        out[i] = ' ';
    }

    if (user_char) {
        /* Custom symbol mode:
         * - Template defines where veins are.
         * - Phase function makes them pulse / flow.
         */
        for (int i = 0; i < width; ++i) {
            if (out[i] == ' ') {
                continue;
            }
            int phase = (int)((g_frame_counter + line + i) % 6);
            if (phase == 0) {
                out[i] = ' ';
            } else {
                out[i] = user_char;
            }
        }
    } else {
        /* Normal mode: template + light random jitter */
        for (int i = 0; i < width; ++i) {
            if (out[i] == ' ') continue;
            if ((rng_range(100)) < JITTER_PCT && charset_len > 0) {
                out[i] = chars[rng_range(charset_len)];
            }
        }
    }

    out[width] = '\0';
}

static int mask_inside(int shape, int x, int y, int width, int total_rows) {
    if (width <= 1 || total_rows <= 1) return 1;

    double a = (width - 1) / 2.0;
    double b = (total_rows - 1) / 2.0;
    double cx = a;
    double cy = b;

    double dx = x - cx;
    double dy = y - cy;
    double adx = dx < 0 ? -dx : dx;
    double ady = dy < 0 ? -dy : dy;

    switch (shape) {
        case MASK_RECT:
            return 1;
        case MASK_ELLIPSE: {
            double v = (dx * dx) / (a * a) + (dy * dy) / (b * b);
            return v <= 1.0;
        }
        case MASK_DIAMOND:
            return (adx / a + ady / b) <= 1.0;
        case MASK_TRI_UP: {
            double span = (double)y / (double)(total_rows - 1);
            double half = a * (1.0 - span);
            return adx <= half;
        }
        case MASK_TRI_DOWN: {
            double span = (double)(total_rows - 1 - y) / (double)(total_rows - 1);
            double half = a * (1.0 - span);
            return adx <= half;
        }
        case MASK_TRI_LEFT: {
            double span = (double)x / (double)(width - 1);
            double half = b * (1.0 - span);
            return ady <= half;
        }
        case MASK_TRI_RIGHT: {
            double span = (double)(width - 1 - x) / (double)(width - 1);
            double half = b * (1.0 - span);
            return ady <= half;
        }
        case MASK_TRAP_UP: {
            double t = (double)y / (double)(total_rows - 1);
            double half = a * (0.6 + 0.4 * t); /* narrow top, wide bottom */
            return adx <= half;
        }
        case MASK_TRAP_DOWN: {
            double t = (double)(total_rows - 1 - y) / (double)(total_rows - 1);
            double half = a * (0.6 + 0.4 * t); /* narrow bottom, wide top */
            return adx <= half;
        }
        case MASK_TRAP_LEFT: {
            double t = (double)x / (double)(width - 1);
            double half = b * (0.6 + 0.4 * t);
            return ady <= half;
        }
        case MASK_TRAP_RIGHT: {
            double t = (double)(width - 1 - x) / (double)(width - 1);
            double half = b * (0.6 + 0.4 * t);
            return ady <= half;
        }
        case MASK_HOURGLASS: {
            double m = ady / b;
            if (m > 1.0) m = 1.0;
            double half = a * (0.25 + 0.75 * m);
            return adx <= half;
        }
        case MASK_HEX: {
            double hband = b / 3.0;
            if (ady <= hband) return 1;
            double shrink = (ady - hband) / hband;
            double half = a * (1.0 - shrink);
            return adx <= half && shrink <= 1.0;
        }
        case MASK_OCT: {
            double corner = (a < b ? a : b) * 0.35;
            if ((adx <= a - corner && ady <= b) || (ady <= b - corner && adx <= a)) return 1;
            return (adx + ady) <= (a + b - corner);
        }
        case MASK_CHEV_UP:
            return dy <= 0 && ady >= (adx * (b / a) * 0.6);
        case MASK_CHEV_DOWN:
            return dy >= 0 && ady >= (adx * (b / a) * 0.6);
        case MASK_CHEV_LEFT:
            return dx <= 0 && adx >= (ady * (a / b) * 0.6);
        case MASK_CHEV_RIGHT:
            return dx >= 0 && adx >= (ady * (a / b) * 0.6);
        case MASK_WAVE: {
            double xn = dx / a; /* -1..1 */
            double yn = dy / b; /* signed */
            double wave = 0.35 * sin(3.14159 * xn);
            return (yn <= wave + 0.4) && (yn >= wave - 0.4);
        }
        case MASK_NOTCH_TOP: {
            double notch_h = b * 0.35;
            double notch_w = a * 0.35;
            if (y < notch_h && adx < notch_w) return 0;
            return 1;
        }
        case MASK_NOTCH_BOTTOM: {
            double notch_h = b * 0.35;
            double notch_w = a * 0.35;
            if ((total_rows - 1 - y) < notch_h && adx < notch_w) return 0;
            return 1;
        }
        case MASK_PILL: {
            double r = (a < b ? a : b) * 0.6;
            if (adx <= a - r || ady <= b - r) return 1;
            return (adx - (a - r)) * (adx - (a - r)) + (ady - (b - r)) * (ady - (b - r)) <= r * r;
        }
        case MASK_STAIRS_UP: {
            double step = (double)(y) / (double)(total_rows - 1);
            double limit = step * (double)(width);
            return x <= limit;
        }
        case MASK_STAIRS_DOWN: {
            double step = (double)(total_rows - 1 - y) / (double)(total_rows - 1);
            double limit = step * (double)(width);
            return x <= limit;
        }
        default:
            return 1;
    }
}

/* Print the noise row with a polygonal mask so the color block changes shape
 * each run (ellipse/diamond/triangles/rectangle), framing the PNG area.
 */
static void print_noise_row(const char *row_buf,
                            int width,
                            int palette_idx,
                            int row_idx,
                            int total_rows) {
    if (!row_buf || width <= 0) return;

    const char *bg = bg_code(palette_idx);
    int inside_prev = 0;

    printf("\033[2m"); /* dim noise */
    for (int x = 0; x < width; ++x) {
        int inside = mask_inside(g_mask_shape, x, row_idx, width, total_rows);

        if (inside && !inside_prev && bg && *bg) {
            printf("%s", bg);
        } else if (!inside && inside_prev) {
            printf(F_RESET "\033[2m");
        }

        char ch = row_buf[x];
        if (ch == '\0') ch = ' ';
        putchar(inside ? ch : ' ');
        inside_prev = inside;
    }

    printf(F_RESET);
}

/* ---------- Kitty image support (PNG via file path) ---------- */

static int term_supports_kitty_images(void) {
#ifdef MINIMAL_BUILD
    return 0;
#else
    const char *term = getenv("TERM");
    const char *prog = getenv("TERM_PROGRAM");

    if (term) {
        if (strstr(term, "kitty") || strstr(term, "ghostty") || strstr(term, "wezterm")) {
            return 1;
        }
    }
    if (prog) {
        if (strstr(prog, "Ghostty") || strstr(prog, "WezTerm") || strstr(prog, "kitty")) {
            return 1;
        }
    }

    if (getenv("GLITCH_FORCE_KITTY")) {
        return 1;
    }

    return 0;
#endif
}

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_encode(const unsigned char *in, int len, char *out, int out_size) {
    int i = 0;
    int j = 0;

    while (i < len && (j + 4) < out_size) {
        int rem = len - i;
        uint32_t octet_a = in[i++];
        uint32_t octet_b = (rem > 1) ? in[i++] : 0;
        uint32_t octet_c = (rem > 2) ? in[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (rem > 1) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (rem > 2) ? b64_table[triple & 0x3F] : '=';
    }

    if (j < out_size) {
        out[j] = '\0';
    }

    return j;
}

static int readable_png(const char *path) {
    if (!path) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char sig[8] = {0};
    ssize_t n = read(fd, sig, 8);
    close(fd);
    const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8) return 0;
    return memcmp(sig, png_sig, 8) == 0;
}

/* Print a PNG using Kitty graphics protocol in "file" mode.
 * - path is not the raw image; it's base64(path) sent to the terminal.
 * - We assume PNG (f=100).
 */
/* ---------- Frame rendering ---------- */

static void
print_kitty_image_file(const char *path, int stat_rows)
{
#ifdef MINIMAL_BUILD
	(void)path;
	(void)stat_rows;
	return;
#else
	char b64[2048];
	int len, out_len;
	int img_rows, img_cols;
	int target_row, target_col;

	if (!path || !*path)
		return;
	if (!term_supports_kitty_images())
		return;
	if (stat_rows <= 0)
		stat_rows = 1;
	img_rows = stat_rows;
	img_cols = IMG_DRAW_WIDTH;

	target_row = FRAME_TOP_ROW + IMG_ROW;
	target_col = SHAPE_COLS + IMG_PAD + 1;
	if (target_col < 1)
		target_col = 1;

	len = (int)strlen(path);
	if (len <= 0 || len > 512)
		return;
	out_len = b64_encode((const unsigned char *)path, len, b64, sizeof(b64));
	if (out_len <= 0)
		return;

	printf("\033[s");
	printf("\033[%d;%dH", target_row, target_col);
	printf("\033_Ga=T,f=100,t=f,c=%d,r=%d,C=1,q=2;%s\033\\", img_cols, img_rows, b64);
	printf("\033[u");
	fflush(stdout);
#endif
}


static void print_frame(int frame,
                        const StatEntry *stats,
                        int stat_count,
                        int have_image) {
    (void)frame; /* using global g_frame_counter for animation */

    if (stat_count < 1) stat_count = 1;
    if (stat_count > MAX_STATS) stat_count = MAX_STATS;

    int img_cols = have_image ? IMG_DRAW_WIDTH : 0;
    int img_gap = have_image ? (IMG_PAD * 2 + img_cols) : 1; /* left/right pad + image lane */
    if (img_gap < 1) img_gap = 1;

    for (int i = 0; i < stat_count; ++i) {
        const StatEntry *st = &stats[i];
        int palette_idx = i % 4;
        char line_buf[SHAPE_COLS + 1];
        fill_noise_row(i, line_buf, SHAPE_COLS);

        print_noise_row(line_buf, SHAPE_COLS, palette_idx, i, stat_count);
        print_img_gap_line(palette_idx, i, stat_count, img_gap, have_image);
        printf(" %s\033[2m%-6s  | %s\033[0m\n", fg_code(palette_idx), st->label, st->value);
    }

    int pad_gap = have_image ? (IMG_PAD * 2 + IMG_DRAW_WIDTH) : 1;
    print_entropy_footer(pad_gap);
    printf("\e[0m\n");
}









int main(int argc, char **argv) {
    int once = 0;
    useconds_t delay = 50000; /* default 50ms */
    int noise_locked = 0;
    unsigned long duration_ms = 1500; /* default run ~1.5 seconds */
    AppConfig cfg;
    load_app_config(&cfg);

    uint64_t seed = ((uint64_t)time(NULL) << 32) ^ (uint64_t)getpid();
    get_random_bytes(&seed, sizeof(seed));
    rng_seed(seed);
    init_mask_shape();

    const char *env_noise = getenv("GLITCH_NOISE");
    if (env_noise && *env_noise) {
        noise_locked = 1;
    }

    char chosen_variant_path[1024] = {0};
    if (!noise_locked) {
        char variant_name[256] = {0};
        if (choose_random_variant(variant_name, sizeof(variant_name), chosen_variant_path, sizeof(chosen_variant_path))) {
            /* strip .png for GLITCH_NOISE/VARIANT */
            char base[256] = {0};
            snprintf(base, sizeof(base), "%s", variant_name);
            char *dot = strrchr(base, '.');
            if (dot) *dot = '\0';
            if (*base) {
                setenv("GLITCH_NOISE", base, 1);
                setenv("GLITCH_VARIANT", base, 1);
                noise_locked = 1;
            }
        }
    }

    /* env override for speed */
    char *env = getenv("GLITCH_SPEED");
    if (env) {
        int ms = atoi(env);
        if (ms > 0) {
            delay = (useconds_t)ms * 1000;
        }
    }

    char *env_duration = getenv("GLITCH_DURATION_MS");
    if (env_duration) {
        long ms = atol(env_duration);
        if (ms >= 0) {
            duration_ms = (unsigned long)ms;
        }
    }

    /* subcommands for entropy/passphrase/keyfile */
    if (argc >= 2) {
        if (strcmp(argv[1], "entropy") == 0) {
            long n = (argc >= 3) ? atol(argv[2]) : 32;
            if (n < 0) n = 0;
            gen_entropy_bytes((size_t)n);
            return 0;
        } else if (strcmp(argv[1], "gen-pass") == 0) {
            int len = (argc >= 3) ? atoi(argv[2]) : 48;
            if (len < 12) len = 12;
            if (len > 96) len = 96;
            gen_passphrase(len);
            return 0;
        } else if (strcmp(argv[1], "gen-keyfile") == 0) {
            long n = (argc >= 3) ? atol(argv[2]) : 64;
            if (n < 16) n = 16;
            if (n > 4096) n = 4096;
            gen_entropy_bytes((size_t)n);
            return 0;
        } else if (strcmp(argv[1], "entropy-cache") == 0) {
            const char *home = getenv("HOME");
            if (!home || !*home) return 1;
            long n = (argc >= 3) ? atol(argv[2]) : 2048;
            if (n < 64) n = 64;
            if (n > 8192) n = 8192;
            entropy_cache(home, (size_t)n);
            return 0;
        }
    }

    /* args */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            int ms = atoi(argv[++i]);
            if (ms > 0) {
                delay = (useconds_t)ms * 1000;
            }
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            long ms = atol(argv[++i]);
            if (ms >= 0) {
                duration_ms = (unsigned long)ms;
            }
        } else if (strcmp(argv[i], "--fetch") == 0 || strcmp(argv[i], "--fetch-only") == 0) {
            run_fetcher(&cfg);
            if (strcmp(argv[i], "--fetch-only") == 0) {
                printf("[+] fetched variants\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--shape") == 0 && i + 1 < argc) {
            ++i; /* consumed, currently unused */
        } else if (strcmp(argv[i], "--noise") == 0 && i + 1 < argc) {
            setenv("GLITCH_NOISE", argv[++i], 1);
            noise_locked = 1;
        } else if (strcmp(argv[i], "--char") == 0 && i + 1 < argc) {
            setenv("GLITCH_CHAR", argv[++i], 1);
        }
    }

    /* refresh variants if enabled */
    run_fetcher(&cfg);

    /* Resolve image path (optional) */
    char img_buf[1024];
    const char *img_path = NULL;
    int variant_has_image = select_variant_image(noise_locked, img_buf, sizeof(img_buf));

    if (variant_has_image) {
        img_path = img_buf;
        noise_locked = 1; /* variant sets GLITCH_NOISE */
    } else if (chosen_variant_path[0]) {
        img_path = chosen_variant_path;
    }

    if (!img_path) {
        img_path = getenv("GLITCH_IMAGE_PATH");
    }

    if (!img_path || !*img_path) {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(img_buf, sizeof(img_buf), "%s/.config/glitch/logo.png", home);
            img_path = img_buf;
        } else {
            img_path = NULL;
        }
    }

    init_noise_mode();
    init_symbol();

    const char *palette_img = (img_path && *img_path && access(img_path, R_OK) == 0) ? img_path : NULL;
    init_palette(palette_img);

    int have_image = 0;
    if (img_path && *img_path && term_supports_kitty_images() && access(img_path, R_OK) == 0 && readable_png(img_path)) {
        have_image = 1;
    }

    /* Gather system info once per frame loop */
    struct utsname un;
    struct sysinfo info;

    char distro[128]    = {0};
    char kernel[128]    = {0};
    char uptime_buf[64] = {0};
    char mem_buf[64]    = {0};
    char cpu_name[128]  = {0};
    read_cpu_name(cpu_name, sizeof(cpu_name));
    StatEntry stats[MAX_STATS];
    int stats_count = 0;

    /* hide cursor */
    printf("\e[?25l");

    if (once) {
        if (uname(&un) != 0) {
            memset(&un, 0, sizeof(un));
            strcpy(un.sysname, "unknown");
            strcpy(un.release, "unknown");
        }

        if (sysinfo(&info) != 0) {
            memset(&info, 0, sizeof(info));
        }

        /* distro */
        FILE *osrelease = fopen("/etc/os-release", "r");
        if (!osrelease) {
            snprintf(distro, sizeof(distro), "%s", un.sysname);
        } else {
            char line[256];
            int set = 0;
            while (fgets(line, sizeof(line), osrelease)) {
                if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char *v = strchr(line, '=');
                    if (v) {
                        v++;
                        if (*v == '\"') v++;
                        char *end = strrchr(v, '\"');
                        if (end) *end = '\0';
                        char *nl = strchr(v, '\n');
                        if (nl) *nl = '\0';
                        snprintf(distro, sizeof(distro), "%s", v);
                        set = 1;
                    }
                    break;
                }
                if (strncmp(line, "ID=", 3) == 0 && !set) {
                    char *v = strchr(line, '=');
                    if (v) {
                        v++;
                        char *nl = strchr(v, '\n');
                        if (nl) *nl = '\0';
                        snprintf(distro, sizeof(distro), "%s", v);
                        set = 1;
                    }
                    break;
                }
            }
            fclose(osrelease);
            if (!set) {
                snprintf(distro, sizeof(distro), "%s", un.sysname);
            }
        }

        snprintf(kernel, sizeof(kernel), "%s", un.release);

        long uptime = info.uptime;
        int days    = uptime / 86400;
        int hours   = (uptime % 86400) / 3600;
        int minutes = (uptime % 3600) / 60;

        if (days > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dd %dh %dm", days, hours, minutes);
        } else if (hours > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dh %dm", hours, minutes);
        } else if (minutes > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dm", minutes);
        } else {
            snprintf(uptime_buf, sizeof(uptime_buf), "%lds", uptime);
        }

        unsigned long long total =
            (unsigned long long)info.totalram * info.mem_unit;
        unsigned long long free_mem =
            (unsigned long long)(info.freeram + info.bufferram + info.freeswap) * info.mem_unit;

        double used_gib  = (double)(total - free_mem) / 1073741824.0;
        double total_gib = (double)total / 1073741824.0;

        snprintf(mem_buf, sizeof(mem_buf), "%.2f GiB / %.2f GiB", used_gib, total_gib);

        build_stats(&cfg, distro, kernel, uptime_buf, mem_buf, cpu_name, &un, stats, &stats_count);

        printf("\e[H\e[2J");
        if (have_image) {
            print_kitty_image_file(img_path, stats_count);
        }
        print_frame(0, stats, stats_count, have_image);
        printf("\e[?25h");
        return 0;
    }

    int frame = 0;
    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    while (1) {
        if (uname(&un) != 0) {
            memset(&un, 0, sizeof(un));
            strcpy(un.sysname, "unknown");
            strcpy(un.release, "unknown");
        }

        if (sysinfo(&info) != 0) {
            memset(&info, 0, sizeof(info));
        }

        /* distro */
        FILE *osrelease = fopen("/etc/os-release", "r");
        if (!osrelease) {
            snprintf(distro, sizeof(distro), "%s", un.sysname);
        } else {
            char line[256];
            int set = 0;
            while (fgets(line, sizeof(line), osrelease)) {
                if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char *v = strchr(line, '=');
                    if (v) {
                        v++;
                        if (*v == '\"') v++;
                        char *end = strrchr(v, '\"');
                        if (end) *end = '\0';
                        char *nl = strchr(v, '\n');
                        if (nl) *nl = '\0';
                        snprintf(distro, sizeof(distro), "%s", v);
                        set = 1;
                    }
                    break;
                }
                if (strncmp(line, "ID=", 3) == 0 && !set) {
                    char *v = strchr(line, '=');
                    if (v) {
                        v++;
                        char *nl = strchr(v, '\n');
                        if (nl) *nl = '\0';
                        snprintf(distro, sizeof(distro), "%s", v);
                        set = 1;
                    }
                    break;
                }
            }
            fclose(osrelease);
            if (!set) {
                snprintf(distro, sizeof(distro), "%s", un.sysname);
            }
        }

        snprintf(kernel, sizeof(kernel), "%s", un.release);

        long uptime = info.uptime;
        int days    = uptime / 86400;
        int hours   = (uptime % 86400) / 3600;
        int minutes = (uptime % 3600) / 60;

        if (days > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dd %dh %dm", days, hours, minutes);
        } else if (hours > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dh %dm", hours, minutes);
        } else if (minutes > 0) {
            snprintf(uptime_buf, sizeof(uptime_buf), "%dm", minutes);
        } else {
            snprintf(uptime_buf, sizeof(uptime_buf), "%lds", uptime);
        }

        unsigned long long total =
            (unsigned long long)info.totalram * info.mem_unit;
        unsigned long long free_mem =
            (unsigned long long)(info.freeram + info.bufferram + info.freeswap) * info.mem_unit;

        double used_gib  = (double)(total - free_mem) / 1073741824.0;
        double total_gib = (double)total / 1073741824.0;

        snprintf(mem_buf, sizeof(mem_buf), "%.2f GiB / %.2f GiB", used_gib, total_gib);

        build_stats(&cfg, distro, kernel, uptime_buf, mem_buf, cpu_name, &un, stats, &stats_count);

        printf("\e[H\e[2J");
        if (have_image) {
            print_kitty_image_file(img_path, stats_count);
        }
        print_frame(frame, stats, stats_count, have_image);

        fflush(stdout);
        frame = (frame + 1) % FRAMES;
        g_frame_counter++;
        usleep(delay);

        if (duration_ms > 0) {
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            unsigned long elapsed_ms =
                (unsigned long)((now_ts.tv_sec - start_ts.tv_sec) * 1000UL +
                                (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000UL);
            if (elapsed_ms >= duration_ms) {
                break;
            }
        }
    }

    /* not reached */
    printf("\e[?25h");
    return 0;
}
