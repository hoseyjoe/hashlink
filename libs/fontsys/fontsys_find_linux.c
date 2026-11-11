/*
 * fontsys_os_find on Linux (and other non-Apple, non-Android Unix):
 * fontconfig system font fallback. The query pattern carries the code point
 * (FC_CHARSET), language, weight, slant and spacing; FcFontSort ranks the
 * installed fonts against it and the first one whose charset really contains
 * the code point wins. The file comes from FC_FILE, the face index (with a
 * variable font's named instance in the high 16 bits, as FreeType numbers
 * them) from FC_INDEX.
 *
 * Compile-only in CI; also checked by hand against the system fontconfig.
 */
#include <fontconfig/fontconfig.h>
#include <stdlib.h>
#include <string.h>

#include "fontsys_internal.h"

/* Per-context state: the fontconfig configuration with the system fonts
   loaded, kept for the life of the context. */
typedef struct {
    FcConfig *config;
} linux_state;

static linux_state *get_state(fontsys_ctx *ctx) {
    void **slot = fontsys_os_state(ctx);
    linux_state *st = (linux_state *)*slot;

    if (st) return st;
    st = (linux_state *)calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->config = FcInitLoadConfigAndFonts();
    if (!st->config) {
        free(st);
        return NULL;
    }
    *slot = st;
    return st;
}

/* BCP 47 tag to a language fontconfig knows. fontconfig names Han by region
   (zh-cn, zh-tw), not by script; everything else it normalizes itself. */
static const char *fc_lang(const char *lang) {
    if (!strncmp(lang, "zh-Hans", 7) && (!lang[7] || lang[7] == '-')) return "zh-cn";
    if (!strncmp(lang, "zh-Hant", 7) && (!lang[7] || lang[7] == '-')) return "zh-tw";
    return lang;
}

int fontsys_os_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                    char *path_out, int path_cap, int *index_out) {
    linux_state *st = get_state(ctx);
    FcPattern *pat;
    FcCharSet *cs;
    FcFontSet *set = NULL;
    FcResult res;
    int weight = style->weight < 1 ? 400 : style->weight;
    int pick = -1; /* index in set of the font to use */
    int i, r = 0;

    if (!st) return -1;
    pat = FcPatternCreate();
    cs = FcCharSetCreate();
    if (!pat || !cs) {
        if (pat) FcPatternDestroy(pat);
        if (cs) FcCharSetDestroy(cs);
        return -1;
    }
    if (!FcCharSetAddChar(cs, (FcChar32)cp) || !FcPatternAddCharSet(pat, FC_CHARSET, cs)) r = -1;
    FcCharSetDestroy(cs); /* the pattern holds its own reference */
    if (r == 0 && *lang) FcPatternAddString(pat, FC_LANG, (const FcChar8 *)fc_lang(lang));
    if (r == 0) {
        FcPatternAddDouble(pat, FC_WEIGHT, FcWeightFromOpenType(weight));
        FcPatternAddInteger(pat, FC_SLANT, style->italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
        if (style->monospace) FcPatternAddInteger(pat, FC_SPACING, FC_MONO);
        if (!FcConfigSubstitute(st->config, pat, FcMatchPattern)) {
            r = -1;
        } else {
            FcDefaultSubstitute(pat);
            set = FcFontSort(st->config, pat, FcTrue, NULL, &res);
            if (!set) r = (res == FcResultNoMatch) ? 0 : -1;
        }
    }

    /* fontconfig ranks the family (DejaVu Sans for the default "sans") ahead
       of slant, so the top font may lack the requested italic although a
       covering italic exists further down: prefer the best-ranked covering
       font with the right slant, else the best-ranked covering font. */
    for (i = 0; set && i < set->nfont; i++) {
        FcCharSet *font_cs = NULL;
        FcChar8 *file = NULL;
        int slant = FC_SLANT_ROMAN;

        if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &font_cs) != FcResultMatch ||
            !FcCharSetHasChar(font_cs, (FcChar32)cp))
            continue;
        if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch || !file) continue;
        if (pick < 0) pick = i;
        FcPatternGetInteger(set->fonts[i], FC_SLANT, 0, &slant);
        if ((slant != FC_SLANT_ROMAN) == (style->italic != 0)) {
            pick = i;
            break;
        }
    }

    if (pick >= 0) {
        FcChar8 *file = NULL;
        int index = 0;
        size_t size;

        FcPatternGetString(set->fonts[pick], FC_FILE, 0, &file);
        FcPatternGetInteger(set->fonts[pick], FC_INDEX, 0, &index);
        size = strlen((const char *)file) + 1;
        if (size > (size_t)path_cap) {
            path_out[0] = 0;
            r = -2;
        } else {
            memcpy(path_out, file, size);
            *index_out = index;
            r = 1;
        }
    }

    if (set) FcFontSetDestroy(set);
    FcPatternDestroy(pat);
    return r;
}

void fontsys_os_destroy(void *os_state) {
    linux_state *st = (linux_state *)os_state;

    if (!st) return;
    if (st->config) FcConfigDestroy(st->config);
    free(st);
}
