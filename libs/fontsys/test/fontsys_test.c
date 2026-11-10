/* fontsys core tests (CTest: fontsys). Exit code 0 = all passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#   include <windows.h>
#   include <wchar.h>
#endif
#include "fontsys.h"

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

static void test_find(void) {
    fontsys_ctx *ctx = fontsys_create();
    char hans[PATH_CAP], ja[PATH_CAP], path[PATH_CAP];
    int hans_index = -1, ja_index = -1, index = -1, face, calls;
    fontsys_style regular = {400, 0, 0};

    CHECK(ctx != NULL);
    if (!ctx) return;

    /* Han: the language picks the regional font */
    find_face(ctx, CP_ZHONG, "zh-Hans", 400, 0, 0, hans, &hans_index);
    find_face(ctx, CP_ZHONG, "ja", 400, 0, 0, ja, &ja_index);
    CHECK(strcmp(hans, ja) != 0 || hans_index != ja_index);

    /* Latin, and the style axes */
    find_face(ctx, 'A', "en", 400, 0, 0, path, &index);
    face = find_face(ctx, 'A', "en", 700, 0, 0, path, &index);
    CHECK(face >= 0 && fontsys_face_is_bold(ctx, face) == 1);
    face = find_face(ctx, 'A', "en", 400, 1, 0, path, &index);
    CHECK(face >= 0 && fontsys_face_is_italic(ctx, face) == 1);
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

    /* a path buffer too small: an error, and nothing written past path_cap */
    {
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
    } else {
        fprintf(stderr, "cannot open test font '%s' (set FONTSYS_TEST_FONT)\n", path);
    }
    fontsys_destroy(ctx);

    test_find();

    if (failures) {
        fprintf(stderr, "fontsys_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("fontsys_test: all passed\n");
    return 0;
}
