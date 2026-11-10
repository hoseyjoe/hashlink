/*
 * Android /system/etc/fonts.xml: parser and font choice for the API 24-28
 * lookup in fontsys_find_android.c. Plain C with no FreeType or OS calls, so
 * it also builds and runs on the host (test/fontsys_fontsxml_test.c).
 * Private: not part of the public API, not installed.
 */
#ifndef FONTSYS_FONTSXML_H
#define FONTSYS_FONTSXML_H

#include <stddef.h>

#include "fontsys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;     /* name attribute ("sans-serif"), "" when none */
    char *lang;     /* lang attribute as written ("zh-Hant,zh-Bopo"), "" when none */
    int first_font; /* its fonts are fonts[first_font .. first_font + font_count) */
    int font_count;
} fontsys_xml_family;

typedef struct {
    char *path;         /* font_dir + file name; a file name starting with '/' is kept as is */
    int weight;         /* weight attribute, default 400 */
    int italic;         /* style="italic" */
    int index;          /* index attribute (face in a .ttc), default 0 */
    int serif_fallback; /* has a fallbackFor attribute (the serif form of a CJK family) */
    int family;         /* index in families */
} fontsys_xml_font;

typedef struct {
    fontsys_xml_family *families;
    int family_count;
    fontsys_xml_font *fonts;
    int font_count;
} fontsys_xml_table;

/* Parses len bytes of fonts.xml into *out (zeroed first). Every <font> inside
   a <family> becomes an entry; <alias>, <axis>, comments and unknown elements
   are skipped. Returns 0, or -1 when out of memory (*out is then empty). */
int fontsys_xml_parse(const char *xml, size_t len, const char *font_dir, fontsys_xml_table *out);

/* Reads the file at path and parses it. 0, or -1 when it cannot be read. */
int fontsys_xml_load(const char *path, const char *font_dir, fontsys_xml_table *out);

void fontsys_xml_free(fontsys_xml_table *t);

/* 1 when the face font->index of font->path maps cp, else 0. */
typedef int (*fontsys_xml_covers_fn)(void *user, const fontsys_xml_font *font, unsigned cp);

/* The font to draw cp with, as an index in t->fonts, or -1 when none covers it.
   Families are tried in this order, file order within a rank:
     1. the generic family: "monospace" when style->monospace, else "sans-serif";
        then "sans-serif" for monospace;
     2. families whose lang matches lang exactly (zh-TW counts as zh-Hant);
     3. families whose lang has lang's primary subtag and no conflicting script;
     4. families without a lang, or with an "und-" lang (script fallbacks);
     5. families with another lang;
     6. other named families (serif, sans-serif-condensed, ...).
   In the first family where some font covers cp, the font with the requested
   slant and the closest weight wins (serif forms of CJK families last). */
int fontsys_xml_select(const fontsys_xml_table *t, unsigned cp, const char *lang,
                       const fontsys_style *style, fontsys_xml_covers_fn covers, void *user);

#ifdef __cplusplus
}
#endif

#endif
