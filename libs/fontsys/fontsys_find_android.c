/*
 * fontsys_os_find on Android.
 *
 * API 29+: the NDK font matcher (AFontMatcher, libandroid.so) picks the font
 * the platform itself would draw the code point with, from the generic
 * family ("sans-serif", or "monospace"), the style and the language. minSdk
 * is 24, so its functions are looked up with dlsym at run time: nothing here
 * links against an API 29 symbol.
 *
 * API 24-28, when a function is missing, or with FONTSYS_FORCE_FONTS_XML=1
 * in the environment: /system/etc/fonts.xml, parsed once per context, and
 * the font choice in fontsys_fontsxml.c.
 *
 * Either way the font must really map the code point (FreeType cmap check).
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <android/log.h>

#include "fontsys_fontsxml.h"
#include "fontsys_internal.h"

#define FONTS_XML "/system/etc/fonts.xml"
#define FONT_DIR "/system/fonts/"
#define LOG_TAG "fontsys"

/* <android/font_matcher.h> and <android/font.h> (API 29), as function
   pointer types over opaque AFontMatcher / AFont pointers. */
typedef void *(*matcher_create_fn)(void);
typedef void (*matcher_destroy_fn)(void *matcher);
typedef void (*matcher_set_style_fn)(void *matcher, uint16_t weight, bool italic);
typedef void (*matcher_set_locales_fn)(void *matcher, const char *language_tags);
typedef void *(*matcher_match_fn)(const void *matcher, const char *family_name,
                                  const uint16_t *text, const uint32_t text_length,
                                  uint32_t *run_length_out);
typedef const char *(*font_get_path_fn)(const void *font);
typedef size_t (*font_get_index_fn)(const void *font);
typedef void (*font_close_fn)(void *font);

typedef struct {
    void *libandroid; /* dlopen handle; NULL when the matcher is not used */
    matcher_create_fn matcher_create;
    matcher_destroy_fn matcher_destroy;
    matcher_set_style_fn matcher_set_style;
    matcher_set_locales_fn matcher_set_locales;
    matcher_match_fn matcher_match;
    font_get_path_fn font_get_path;
    font_get_index_fn font_get_index;
    font_close_fn font_close;

    int xml_loaded;        /* fonts.xml parsed into xml (retried while it fails) */
    fontsys_xml_table xml;
} android_state;

/* dlsym into a function pointer (object-to-function pointer casts are not ISO C). */
static int load_sym(void *lib, const char *name, void *fn_out) {
    void *p = dlsym(lib, name);
    memcpy(fn_out, &p, sizeof(p));
    return p != NULL;
}

static void load_matcher(android_state *st) {
    const char *force = getenv("FONTSYS_FORCE_FONTS_XML");
    void *lib;

    if (force && *force && strcmp(force, "0") != 0) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                            "font lookup: " FONTS_XML " (FONTSYS_FORCE_FONTS_XML)");
        return;
    }
    lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (lib && load_sym(lib, "AFontMatcher_create", &st->matcher_create) &&
        load_sym(lib, "AFontMatcher_destroy", &st->matcher_destroy) &&
        load_sym(lib, "AFontMatcher_setStyle", &st->matcher_set_style) &&
        load_sym(lib, "AFontMatcher_setLocales", &st->matcher_set_locales) &&
        load_sym(lib, "AFontMatcher_match", &st->matcher_match) &&
        load_sym(lib, "AFont_getFontFilePath", &st->font_get_path) &&
        load_sym(lib, "AFont_getCollectionIndex", &st->font_get_index) &&
        load_sym(lib, "AFont_close", &st->font_close)) {
        st->libandroid = lib;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "font lookup: AFontMatcher");
        return;
    }
    if (lib) dlclose(lib);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "font lookup: " FONTS_XML " (no AFontMatcher before API 29)");
}

static android_state *get_state(fontsys_ctx *ctx) {
    void **slot = fontsys_os_state(ctx);
    android_state *st = (android_state *)*slot;

    if (st) return st;
    st = (android_state *)calloc(1, sizeof(*st));
    if (!st) return NULL;
    load_matcher(st);
    *slot = st;
    return st;
}

/* Copies path out under the fontsys_find contract. */
static int put_result(const char *path, int index, char *path_out, int path_cap, int *index_out) {
    size_t size = strlen(path) + 1;
    if (size > (size_t)path_cap) {
        path_out[0] = 0;
        return -2;
    }
    memcpy(path_out, path, size);
    *index_out = index;
    return 1;
}

static int find_with_matcher(android_state *st, fontsys_ctx *ctx, unsigned cp, const char *lang,
                             const fontsys_style *style, int weight, char *path_out, int path_cap,
                             int *index_out) {
    uint16_t text[2];
    uint32_t len = 1;
    void *matcher, *font;
    const char *path;
    int index, face, covered, r;

    if (cp >= 0x10000) {
        text[0] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10));
        text[1] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        len = 2;
    } else {
        text[0] = (uint16_t)cp;
    }
    matcher = st->matcher_create();
    if (!matcher) return -1;
    st->matcher_set_style(matcher, (uint16_t)weight, style->italic != 0);
    if (*lang) st->matcher_set_locales(matcher, lang);
    /* Never NULL per the NDK: with nothing covering the text, the font to draw tofu with. */
    font = st->matcher_match(matcher, style->monospace ? "monospace" : "sans-serif", text, len, NULL);
    if (!font) {
        st->matcher_destroy(matcher);
        return -1;
    }
    path = st->font_get_path(font);
    index = (int)st->font_get_index(font);
    if (!path || !*path || index < 0) {
        r = -1;
    } else {
        /* The caller opens the same face next, so this cache entry is reused. */
        face = fontsys_face_open(ctx, path, index);
        covered = face >= 0 ? fontsys_face_has(ctx, face, cp)
                            : fontsys_file_has(ctx, path, index, cp); /* cache full */
        r = covered ? put_result(path, index, path_out, path_cap, index_out) : 0;
    }
    st->font_close(font);
    st->matcher_destroy(matcher);
    return r;
}

static int xml_covers(void *user, const fontsys_xml_font *font, unsigned cp) {
    /* Not the face cache: a lookup may test many files. */
    return fontsys_file_has((fontsys_ctx *)user, font->path, font->index, cp);
}

static int find_with_fonts_xml(android_state *st, fontsys_ctx *ctx, unsigned cp, const char *lang,
                               const fontsys_style *style, char *path_out, int path_cap,
                               int *index_out) {
    int i;

    if (!st->xml_loaded) {
        if (fontsys_xml_load(FONTS_XML, FONT_DIR, &st->xml) != 0) return -1;
        st->xml_loaded = 1;
    }
    i = fontsys_xml_select(&st->xml, cp, lang, style, xml_covers, ctx);
    if (i < 0) return 0;
    return put_result(st->xml.fonts[i].path, st->xml.fonts[i].index, path_out, path_cap, index_out);
}

int fontsys_os_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                    char *path_out, int path_cap, int *index_out) {
    android_state *st = get_state(ctx);
    int weight = style->weight < 1 ? 400 : style->weight > 1000 ? 1000 : style->weight;

    if (!st) return -1;
    if (st->libandroid)
        return find_with_matcher(st, ctx, cp, lang, style, weight, path_out, path_cap, index_out);
    return find_with_fonts_xml(st, ctx, cp, lang, style, path_out, path_cap, index_out);
}

void fontsys_os_destroy(void *os_state) {
    android_state *st = (android_state *)os_state;

    if (!st) return;
    fontsys_xml_free(&st->xml);
    if (st->libandroid) dlclose(st->libandroid);
    free(st);
}
