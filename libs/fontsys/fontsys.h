/*
 * fontsys - system font lookup and SDF glyph rendering (FreeType).
 *
 * Plain C, no HashLink types: the HashLink bindings live in fontsys_hl.c.
 * All paths are UTF-8.
 *
 * Threads: a fontsys_ctx is not thread-safe. Use one context per thread
 * (each has its own FreeType library and face cache).
 *
 * Android: fontsys_find uses the NDK AFontMatcher (API 29+), which only works
 * in an app (zygote-forked) process; a plain executable run from adb shell
 * crashes in minikin (SIGSEGV). Set FONTSYS_FORCE_FONTS_XML=1 in the
 * environment of command-line tests to use the /system/etc/fonts.xml lookup.
 */
#ifndef FONTSYS_H
#define FONTSYS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fontsys_ctx fontsys_ctx;
typedef struct { int weight; int italic; int monospace; } fontsys_style;   /* weight 100..900 */
typedef struct {
    int width, height;   /* bitmap size in px, spread padding included */
    int left, top;       /* bitmap origin: px right of pen, px above baseline (padding included) */
    float advance;       /* px */
} fontsys_glyph;

fontsys_ctx *fontsys_create(void);
void fontsys_destroy(fontsys_ctx *ctx);
/* 1 found, 0 nothing covers it, <0 error. path is UTF-8.
   lang is a BCP 47 tag ("zh-Hans", "ja"); it picks the regional forms of Han
   characters. NULL or "" means the OS's own language:
     Windows: the user's default locale (GetUserDefaultLocaleName);
     macOS/iOS: the user's preferred language (CoreText's default);
     Linux: fontconfig's default language (FC_LANG, else LC_ALL/LC_CTYPE/LANG);
     Android: the device locale (persist.sys.locale, else ro.product.locale,
     else "en"). */
int fontsys_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                 char *path_out, int path_cap, int *index_out);
/* face id >= 0, or <0 on failure. Cached by (path, index). */
int fontsys_face_open(fontsys_ctx *ctx, const char *path, int index);
int fontsys_face_has(fontsys_ctx *ctx, int face, unsigned cp);          /* 1 / 0 */
/* 1 ok, 0 face lacks cp, -1 buffer too small (out->width/height still set), <0 error.
   buf receives width*height bytes, row-major, 255 = deepest inside, 128 = edge. */
int fontsys_render_sdf(fontsys_ctx *ctx, int face, unsigned cp, int em_px, int spread_px,
                       unsigned char *buf, int buf_cap, fontsys_glyph *out);
/* Face style queries (FreeType style_flags / FT_IS_FIXED_WIDTH): 1 / 0. Used by tests and Android matching. */
int fontsys_face_is_bold(fontsys_ctx *ctx, int face);
int fontsys_face_is_italic(fontsys_ctx *ctx, int face);
int fontsys_face_is_mono(fontsys_ctx *ctx, int face);

/* Colour emoji. 1 when the face has colour glyph data (COLR layers or
   colour bitmap strikes: sbix, CBDT), else 0. */
int fontsys_face_is_color(fontsys_ctx *ctx, int face);
/* Shapes the code points cps[0..n) with HarfBuzz (left to right, script and
   language guessed). 1 when they shape to exactly one glyph that is not
   .notdef (*glyph_out set to its glyph id): an emoji sequence the font
   joins (skin tone, ZWJ family, flag, keycap). 0 otherwise, <0 on error. */
int fontsys_shape_cluster(fontsys_ctx *ctx, int face, const unsigned *cps, int n,
                          unsigned *glyph_out);
/* Renders glyph (a glyph id, from fontsys_shape_cluster) in colour with one
   em = em_px pixels. 1 ok: buf receives width*height*4 bytes, RGBA with
   straight (not premultiplied) alpha, rows top to bottom. 0 the glyph has
   no colour image (outline only, COLRv1 only, or a PNG strike in a build
   without PNG). -1 buf too small (out->width/height set). <0 other errors.
   out->left/top/advance are in pixels at em_px; bitmap strikes are scaled
   to em_px. */
int fontsys_render_color(fontsys_ctx *ctx, int face, unsigned glyph, int em_px,
                         unsigned char *buf, int buf_cap, fontsys_glyph *out);
/* Number of OS lookup calls made by fontsys_find so far (test hook). */
int fontsys_debug_os_calls(fontsys_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
