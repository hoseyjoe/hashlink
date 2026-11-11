/* fontsys core tests (CTest: fontsys). Exit code 0 = all passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#   include <windows.h>
#   include <wchar.h>
#endif
#include "fontsys.h"
#include "fontsys_internal.h"
#include <hb.h>

static int failures = 0;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#if defined(_WIN32)
#   define DEFAULT_TEST_FONT "C:/Windows/Fonts/arial.ttf"
#elif defined(__APPLE__)
#   define DEFAULT_TEST_FONT "/System/Library/Fonts/Helvetica.ttc"
#elif defined(__ANDROID__)
#   define DEFAULT_TEST_FONT "/system/fonts/Roboto-Regular.ttf"
#else
#   define DEFAULT_TEST_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#endif

#define EM 32
#define SPREAD 8
#define NO_CP 0x10FFFD /* private use plane 16: no regular font covers it */
#define CP_ZHONG 0x4E2D /* U+4E2D: Han, glyph form differs by language */
#define PATH_CAP 1024

static const char *test_font(void) {
    const char *env = getenv("FONTSYS_TEST_FONT");
    return (env && *env) ? env : DEFAULT_TEST_FONT;
}

static void test_open(fontsys_ctx *ctx, const char *path, int *face_out) {
    int face = fontsys_face_open(ctx, path, 0);
    CHECK(face >= 0);
    CHECK(fontsys_face_open(ctx, path, 0) == face);
    CHECK(fontsys_face_open(ctx, "fontsys-no-such-dir/no-such-font.ttf", 0) < 0);
    *face_out = face;
}

#ifdef _WIN32
/* UTF-8 paths outside the ANSI code page: copy the font to
   %TEMP%\fontsys_<U+6D4B U+8BD5>.ttf and open it by its UTF-8 name. */
static void test_open_non_ascii(const char *path) {
    wchar_t wsrc[MAX_PATH], wdst[MAX_PATH + 32];
    char dst[4 * (MAX_PATH + 32)];
    fontsys_ctx *ctx;

    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wsrc, MAX_PATH)) { CHECK(0); return; }
    if (!GetTempPathW(MAX_PATH, wdst)) { CHECK(0); return; }
    wcscat_s(wdst, MAX_PATH + 32, L"fontsys_\x6D4B\x8BD5.ttf");
    if (!CopyFileW(wsrc, wdst, FALSE)) { CHECK(0); return; }
    CHECK(WideCharToMultiByte(CP_UTF8, 0, wdst, -1, dst, (int)sizeof(dst), NULL, NULL) > 0);

    ctx = fontsys_create();
    {
        int face = fontsys_face_open(ctx, dst, 0);
        CHECK(face >= 0);
        CHECK(fontsys_face_has(ctx, face, 'H') == 1);
    }
    fontsys_destroy(ctx); /* closes the file */
    DeleteFileW(wdst);
}
#endif

static void test_coverage_and_style(fontsys_ctx *ctx, int face) {
    CHECK(fontsys_face_has(ctx, face, 'H') == 1);
    CHECK(fontsys_face_has(ctx, face, NO_CP) == 0);
    CHECK(fontsys_face_is_bold(ctx, face) == 0);
    CHECK(fontsys_face_is_italic(ctx, face) == 0);
    CHECK(fontsys_face_is_mono(ctx, face) == 0);
}

static void test_render(fontsys_ctx *ctx, int face) {
    fontsys_glyph g = {0};
    unsigned char *buf;
    int cap, r, x, y, cx, bar_row, bar_val, edge_found;

    /* size query: no buffer */
    r = fontsys_render_sdf(ctx, face, 'H', EM, SPREAD, NULL, 0, &g);
    CHECK(r == -1);
    CHECK(g.width > 16);
    CHECK(g.height > 16);
    if (r != -1 || g.width <= 0 || g.height <= 0) return;

    cap = g.width * g.height;
    buf = (unsigned char *)malloc(cap);
    r = fontsys_render_sdf(ctx, face, 'H', EM, SPREAD, buf, cap, &g);
    CHECK(r == 1);
    CHECK(g.advance > 10);
    CHECK(g.width >= 2 * SPREAD + 10);
    if (r != 1) { free(buf); return; }

    /* corner: at least one spread away from the ink, so fully outside */
    CHECK(buf[0] < 32);

    /* crossbar: within the ink rows, the centre column of an H is only
       inside the glyph across the crossbar; its deepest pixel is the middle. */
    cx = g.width / 2;
    bar_row = -1;
    bar_val = -1;
    for (y = SPREAD; y < g.height - SPREAD; y++) {
        int v = buf[y * g.width + cx];
        if (v > bar_val) { bar_val = v; bar_row = y; }
    }
    CHECK(bar_row >= 0);
    /* value = 128 + depth/spread*128, so a stroke t px thick peaks at
       128 + 8t here. A regular-weight crossbar is ~2.5 px at em 32 (Arial:
       148); > 140 means "inside by more than 0.75 px" (outside-high would
       read ~108). */
    CHECK(bar_val > 140);

    /* an edge (value near 128) crosses the crossbar row */
    edge_found = 0;
    if (bar_row >= 0) {
        for (x = 0; x < g.width; x++) {
            int v = buf[bar_row * g.width + x];
            if (v >= 112 && v <= 144) edge_found = 1;
        }
    }
    CHECK(edge_found);

    if (failures) {
        fprintf(stderr, "H: %dx%d left=%d top=%d advance=%.2f crossbar row=%d value=%d\n",
                g.width, g.height, g.left, g.top, g.advance, bar_row, bar_val);
    }
    free(buf);

    /* a space has no ink: an empty bitmap, but still an advance */
    {
        fontsys_glyph gs = {0};
        CHECK(fontsys_render_sdf(ctx, face, ' ', EM, SPREAD, NULL, 0, &gs) == 1);
        CHECK(gs.width * gs.height == 0);
        CHECK(gs.advance > 0);
    }

    /* a code point the face lacks */
    {
        unsigned char tiny[16];
        fontsys_glyph g2 = {0};
        CHECK(fontsys_render_sdf(ctx, face, NO_CP, EM, SPREAD, tiny, sizeof(tiny), &g2) == 0);
    }
}

#ifdef _WIN32
/* Glyphs whose contours overlap (variable fonts keep their overlaps, like
   CFF fonts such as Android's Noto Sans CJK). A distance field changes by at
   most 128/spread per pixel (16 at spread 8); FreeType's outline SDF
   renderer gets the inside test wrong at overlaps, which shows as holes in
   the strokes, i.e. jumps of 50+ between neighbours (Bahnschrift N: 58).
   40 leaves room for the bitmap renderer's edge approximation (<= 31). */
static void test_render_overlaps(void) {
    const char *path = "C:/Windows/Fonts/bahnschrift.ttf";
    const unsigned cps[] = {'N', 'M'};
    fontsys_ctx *ctx = fontsys_create();
    int face, i, x, y;

    CHECK(ctx != NULL);
    if (!ctx) return;
    face = fontsys_face_open(ctx, path, 0);
    CHECK(face >= 0);
    for (i = 0; face >= 0 && i < 2; i++) {
        static unsigned char buf[128 * 128];
        fontsys_glyph g = {0};
        int jump = 0;
        CHECK(fontsys_render_sdf(ctx, face, cps[i], EM, SPREAD, buf, sizeof(buf), &g) == 1);
        for (y = 0; y < g.height; y++) {
            for (x = 0; x < g.width; x++) {
                int v = buf[y * g.width + x];
                if (x + 1 < g.width && abs(v - buf[y * g.width + x + 1]) > jump)
                    jump = abs(v - buf[y * g.width + x + 1]);
                if (y + 1 < g.height && abs(v - buf[(y + 1) * g.width + x]) > jump)
                    jump = abs(v - buf[(y + 1) * g.width + x]);
            }
        }
        if (jump > 40) fprintf(stderr, "Bahnschrift %c: neighbour jump %d\n", (char)cps[i], jump);
        CHECK(jump <= 40);
    }
    fontsys_destroy(ctx);
}
#endif

/* Emoji. Windows' Segoe UI Emoji has monochrome outlines under its colour
   layers: an emoji renders as one outline SDF. Android's Noto Color Emoji is
   colour bitmaps (sbix on Android 17, CBDT before) with no outlines: its
   emoji are a miss (the caller draws a box), never an invisible zero-size
   glyph. FONTSYS_TEST_BITMAP_EMOJI names such a font
   (e.g. pulled from a device); without it, Android uses its own. */
#define CP_GRIN 0x1F600
static void test_render_emoji(void) {
    const char *bitmap_font = getenv("FONTSYS_TEST_BITMAP_EMOJI");
    static unsigned char buf[128 * 128];
    fontsys_ctx *ctx = fontsys_create();
    fontsys_glyph g;
    int face;

    CHECK(ctx != NULL);
    if (!ctx) return;
#ifdef _WIN32
    face = fontsys_face_open(ctx, "C:/Windows/Fonts/seguiemj.ttf", 0);
    CHECK(face >= 0);
    if (face >= 0) {
        memset(&g, 0, sizeof(g));
        CHECK(fontsys_render_sdf(ctx, face, CP_GRIN, EM, SPREAD, buf, sizeof(buf), &g) == 1);
        CHECK(g.width > EM / 2 && g.height > EM / 2);
    }
#endif
#ifdef __ANDROID__
    if (!bitmap_font || !*bitmap_font) bitmap_font = "/system/fonts/NotoColorEmoji.ttf";
#endif
    if (bitmap_font && *bitmap_font) {
        face = fontsys_face_open(ctx, bitmap_font, 0);
        CHECK(face >= 0);
        if (face >= 0) {
            CHECK(fontsys_face_has(ctx, face, CP_GRIN));
            memset(&g, 0, sizeof(g));
            /* FT_LOAD_NO_BITMAP finds no outline to load: -3, a miss */
            CHECK(fontsys_render_sdf(ctx, face, CP_GRIN, EM, SPREAD, buf, sizeof(buf), &g) <= 0);
        }
    }
    fontsys_destroy(ctx);
}

/* Colour emoji: a face's colour data, HarfBuzz joining a sequence into the
   font's one glyph for it, and colour rendering (RGBA, straight alpha). */
#define COLOR_EM 64
static int shape(fontsys_ctx *ctx, int face, const unsigned *cps, int n, unsigned *glyph) {
    *glyph = 0;
    return fontsys_shape_cluster(ctx, face, cps, n, glyph);
}

/* Some pixel is clearly coloured (not grey) and opaque; and some edge pixel
   has colour brighter than its alpha, which premultiplied data never has. */
static void check_color_pixels(const unsigned char *px, const fontsys_glyph *g) {
    int i, n = g->width * g->height, colourful = 0, straight = 0;
    for (i = 0; i < n; i++) {
        const unsigned char *p = px + i * 4;
        int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        if (p[3] > 200 && abs((int)p[0] - (int)p[2]) > 60) colourful = 1;
        if (p[3] >= 1 && p[3] <= 127 && mx > p[3]) straight = 1;
    }
    CHECK(colourful);
    CHECK(straight); /* straight alpha, not premultiplied */
}

static void check_flags_font(fontsys_ctx *ctx, const char *path, unsigned char *px, int cap) {
    static const unsigned gb[] = {0x1F1EC, 0x1F1E7};
    fontsys_glyph g;
    unsigned glyph;
    int face = fontsys_face_open(ctx, path, 0), r;
    CHECK(face >= 0);
    if (face < 0) return;
    CHECK(fontsys_face_is_color(ctx, face) == 1);
    CHECK(shape(ctx, face, gb, 2, &glyph) == 1);
    memset(&g, 0, sizeof(g));
    r = fontsys_render_color(ctx, face, glyph, COLOR_EM, px, cap, &g);
#ifdef _WIN32
    CHECK(r == 0); /* no PNG decoding on Windows */
#else
    CHECK(r == 1);
    if (r == 1) {
        CHECK(g.width >= COLOR_EM / 2 && g.width <= COLOR_EM * 2);
        check_color_pixels(px, &g);
    }
#endif
}

static void test_color_emoji(void) {
    static const unsigned thumb[] = {0x1F44D}, thumb_tone[] = {0x1F44D, 0x1F3FD};
    static const unsigned family[] = {0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467};
    static const unsigned technologist[] = {0x1F469, 0x200D, 0x1F4BB};
    static const unsigned scotland[] = {0x1F3F4, 0xE0067, 0xE0062, 0xE0073, 0xE0063, 0xE0074, 0xE007F};
    static const unsigned black_flag[] = {0x1F3F4};
    static const unsigned keycap[] = {0x31, 0xFE0F, 0x20E3}, heart[] = {0x2764, 0xFE0F};
    static const unsigned grin[] = {CP_GRIN}, ab[] = {0x41, 0x42}, a[] = {0x41};
    static unsigned char px[256 * 256 * 4];
    const char *flags_font = getenv("FONTSYS_TEST_BITMAP_EMOJI_FLAGS");
    fontsys_ctx *ctx = fontsys_create();
    fontsys_glyph g;
    unsigned glyph, plain;
    int emoji, text;

    CHECK(ctx != NULL);
    if (!ctx) return;
#ifdef _WIN32
    emoji = fontsys_face_open(ctx, "C:/Windows/Fonts/seguiemj.ttf", 0);
    text = fontsys_face_open(ctx, "C:/Windows/Fonts/arial.ttf", 0);
    CHECK(emoji >= 0 && text >= 0);
    if (emoji >= 0 && text >= 0) {
        CHECK(fontsys_face_is_color(ctx, emoji) == 1);
        CHECK(fontsys_face_is_color(ctx, text) == 0);

        CHECK(shape(ctx, emoji, thumb, 1, &plain) == 1);
        CHECK(shape(ctx, emoji, thumb_tone, 2, &glyph) == 1);
        CHECK(glyph != 0 && glyph != plain); /* the toned thumb, not the yellow one */
        CHECK(shape(ctx, emoji, technologist, 3, &glyph) == 1);
        /* Segoe has no subdivision flags. The tag characters are default
           ignorable, so they are hidden and Scotland is the black flag alone:
           the base, as UTS #51 suggests for a tag sequence a font lacks. */
        CHECK(shape(ctx, emoji, black_flag, 1, &plain) == 1);
        CHECK(shape(ctx, emoji, scotland, 7, &glyph) == 1 && glyph == plain);
        /* Segoe UI Emoji (Windows 11) has no gendered family glyphs: a
           sequence the font does not join is not one glyph. */
        CHECK(shape(ctx, emoji, family, 5, &glyph) == 0);
        CHECK(shape(ctx, emoji, keycap, 3, &glyph) == 1);
        CHECK(shape(ctx, emoji, heart, 2, &glyph) == 1);
        CHECK(shape(ctx, text, ab, 2, &glyph) == 0);

        CHECK(shape(ctx, emoji, grin, 1, &glyph) == 1);
        memset(&g, 0, sizeof(g));
        CHECK(fontsys_render_color(ctx, emoji, glyph, COLOR_EM, px, sizeof(px), &g) == 1);
        CHECK(g.width >= 48 && g.width <= 80 && g.height >= 48 && g.height <= 80);
        CHECK(g.advance > COLOR_EM / 2);
        check_color_pixels(px, &g);
        /* too small a buffer: -1 with the size set */
        memset(&g, 0, sizeof(g));
        CHECK(fontsys_render_color(ctx, emoji, glyph, COLOR_EM, px, 16, &g) == -1);
        CHECK(g.width > 0 && g.height > 0);

        CHECK(shape(ctx, text, a, 1, &glyph) == 1);
        CHECK(fontsys_render_color(ctx, text, glyph, COLOR_EM, px, sizeof(px), &g) == 0);
    }
#endif
#ifdef __ANDROID__
    emoji = fontsys_face_open(ctx, "/system/fonts/NotoColorEmoji.ttf", 0);
    CHECK(emoji >= 0);
    if (emoji >= 0) {
        CHECK(fontsys_face_is_color(ctx, emoji) == 1);
        CHECK(shape(ctx, emoji, family, 5, &glyph) == 1);
        CHECK(shape(ctx, emoji, thumb_tone, 2, &glyph) == 1);
        memset(&g, 0, sizeof(g));
        CHECK(fontsys_render_color(ctx, emoji, glyph, COLOR_EM, px, sizeof(px), &g) == 1);
        CHECK(g.width >= COLOR_EM / 2 && g.width <= COLOR_EM * 2);
        check_color_pixels(px, &g);
    }
    if (!flags_font || !*flags_font) flags_font = "/system/fonts/NotoColorEmojiFlags.ttf";
    {
        /* The app finds a flag's font from its first regional indicator: that face must be a
           colour font that joins the pair, or the flag draws as two letters. */
        static const unsigned gb[] = {0x1F1EC, 0x1F1E7};
        fontsys_style regular = {400, 0, 0};
        char path[PATH_CAP];
        int index = 0, face;
        CHECK(fontsys_find(ctx, 0x1F1EC, "en", &regular, path, PATH_CAP, &index) == 1);
        face = fontsys_face_open(ctx, path, index);
        CHECK(face >= 0);
        if (face >= 0) {
            CHECK(fontsys_face_is_color(ctx, face) == 1);
            CHECK(shape(ctx, face, gb, 2, &glyph) == 1);
        }
    }
#endif
    if (flags_font && *flags_font) check_flags_font(ctx, flags_font, px, sizeof(px));
    (void)emoji; (void)text; (void)plain; (void)g;
    fontsys_destroy(ctx);
}

/* HarfBuzz is compiled into fontsys_core. */
static void test_harfbuzz_linked(void) {
    unsigned major = 0, minor = 0, micro = 0;
    hb_version(&major, &minor, &micro);
    CHECK(major >= 8);
}

/* fontsys_file_face_index (private, used by the Apple lookup): the face of a
   font file named by PostScript name, else the first face that maps cp. */
static void test_file_face_index(const char *path) {
    fontsys_ctx *ctx = fontsys_create();

    CHECK(ctx != NULL);
    if (!ctx) return;
    /* no name match: the first face that maps the code point */
    CHECK(fontsys_file_face_index(ctx, path, "fontsys-no-such-name", 'H') == 0);
    CHECK(fontsys_file_face_index(ctx, path, NULL, 'H') == 0);
    /* no name match and no face maps it */
    CHECK(fontsys_file_face_index(ctx, path, "fontsys-no-such-name", NO_CP) == -1);
    CHECK(fontsys_file_face_index(ctx, "fontsys-no-such-dir/no-such-font.ttf", NULL, 'H') == -1);
#ifdef _WIN32
    CHECK(fontsys_file_face_index(ctx, "C:/Windows/Fonts/arial.ttf", "ArialMT", 'H') == 0);
    /* collections: the name wins over coverage (every face maps 'H') */
    CHECK(fontsys_file_face_index(ctx, "C:/Windows/Fonts/msgothic.ttc", "MS-Gothic", 'H') == 0);
    CHECK(fontsys_file_face_index(ctx, "C:/Windows/Fonts/msgothic.ttc", "MS-UIGothic", 'H') == 1);
    CHECK(fontsys_file_face_index(ctx, "C:/Windows/Fonts/msgothic.ttc", "MS-PGothic", 'H') == 2);
    CHECK(fontsys_file_face_index(ctx, "C:/Windows/Fonts/cambria.ttc", "CambriaMath", NO_CP) == 1);
    /* variable font: a named instance is face (instance << 16) | index */
    {
        const char *bahn = "C:/Windows/Fonts/bahnschrift.ttf";
        int index = fontsys_file_face_index(ctx, bahn, "Bahnschrift-Bold", 'H');
        CHECK(index == 0x50000);
        CHECK(fontsys_face_has(ctx, fontsys_face_open(ctx, bahn, index), 'H') == 1);
        CHECK(fontsys_file_face_index(ctx, bahn, "Bahnschrift", 'H') == 0);
    }
#endif
    /* the faces are opened only for the lookup: none enter the face cache
       (only the one opened above, on Windows), and the OS is never asked */
#ifdef _WIN32
    CHECK(fontsys_face_open(ctx, path, 0) == 1);
#else
    CHECK(fontsys_face_open(ctx, path, 0) == 0);
#endif
    CHECK(fontsys_debug_os_calls(ctx) == 0);
    fontsys_destroy(ctx);
}

/* fontsys_file_has (private, used by the Android fonts.xml lookup): a
   coverage check that leaves the face cache alone. */
static void test_file_has(const char *path) {
    fontsys_ctx *ctx = fontsys_create();

    CHECK(ctx != NULL);
    if (!ctx) return;
    CHECK(fontsys_file_has(ctx, path, 0, 'H') == 1);
    CHECK(fontsys_file_has(ctx, path, 0, NO_CP) == 0);
    CHECK(fontsys_file_has(ctx, path, 999, 'H') == 0);   /* no such face */
    CHECK(fontsys_file_has(ctx, "fontsys-no-such-dir/no-such-font.ttf", 0, 'H') == 0);
    /* nothing entered the cache: the first face opened gets id 0 */
    CHECK(fontsys_face_open(ctx, path, 0) == 0);
    /* a cached face answers too */
    CHECK(fontsys_file_has(ctx, path, 0, 'H') == 1);
    CHECK(fontsys_face_open(ctx, path, 0) == 0);
    CHECK(fontsys_debug_os_calls(ctx) == 0);
    fontsys_destroy(ctx);
}

static int file_exists(const char *path) {
#ifdef _WIN32
    wchar_t wpath[4 * MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath, 4 * MAX_PATH)) return 0;
    return GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES;
#else
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
#endif
}

/* fontsys_find, then check the result opens and covers cp. Returns the face id or -1. */
static int find_face(fontsys_ctx *ctx, unsigned cp, const char *lang, int weight, int italic,
                     int mono, char *path, int *index) {
    fontsys_style style;
    int r, face;

    style.weight = weight;
    style.italic = italic;
    style.monospace = mono;
    path[0] = 0;
    *index = -1;
    r = fontsys_find(ctx, cp, lang, &style, path, PATH_CAP, index);
    printf("find(U+%04X, %s, w%d i%d m%d) = %d: %s #%d\n", cp, lang, weight, italic, mono, r,
           path, *index);
    CHECK(r == 1);
    if (r != 1) return -1;
    CHECK(file_exists(path));
    CHECK(*index >= 0);
    face = fontsys_face_open(ctx, path, *index);
    CHECK(face >= 0);
    if (face < 0) return -1;
    CHECK(fontsys_face_has(ctx, face, cp) == 1);
    return face;
}

/* Apple: CoreText answers bold/italic requests with the variable SF font,
   whose default instance FreeType reports as regular (see the ledger Ruling
   on variable system fonts). There the style checks are reported, not
   counted. Everywhere else they are strict. */
static void check_style(int face, int ok, const char *what, const char *path, int index) {
    CHECK(face >= 0);
    if (face < 0) return;
#ifdef __APPLE__
    if (!ok) {
        printf("SKIP (variable font, see Ruling): %s: %s #%d\n", what, path, index);
        return;
    }
#else
    (void)what; (void)path; (void)index;
#endif
    CHECK(ok);
}

static void test_find(void) {
    fontsys_ctx *ctx = fontsys_create();
    char hans[PATH_CAP], ja[PATH_CAP], path[PATH_CAP];
    int hans_index = -1, ja_index = -1, index = -1, face, calls, have_cjk = 1;
    fontsys_style regular = {400, 0, 0};

    CHECK(ctx != NULL);
    if (!ctx) return;

#if defined(__linux__) && !defined(__ANDROID__)
    /* A bare Linux install (e.g. a CI runner without fonts-noto-cjk) may have
       no CJK font at all. Windows, macOS and Android always do, so the Han
       checks stay strict there. */
    {
        /* Only "none found" (0) skips; a lookup error (< 0) still fails. */
        int cjk_rc = fontsys_find(ctx, CP_ZHONG, "zh-Hans", &regular, path, PATH_CAP, &index);
        CHECK(cjk_rc >= 0);
        have_cjk = cjk_rc == 1;
    }
    if (!have_cjk) printf("SKIP: no CJK font installed\n");
#endif

    /* Han: the language picks the regional font */
    if (have_cjk) {
        find_face(ctx, CP_ZHONG, "zh-Hans", 400, 0, 0, hans, &hans_index);
        find_face(ctx, CP_ZHONG, "ja", 400, 0, 0, ja, &ja_index);
        CHECK(strcmp(hans, ja) != 0 || hans_index != ja_index);
    }

    /* Latin, and the style axes */
    find_face(ctx, 'A', "en", 400, 0, 0, path, &index);
    face = find_face(ctx, 'A', "en", 700, 0, 0, path, &index);
    check_style(face, face >= 0 && fontsys_face_is_bold(ctx, face) == 1,
                "find(A, en, w700) face is not bold", path, index);
    face = find_face(ctx, 'A', "en", 400, 1, 0, path, &index);
    check_style(face, face >= 0 && fontsys_face_is_italic(ctx, face) == 1,
                "find(A, en, italic) face is not italic", path, index);
    face = find_face(ctx, 'A', "en", 400, 0, 1, path, &index);
    CHECK(face >= 0 && fontsys_face_is_mono(ctx, face) == 1);

    /* nothing covers a private-use code point */
    CHECK(fontsys_find(ctx, NO_CP, "en", &regular, path, PATH_CAP, &index) == 0);

    /* surrogates and out-of-range code points are rejected before the OS is asked */
    calls = fontsys_debug_os_calls(ctx);
    CHECK(calls > 0);
    CHECK(fontsys_find(ctx, 0xD83D, "en", &regular, path, PATH_CAP, &index) == 0);
    CHECK(fontsys_find(ctx, 0xDFFF, "en", &regular, path, PATH_CAP, &index) == 0);
    CHECK(fontsys_find(ctx, 0x110000, "en", &regular, path, PATH_CAP, &index) == 0);
    CHECK(fontsys_debug_os_calls(ctx) == calls);

    /* a path buffer too small: an error, and nothing written past path_cap
       (needs a font that covers the code point) */
    if (have_cjk) {
        char short_buf[16];
        int i, untouched = 1;
        memset(short_buf, 0x5A, sizeof(short_buf));
        CHECK(fontsys_find(ctx, CP_ZHONG, "zh-Hans", &regular, short_buf, 4, &index) < 0);
        for (i = 4; i < (int)sizeof(short_buf); i++)
            if (short_buf[i] != 0x5A) untouched = 0;
        CHECK(untouched);
    }

    fontsys_destroy(ctx);
}

int main(void) {
    const char *path = test_font();
    fontsys_ctx *ctx = fontsys_create();
    int face = -1;

    CHECK(ctx != NULL);
    if (!ctx) return 1;
    CHECK(fontsys_debug_os_calls(ctx) == 0);

    test_open(ctx, path, &face);
    if (face >= 0) {
        test_coverage_and_style(ctx, face);
#ifdef _WIN32
        test_open_non_ascii(path);
#endif
        test_render(ctx, face);
        test_file_face_index(path);
        test_file_has(path);
    } else {
        fprintf(stderr, "cannot open test font '%s' (set FONTSYS_TEST_FONT)\n", path);
    }
    fontsys_destroy(ctx);

#ifdef _WIN32
    test_render_overlaps();
#endif
    test_render_emoji();
    test_harfbuzz_linked();
    test_color_emoji();
    test_find();

    if (failures) {
        fprintf(stderr, "fontsys_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("fontsys_test: all passed\n");
    return 0;
}
