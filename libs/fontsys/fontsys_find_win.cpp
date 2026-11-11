/*
 * fontsys_os_find on Windows: DirectWrite system font fallback
 * (IDWriteFontFallback::MapCharacters, Windows 8.1+). The mapped font's
 * file path comes from the local font file loader.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite_2.h>
#include <stdlib.h>

#include "fontsys_internal.h"

#pragma comment(lib, "dwrite.lib")

namespace {

/* Per-context DirectWrite objects, created on the first lookup. */
struct win_state {
    IDWriteFactory2 *factory;
    IDWriteFontCollection *system_fonts;
    IDWriteFontFallback *fallback;
};

template <class T> void release(T *&p) {
    if (p) {
        p->Release();
        p = NULL;
    }
}

void free_state(win_state *s) {
    release(s->fallback);
    release(s->system_fonts);
    release(s->factory);
    free(s);
}

win_state *get_state(fontsys_ctx *ctx) {
    void **slot = fontsys_os_state(ctx);
    win_state *s;

    if (*slot) return (win_state *)*slot;
    s = (win_state *)calloc(1, sizeof(win_state));
    if (!s) return NULL;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                                   reinterpret_cast<IUnknown **>(&s->factory))) ||
        FAILED(s->factory->GetSystemFontCollection(&s->system_fonts, FALSE)) ||
        FAILED(s->factory->GetSystemFontFallback(&s->fallback))) {
        free_state(s);
        return NULL;
    }
    *slot = s;
    return s;
}

/* One code point of text in one locale, for MapCharacters. It lives on the
   stack of the caller, so reference counting is a no-op. */
class one_char_source : public IDWriteTextAnalysisSource {
public:
    one_char_source(const WCHAR *text, UINT32 len, const WCHAR *locale)
        : text_(text), len_(len), locale_(locale) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextAnalysisSource)) {
            *out = this;
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, const WCHAR **text, UINT32 *len) override {
        *text = pos < len_ ? text_ + pos : NULL;
        *len = pos < len_ ? len_ - pos : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 pos, const WCHAR **text, UINT32 *len) override {
        *text = pos > 0 && pos <= len_ ? text_ : NULL;
        *len = pos > 0 && pos <= len_ ? pos : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 pos, UINT32 *len, const WCHAR **locale) override {
        *len = pos < len_ ? len_ - pos : 0;
        *locale = locale_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 pos, UINT32 *len,
                                                    IDWriteNumberSubstitution **sub) override {
        *len = pos < len_ ? len_ - pos : 0;
        *sub = NULL;
        return S_OK;
    }

private:
    const WCHAR *text_;
    UINT32 len_;
    const WCHAR *locale_;
};

bool covers(IDWriteFont *font, unsigned cp) {
    BOOL has = FALSE;
    return SUCCEEDED(font->HasCharacter(cp, &has)) && has;
}

/* The Consolas face closest to the style, if it covers cp. */
IDWriteFont *find_consolas(win_state *s, unsigned cp, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style) {
    UINT32 i;
    BOOL exists = FALSE;
    IDWriteFontFamily *family = NULL;
    IDWriteFont *font = NULL;

    if (FAILED(s->system_fonts->FindFamilyName(L"Consolas", &i, &exists)) || !exists) return NULL;
    if (FAILED(s->system_fonts->GetFontFamily(i, &family))) return NULL;
    if (FAILED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, &font))) font = NULL;
    release(family);
    if (font && !covers(font, cp)) release(font);
    return font;
}

/* The system fallback font for cp in the given locale, or NULL. */
IDWriteFont *map_fallback(win_state *s, unsigned cp, const WCHAR *locale, DWRITE_FONT_WEIGHT weight,
                          DWRITE_FONT_STYLE style) {
    WCHAR text[2];
    UINT32 len, mapped_len = 0;
    FLOAT scale = 1.0f;
    IDWriteFont *font = NULL;

    if (cp >= 0x10000) {
        text[0] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
        text[1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        len = 2;
    } else {
        text[0] = (WCHAR)cp;
        len = 1;
    }
    one_char_source source(text, len, locale);
    if (FAILED(s->fallback->MapCharacters(&source, 0, len, s->system_fonts, NULL, weight, style,
                                          DWRITE_FONT_STRETCH_NORMAL, &mapped_len, &font, &scale)))
        return NULL;
    if (font && !covers(font, cp)) release(font);
    return font;
}

/* The UTF-8 file path and face index of a font backed by one local file.
   1 ok, 0 not a local single-file font, -2 path_cap too small, -3 error. */
int font_location(IDWriteFont *font, char *path_out, int path_cap, int *index_out) {
    IDWriteFontFace *face = NULL;
    IDWriteFontFile *file = NULL;
    IDWriteFontFileLoader *loader = NULL;
    IDWriteLocalFontFileLoader *local = NULL;
    const void *key = NULL;
    UINT32 key_size = 0, count = 0, wlen = 0;
    WCHAR *wpath = NULL;
    int r = -3, n;

    if (FAILED(font->CreateFontFace(&face))) goto done;
    if (FAILED(face->GetFiles(&count, NULL))) goto done;
    if (count != 1) {
        r = 0;
        goto done;
    }
    if (FAILED(face->GetFiles(&count, &file))) goto done;
    if (FAILED(file->GetReferenceKey(&key, &key_size)) || FAILED(file->GetLoader(&loader))) goto done;
    if (FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                      reinterpret_cast<void **>(&local)))) {
        r = 0; /* e.g. a memory or remote font: no path to give */
        goto done;
    }
    if (FAILED(local->GetFilePathLengthFromKey(key, key_size, &wlen))) goto done;
    wpath = (WCHAR *)malloc(sizeof(WCHAR) * (wlen + 1));
    if (!wpath || FAILED(local->GetFilePathFromKey(key, key_size, wpath, wlen + 1))) goto done;

    n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
    if (n <= 0) goto done;
    if (n > path_cap) {
        path_out[0] = 0;
        r = -2;
        goto done;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path_out, path_cap, NULL, NULL) != n) goto done;
    *index_out = (int)face->GetIndex();
    r = 1;

done:
    free(wpath);
    release(local);
    release(loader);
    release(file);
    release(face);
    return r;
}

} // namespace

extern "C" int fontsys_os_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                               char *path_out, int path_cap, int *index_out) {
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE font_style = style->italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    IDWriteFont *font = NULL;
    win_state *s = get_state(ctx);
    int r;

    if (!s) return -3;
    /* An empty or unusable tag means the user's own locale. */
    if (!*lang || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, lang, -1, locale, LOCALE_NAME_MAX_LENGTH))
        if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) locale[0] = 0;
    weight = (DWRITE_FONT_WEIGHT)(style->weight < 1 ? 400 : style->weight > 999 ? 999 : style->weight);

    if (style->monospace) font = find_consolas(s, cp, weight, font_style);
    if (!font) font = map_fallback(s, cp, locale, weight, font_style);
    if (!font) return 0;
    r = font_location(font, path_out, path_cap, index_out);
    release(font);
    return r;
}

extern "C" void fontsys_os_destroy(void *os_state) {
    free_state((win_state *)os_state);
}
