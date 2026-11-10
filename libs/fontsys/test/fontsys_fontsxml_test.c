/* fonts.xml parser and font choice (Android API 24-28 lookup), run on the
   host against test/fixtures/fonts.xml. CTest: fontsys_fontsxml.
   Usage: fontsys_fontsxml_test <path to fixtures/fonts.xml>. Exit 0 = all passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontsys_fontsxml.h"

static int failures = 0;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define DIR "/system/fonts/"

/* Font positions in the fixture, in file order. */
enum {
    ROBOTO_THIN, ROBOTO_THIN_ITALIC, ROBOTO_REGULAR, ROBOTO_ITALIC, ROBOTO_BOLD,
    ROBOTO_BOLD_ITALIC, ROBOTO_BLACK, CONDENSED, NOTO_SERIF, DROID_MONO, FLEX, FLEX_ITALIC, FLEX_BOLD_ITALIC,
    ETHIOPIC, ETHIOPIC_BOLD, SYMBOLS, CJK_HANS, CJK_HANS_SERIF, CJK_HANT, CJK_HANT_SERIF,
    CJK_JA, CJK_JA_SERIF, CJK_KO_SERIF, CJK_KO, EMOJI, EXTRA, FONT_COUNT
};

static void check_font(const fontsys_xml_table *t, int i, const char *path, int weight,
                       int italic, int index, int serif_fallback, int family, int line) {
    const fontsys_xml_font *f;
    if (i >= t->font_count) {
        fprintf(stderr, "line %d: no font %d\n", line, i);
        failures++;
        return;
    }
    f = &t->fonts[i];
    if (strcmp(f->path, path) != 0 || f->weight != weight || f->italic != italic ||
        f->index != index || f->serif_fallback != serif_fallback || f->family != family) {
        fprintf(stderr, "line %d: font %d = {%s w%d i%d #%d serif%d fam%d}, expected {%s w%d i%d #%d serif%d fam%d}\n",
                line, i, f->path, f->weight, f->italic, f->index, f->serif_fallback, f->family,
                path, weight, italic, index, serif_fallback, family);
        failures++;
    }
}
#define FONT(i, file, w, it, idx, serif, fam) check_font(&t, i, file, w, it, idx, serif, fam, __LINE__)

static void check_family(const fontsys_xml_table *t, int i, const char *name, const char *lang,
                         int first, int count, int line) {
    const fontsys_xml_family *f;
    if (i >= t->family_count) {
        fprintf(stderr, "line %d: no family %d\n", line, i);
        failures++;
        return;
    }
    f = &t->families[i];
    if (strcmp(f->name, name) != 0 || strcmp(f->lang, lang) != 0 ||
        (count > 0 && f->first_font != first) || f->font_count != count) {
        fprintf(stderr, "line %d: family %d = {'%s' '%s' %d+%d}, expected {'%s' '%s' %d+%d}\n",
                line, i, f->name, f->lang, f->first_font, f->font_count, name, lang, first, count);
        failures++;
    }
}
#define FAMILY(i, name, lang, first, count) check_family(&t, i, name, lang, first, count, __LINE__)

static void test_parse(const fontsys_xml_table *tp) {
    fontsys_xml_table t = *tp;
    int i;

    CHECK(t.family_count == 14);
    CHECK(t.font_count == FONT_COUNT);

    FAMILY(0, "sans-serif", "", ROBOTO_THIN, 7);
    FAMILY(1, "sans-serif-condensed", "", CONDENSED, 1);
    FAMILY(2, "serif", "", NOTO_SERIF, 1);
    FAMILY(3, "monospace", "", DROID_MONO, 1);
    FAMILY(4, "sans-serif-flex", "", FLEX, 3);
    FAMILY(5, "empty-family", "", 0, 0);
    FAMILY(6, "", "und-Ethi", ETHIOPIC, 2);
    FAMILY(7, "", "", SYMBOLS, 1);
    FAMILY(8, "", "zh-Hans", CJK_HANS, 2);
    FAMILY(9, "", "zh-Hant,zh-Bopo", CJK_HANT, 2);
    FAMILY(10, "", "ja", CJK_JA, 2);
    FAMILY(11, "", "ko", CJK_KO_SERIF, 2);
    FAMILY(12, "", "und-Zsye", EMOJI, 1);
    FAMILY(13, "", "und-Zsym", EXTRA, 1);

    FONT(ROBOTO_THIN, DIR "Roboto-Thin.ttf", 100, 0, 0, 0, 0);
    FONT(ROBOTO_THIN_ITALIC, DIR "Roboto-ThinItalic.ttf", 100, 1, 0, 0, 0);
    FONT(ROBOTO_REGULAR, DIR "Roboto-Regular.ttf", 400, 0, 0, 0, 0);
    FONT(ROBOTO_ITALIC, DIR "Roboto-Italic.ttf", 400, 1, 0, 0, 0);       /* attributes reversed */
    FONT(ROBOTO_BOLD, DIR "Roboto-Bold.ttf", 700, 0, 0, 0, 0);           /* spaces, multi-line text */
    FONT(ROBOTO_BOLD_ITALIC, DIR "Roboto-BoldItalic.ttf", 700, 1, 0, 0, 0);
    FONT(ROBOTO_BLACK, DIR "Roboto-Black.ttf", 900, 0, 0, 0, 0);
    FONT(CONDENSED, DIR "RobotoCondensed-Regular.ttf", 400, 0, 0, 0, 1);
    FONT(NOTO_SERIF, DIR "NotoSerif-Regular.ttf", 400, 0, 0, 0, 2);
    FONT(DROID_MONO, DIR "DroidSansMono.ttf", 400, 0, 0, 0, 3);
    FONT(FLEX, DIR "RobotoFlex-Regular.ttf", 300, 0, 0, 0, 4);           /* single quotes, <axis> child */
    FONT(FLEX_ITALIC, DIR "RobotoFlex-Italic.ttf", 300, 1, 0, 0, 4);
    FONT(FLEX_BOLD_ITALIC, DIR "RobotoFlex-Italic.ttf", 700, 1, 0, 0, 4);
    FONT(ETHIOPIC, DIR "NotoSansEthiopic-Regular.ttf", 400, 0, 0, 0, 6);
    FONT(ETHIOPIC_BOLD, DIR "NotoSansEthiopic-Bold.ttf", 700, 0, 0, 0, 6);
    FONT(SYMBOLS, DIR "NotoSansSymbols-Regular-Subsetted.ttf", 400, 0, 0, 0, 7);
    FONT(CJK_HANS, DIR "NotoSansCJK-Regular.ttc", 400, 0, 2, 0, 8);
    FONT(CJK_HANS_SERIF, DIR "NotoSerifCJK-Regular.ttc", 400, 0, 2, 1, 8); /* attributes over 2 lines */
    FONT(CJK_HANT, DIR "NotoSansCJK-Regular.ttc", 400, 0, 3, 0, 9);
    FONT(CJK_HANT_SERIF, DIR "NotoSerifCJK-Regular.ttc", 400, 0, 3, 1, 9);
    FONT(CJK_JA, DIR "NotoSansCJK-Regular.ttc", 400, 0, 0, 0, 10);
    FONT(CJK_JA_SERIF, DIR "NotoSerifCJK-Regular.ttc", 400, 0, 0, 1, 10);
    FONT(CJK_KO_SERIF, DIR "NotoSerifCJK-Regular.ttc", 400, 0, 1, 1, 11); /* serif form first */
    FONT(CJK_KO, DIR "NotoSansCJK-Regular.ttc", 400, 0, 1, 0, 11);       /* index first */
    FONT(EMOJI, DIR "NotoColorEmoji.ttf", 400, 0, 0, 0, 12);
    FONT(EXTRA, "/product/fonts/Extra.ttf", 400, 0, 0, 0, 13);           /* absolute path kept */

    /* the commented-out family and the alias targets are not fonts */
    for (i = 0; i < t.font_count; i++) {
        CHECK(strstr(t.fonts[i].path, "Ghost") == NULL);
        CHECK(strstr(t.fonts[i].path, "sans-serif") == NULL);
    }
}

/* ---- font choice, with a fake coverage table ---- */

#define CP_LATIN 'A'
#define CP_AMACRON 0x0100 /* only in the upright Roboto files */
#define CP_CYRL 0x0400    /* Roboto and the CJK fonts */
#define CP_ETHI 0x1200
#define CP_SUN 0x2600     /* NotoSansSymbols and the CJK fonts */
#define CP_WAVY 0x3030    /* NotoColorEmoji and the CJK fonts */
#define CP_ZHONG 0x4E2D
#define CP_PUA 0xE000     /* nothing */
#define CP_FLEX 0xE001    /* only RobotoFlex-Regular.ttf */
#define CP_EMOJI 0x1F600

typedef struct {
    int calls;
} fake;

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int fake_covers(void *user, const fontsys_xml_font *font, unsigned cp) {
    const char *b = base_name(font->path);
    int cjk = starts(b, "NotoSansCJK") || starts(b, "NotoSerifCJK");

    ((fake *)user)->calls++;
    switch (cp) {
    case CP_LATIN:
        return starts(b, "Roboto") || starts(b, "NotoSerif-") || starts(b, "DroidSansMono") || cjk;
    case CP_AMACRON:
        return !strcmp(b, "Roboto-Regular.ttf") || !strcmp(b, "Roboto-Bold.ttf") ||
               !strcmp(b, "Roboto-Black.ttf");
    case CP_CYRL:
        return starts(b, "Roboto") || cjk;
    case CP_WAVY:
        return starts(b, "NotoColorEmoji") || cjk;
    case CP_ETHI:
        return starts(b, "NotoSansEthiopic");
    case CP_SUN:
        return starts(b, "NotoSansSymbols") || cjk;
    case CP_ZHONG:
        return cjk;
    case CP_FLEX:
        return !strcmp(b, "RobotoFlex-Regular.ttf");
    case CP_EMOJI:
        return starts(b, "NotoColorEmoji");
    default:
        return 0;
    }
}

static int pick(const fontsys_xml_table *t, unsigned cp, const char *lang, int weight, int italic,
                int mono, int *calls) {
    fontsys_style style;
    fake f = {0};
    int r;

    style.weight = weight;
    style.italic = italic;
    style.monospace = mono;
    r = fontsys_xml_select(t, cp, lang, &style, fake_covers, &f);
    if (calls) *calls = f.calls;
    return r;
}

static void check_pick(const fontsys_xml_table *t, unsigned cp, const char *lang, int weight,
                       int italic, int mono, int expected, int line) {
    int r = pick(t, cp, lang, weight, italic, mono, NULL);
    if (r != expected) {
        fprintf(stderr, "line %d: select(U+%04X, '%s', w%d i%d m%d) = %d (%s #%d), expected %d (%s #%d)\n",
                line, cp, lang, weight, italic, mono, r,
                r >= 0 && r < t->font_count ? t->fonts[r].path : "-",
                r >= 0 && r < t->font_count ? t->fonts[r].index : -1, expected,
                expected >= 0 ? t->fonts[expected].path : "-",
                expected >= 0 ? t->fonts[expected].index : -1);
        failures++;
    }
}
#define PICK(cp, lang, w, it, mono, expected) check_pick(t, cp, lang, w, it, mono, expected, __LINE__)

static void test_select(const fontsys_xml_table *t) {
    int calls;

    /* Latin: the generic family first, whatever the language */
    PICK(CP_LATIN, "en", 400, 0, 0, ROBOTO_REGULAR);
    PICK(CP_LATIN, "ja", 400, 0, 0, ROBOTO_REGULAR);
    PICK(CP_LATIN, "", 400, 0, 0, ROBOTO_REGULAR);
    PICK(CP_LATIN, "en", 0, 0, 0, ROBOTO_REGULAR);      /* weight unset = 400 */

    /* italic, and the closest weight */
    PICK(CP_LATIN, "en", 400, 1, 0, ROBOTO_ITALIC);
    PICK(CP_LATIN, "en", 700, 0, 0, ROBOTO_BOLD);
    PICK(CP_LATIN, "en", 600, 0, 0, ROBOTO_BOLD);
    PICK(CP_LATIN, "en", 300, 0, 0, ROBOTO_REGULAR);
    PICK(CP_LATIN, "en", 200, 0, 0, ROBOTO_THIN);
    PICK(CP_LATIN, "en", 850, 0, 0, ROBOTO_BLACK);
    PICK(CP_LATIN, "en", 800, 0, 0, ROBOTO_BOLD);       /* tie: file order */
    PICK(CP_LATIN, "en", 100, 1, 0, ROBOTO_THIN_ITALIC);
    PICK(CP_LATIN, "en", 700, 1, 0, ROBOTO_BOLD_ITALIC);
    PICK(CP_LATIN, "en", 900, 1, 0, ROBOTO_BOLD_ITALIC); /* right slant beats right weight */

    /* the italic files lack it: the best upright one that has it */
    PICK(CP_AMACRON, "en", 400, 1, 0, ROBOTO_REGULAR);
    PICK(CP_AMACRON, "en", 700, 1, 0, ROBOTO_BOLD);

    /* monospace */
    PICK(CP_LATIN, "en", 400, 0, 1, DROID_MONO);
    PICK(CP_LATIN, "en", 700, 1, 1, DROID_MONO);
    /* the monospace family lacks it: sans-serif before the fallbacks */
    PICK(CP_CYRL, "ja", 400, 0, 1, ROBOTO_REGULAR);
    PICK(CP_CYRL, "ja", 400, 0, 0, ROBOTO_REGULAR);

    /* Han: the family of the language, never its serif form */
    PICK(CP_ZHONG, "zh-Hans", 400, 0, 0, CJK_HANS);
    PICK(CP_ZHONG, "zh-Hant", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "ja", 400, 0, 0, CJK_JA);
    PICK(CP_ZHONG, "ko", 400, 0, 0, CJK_KO);       /* listed after its serif form */
    PICK(CP_ZHONG, "ja-JP", 400, 0, 0, CJK_JA);
    PICK(CP_ZHONG, "zh", 400, 0, 0, CJK_HANS);
    PICK(CP_ZHONG, "zh-CN", 400, 0, 0, CJK_HANS);
    PICK(CP_ZHONG, "zh-TW", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "zh-HK", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "zh_TW", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "zh-Hant-HK", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "ZH-HANT", 400, 0, 0, CJK_HANT);
    PICK(CP_ZHONG, "zh-Bopo", 400, 0, 0, CJK_HANT);     /* second tag of the list */
    PICK(CP_ZHONG, "ja", 700, 1, 1, CJK_JA);            /* no such style: still the sans face */
    /* another language: the first family that has it, in file order */
    PICK(CP_ZHONG, "en", 400, 0, 0, CJK_HANS);
    PICK(CP_ZHONG, "", 400, 0, 0, CJK_HANS);
    PICK(CP_ZHONG, "fr-CA", 400, 0, 0, CJK_HANS);

    /* a lang-less fallback beats another language's; the language's beats both */
    PICK(CP_SUN, "en", 400, 0, 0, SYMBOLS);
    PICK(CP_SUN, "", 400, 0, 0, SYMBOLS);
    PICK(CP_SUN, "ja", 400, 0, 0, CJK_JA);
    PICK(CP_SUN, "zh-Hans", 400, 0, 0, CJK_HANS);

    /* script fallbacks (und-*) */
    PICK(CP_ETHI, "am", 400, 0, 0, ETHIOPIC);
    PICK(CP_ETHI, "en", 700, 0, 0, ETHIOPIC_BOLD);
    PICK(CP_EMOJI, "en", 400, 0, 0, EMOJI);
    /* und-* counts as lang-less: before another language's family, even later in the file */
    PICK(CP_WAVY, "en", 400, 0, 0, EMOJI);
    PICK(CP_WAVY, "ja", 400, 0, 0, CJK_JA);

    /* nothing has it */
    PICK(CP_PUA, "en", 400, 0, 0, -1);
    PICK(CP_PUA, "ja", 700, 1, 1, -1);

    /* coverage is checked once per family (its upright regular font), then
       per candidate only when it is a different face */
    CHECK(pick(t, CP_ZHONG, "zh-Hans", 400, 0, 0, &calls) == CJK_HANS);
    CHECK(calls == 2); /* Roboto-Regular, NotoSansCJK #2 */
    CHECK(pick(t, CP_LATIN, "en", 400, 0, 0, &calls) == ROBOTO_REGULAR);
    CHECK(calls == 1);
    CHECK(pick(t, CP_LATIN, "en", 400, 1, 0, &calls) == ROBOTO_ITALIC);
    CHECK(calls == 2);
    /* nothing has it: one check per family with fonts (13) */
    CHECK(pick(t, CP_PUA, "en", 400, 0, 0, &calls) == -1);
    CHECK(calls == 13);
    /* only another named family has it (last resort). Its italic file is
       listed twice and tried once: 12 family checks before it (sans-serif,
       4 neutral, 4 other-lang, 3 named), then RobotoFlex-Regular (its
       family check), RobotoFlex-Italic (700 italic; the 300 italic is the
       same face, skipped) */
    CHECK(pick(t, CP_FLEX, "en", 700, 1, 0, &calls) == FLEX);
    CHECK(calls == 14);
}

/* ---- malformed and edge-case input ---- */

static int parse_str(const char *xml, fontsys_xml_table *t) {
    return fontsys_xml_parse(xml, strlen(xml), DIR, t);
}

static void test_edge_cases(void) {
    fontsys_xml_table t;
    fontsys_style regular = {400, 0, 0};
    fake f = {0};

    CHECK(parse_str("", &t) == 0);
    CHECK(t.family_count == 0 && t.font_count == 0);
    CHECK(fontsys_xml_select(&t, CP_LATIN, "en", &regular, fake_covers, &f) == -1);
    CHECK(f.calls == 0);
    fontsys_xml_free(&t);

    CHECK(parse_str("<!-- <family><font>A.ttf</font></family>", &t) == 0); /* unterminated comment */
    CHECK(t.family_count == 0 && t.font_count == 0);
    fontsys_xml_free(&t);

    /* truncated inside the text: no '<' after it, so no font */
    CHECK(parse_str("<familyset><family lang=\"ja\"><font weight=\"400\">A.ttf", &t) == 0);
    CHECK(t.family_count == 1 && t.font_count == 0);
    fontsys_xml_free(&t);

    /* truncated inside an attribute value */
    CHECK(parse_str("<familyset><family lang=\"ja", &t) == 0);
    CHECK(t.font_count == 0);
    fontsys_xml_free(&t);

    /* a font outside any family is ignored; a self-closing family is empty;
       a comment before the file name inside <font> is skipped; bad numbers
       fall back to the defaults */
    CHECK(parse_str("<font>Stray.ttf</font><family name=\"x\"/>"
                    "<family><font weight=\"bold\" index=\"-3\" style=\"oblique\"><!-- c -->"
                    " B.ttf </font></family>", &t) == 0);
    CHECK(t.family_count == 2);
    CHECK(t.font_count == 1);
    if (t.font_count == 1) {
        CHECK(strcmp(t.fonts[0].path, DIR "B.ttf") == 0);
        CHECK(t.fonts[0].weight == 400);
        CHECK(t.fonts[0].index == 0);
        CHECK(t.fonts[0].italic == 0);
        CHECK(t.fonts[0].family == 1);
    }
    if (t.family_count == 2) CHECK(t.families[0].font_count == 0);
    fontsys_xml_free(&t);

    /* no font_dir: the file name as written */
    CHECK(fontsys_xml_parse("<family><font>C.ttf</font></family>", 35, NULL, &t) == 0);
    CHECK(t.font_count == 1 && strcmp(t.fonts[0].path, "C.ttf") == 0);
    fontsys_xml_free(&t);

    /* length, not NUL, ends the input */
    CHECK(fontsys_xml_parse("<family><font>D.ttf</font></family><family><font>E.ttf</font></family>",
                            35, DIR, &t) == 0);
    CHECK(t.family_count == 1 && t.font_count == 1);
    fontsys_xml_free(&t);

    CHECK(fontsys_xml_load("fontsys-no-such-dir/fonts.xml", DIR, &t) == -1);
    CHECK(t.family_count == 0 && t.font_count == 0);
    fontsys_xml_free(&t); /* safe on an empty table */
}

int main(int argc, char **argv) {
    fontsys_xml_table t;

    if (argc < 2) {
        fprintf(stderr, "usage: fontsys_fontsxml_test <fixtures/fonts.xml>\n");
        return 2;
    }
    CHECK(fontsys_xml_load(argv[1], DIR, &t) == 0);
    test_parse(&t);
    if (t.font_count == FONT_COUNT) test_select(&t);
    fontsys_xml_free(&t);

    test_edge_cases();

    if (failures) {
        fprintf(stderr, "fontsys_fontsxml_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("fontsys_fontsxml_test: all passed\n");
    return 0;
}
