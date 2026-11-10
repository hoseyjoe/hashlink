/*
 * fontsys_os_find on macOS and iOS: CoreText system font fallback.
 * The style goes on a font descriptor applied to the system UI font for the
 * language (its cascade list follows the language), and CTFontCreateForString
 * picks the font that renders the code point. The file comes from
 * kCTFontURLAttribute, the face index within it from matching the font's
 * PostScript name with FreeType (fontsys_file_face_index).
 *
 * Compile-only so far: not yet built or run on macOS or iOS.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "fontsys_internal.h"

#ifndef PATH_MAX
#   define PATH_MAX 1024
#endif

/* PostScript names are at most 63 bytes (OpenType 'name' ID 6). */
#define PS_NAME_CAP 256

#define RELEASE(obj) do { if (obj) { CFRelease(obj); (obj) = NULL; } } while (0)

/* CSS weight (100..900) to kCTFontWeightTrait (-1..1), interpolating
   between the values of Apple's NSFontWeight constants (UltraLight..Black). */
static double ct_weight(int weight) {
    static const double table[9] = {-0.8, -0.6, -0.4, 0.0, 0.23, 0.3, 0.4, 0.56, 0.62};
    int i;

    if (weight < 1) weight = 400;
    if (weight <= 100) return table[0];
    if (weight >= 900) return table[8];
    i = (weight - 100) / 100;
    return table[i] + (table[i + 1] - table[i]) * ((weight - 100) % 100) / 100.0;
}

/* A descriptor with the style's traits (weight, italic, monospace) and, if
   family is not NULL, that family name. NULL on failure. */
static CTFontDescriptorRef style_descriptor(const fontsys_style *style, CFStringRef family) {
    double weight = ct_weight(style->weight);
    int32_t symbolic = (int32_t)((style->italic ? kCTFontItalicTrait : 0) |
                                 (style->monospace ? kCTFontMonoSpaceTrait : 0));
    CFNumberRef weight_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &weight);
    CFNumberRef symbolic_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &symbolic);
    CFDictionaryRef traits = NULL, attrs = NULL;
    CTFontDescriptorRef desc = NULL;

    if (weight_num && symbolic_num) {
        const void *keys[2] = {kCTFontWeightTrait, kCTFontSymbolicTrait};
        const void *values[2] = {weight_num, symbolic_num};
        traits = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
    }
    if (traits) {
        const void *keys[2] = {kCTFontTraitsAttribute, kCTFontFamilyNameAttribute};
        const void *values[2] = {traits, family};
        attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, values, family ? 2 : 1,
                                   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }
    if (attrs) desc = CTFontDescriptorCreateWithAttributes(attrs);
    RELEASE(attrs);
    RELEASE(traits);
    RELEASE(symbolic_num);
    RELEASE(weight_num);
    return desc;
}

/* 1 if font maps the code point in chars (n UTF-16 units) to a real glyph.
   LastResort maps everything to placeholder glyphs, so it never counts. */
static int covers(CTFontRef font, const UniChar *chars, CFIndex n) {
    CGGlyph glyphs[2] = {0, 0};
    CFStringRef name;
    int last_resort;

    if (!CTFontGetGlyphsForCharacters(font, chars, glyphs, n) || glyphs[0] == 0) return 0;
    name = CTFontCopyPostScriptName(font);
    last_resort = name && CFStringCompare(name, CFSTR("LastResort"), 0) == kCFCompareEqualTo;
    RELEASE(name);
    return !last_resort;
}

/* Menlo in the closest style, if it maps the code point (the monospace font
   the system UI font's cascade would not pick for scripts it covers). */
static CTFontRef find_menlo(const fontsys_style *style, const UniChar *chars, CFIndex n) {
    const void *family_key = kCTFontFamilyNameAttribute;
    CTFontDescriptorRef desc = style_descriptor(style, CFSTR("Menlo")), match = NULL;
    CFSetRef mandatory = CFSetCreate(kCFAllocatorDefault, &family_key, 1, &kCFTypeSetCallBacks);
    CTFontRef font = NULL;

    if (desc && mandatory) match = CTFontDescriptorCreateMatchingFontDescriptor(desc, mandatory);
    if (match) font = CTFontCreateWithFontDescriptor(match, 0.0, NULL);
    if (font && !covers(font, chars, n)) RELEASE(font);
    RELEASE(match);
    RELEASE(mandatory);
    RELEASE(desc);
    return font;
}

/* The system fallback font for the code point in the language (NULL: the
   user's), with the style applied to the UI font it cascades from. */
static CTFontRef map_fallback(const fontsys_style *style, CFStringRef lang, const UniChar *chars, CFIndex n) {
    CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, lang);
    CTFontRef styled = NULL, font = NULL;
    CTFontDescriptorRef desc = NULL;
    CFStringRef text = NULL;

    /* A language CoreText does not know: the user's own. */
    if (!base && lang) base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, NULL);
    if (!base) return NULL;
    desc = style_descriptor(style, NULL);
    if (desc) styled = CTFontCreateCopyWithAttributes(base, 0.0, NULL, desc);
    if (!styled) styled = (CTFontRef)CFRetain(base); /* no such style: keep the UI font's */
    text = CFStringCreateWithCharacters(kCFAllocatorDefault, chars, n);
    if (text) font = CTFontCreateForString(styled, text, CFRangeMake(0, n));
    if (font && !covers(font, chars, n)) RELEASE(font);
    RELEASE(text);
    RELEASE(desc);
    RELEASE(styled);
    RELEASE(base);
    return font;
}

/* The UTF-8 file path and face index of a font backed by a local file.
   1 ok, 0 no usable file, -2 path_cap too small. */
static int font_location(fontsys_ctx *ctx, CTFontRef font, unsigned cp, char *path_out, int path_cap,
                         int *index_out) {
    char path[PATH_MAX], ps_name[PS_NAME_CAP];
    CFTypeRef url = CTFontCopyAttribute(font, kCTFontURLAttribute);
    CFStringRef name;
    Boolean ok;
    size_t size;
    int index;

    if (!url) return 0; /* e.g. a font made from data in memory */
    ok = CFGetTypeID(url) == CFURLGetTypeID() &&
         CFURLGetFileSystemRepresentation((CFURLRef)url, true, (UInt8 *)path, (CFIndex)sizeof(path));
    RELEASE(url);
    if (!ok) return 0; /* not a file URL */

    size = strlen(path) + 1;
    if (size > (size_t)path_cap) {
        path_out[0] = 0;
        return -2;
    }

    ps_name[0] = 0;
    name = CTFontCopyPostScriptName(font);
    if (name && !CFStringGetCString(name, ps_name, (CFIndex)sizeof(ps_name), kCFStringEncodingUTF8))
        ps_name[0] = 0;
    RELEASE(name);
    index = fontsys_file_face_index(ctx, path, ps_name[0] ? ps_name : NULL, cp);
    if (index < 0) return 0; /* FreeType cannot read the file */

    memcpy(path_out, path, size);
    *index_out = index;
    return 1;
}

int fontsys_os_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                    char *path_out, int path_cap, int *index_out) {
    UniChar chars[2];
    CFIndex n;
    CFStringRef lang_str = NULL;
    CTFontRef font = NULL;
    int r;

    /* fontsys_find has already rejected surrogates and cp > 0x10FFFF. */
    if (cp >= 0x10000) {
        chars[0] = (UniChar)(0xD800 + ((cp - 0x10000) >> 10));
        chars[1] = (UniChar)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        n = 2;
    } else {
        chars[0] = (UniChar)cp;
        n = 1;
    }
    /* An empty or unconvertible tag means the user's own language. */
    if (*lang) lang_str = CFStringCreateWithCString(kCFAllocatorDefault, lang, kCFStringEncodingUTF8);

    if (style->monospace) font = find_menlo(style, chars, n);
    if (!font) font = map_fallback(style, lang_str, chars, n);
    RELEASE(lang_str);
    if (!font) return 0;
    r = font_location(ctx, font, cp, path_out, path_cap, index_out);
    RELEASE(font);
    return r;
}

/* No per-context state: CoreText caches fonts itself. */
void fontsys_os_destroy(void *os_state) {
    (void)os_state;
}
