/*
 * fontsys core: context, face cache, coverage check and SDF glyph rendering
 * with FreeType, and the OS-neutral part of fontsys_find. The OS font lookup
 * itself (fontsys_os_find) lives in fontsys_find_<os>.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#endif

#include "fontsys.h"
#include "fontsys_internal.h"

#define FONTSYS_MAX_FACES 32

/* FreeType's SDF renderer accepts spreads in this range (ftsdfrend.c). */
#define FONTSYS_MIN_SPREAD 2
#define FONTSYS_MAX_SPREAD 32

typedef struct {
    char *path;     /* UTF-8, owned */
    int index;
    FT_Face face;
} fontsys_face_entry;

struct fontsys_ctx {
    FT_Library lib;
    fontsys_face_entry faces[FONTSYS_MAX_FACES];
    int face_count;
    int os_calls;   /* OS lookups made by fontsys_find (test hook) */
    void *os_state; /* owned by fontsys_os_find, freed by fontsys_os_destroy */
};

fontsys_ctx *fontsys_create(void) {
    fontsys_ctx *ctx = (fontsys_ctx *)calloc(1, sizeof(fontsys_ctx));
    if (!ctx) return NULL;
    if (FT_Init_FreeType(&ctx->lib) != 0) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void fontsys_destroy(fontsys_ctx *ctx) {
    int i;
    if (!ctx) return;
    for (i = 0; i < ctx->face_count; i++) {
        FT_Done_Face(ctx->faces[i].face);
        free(ctx->faces[i].path);
    }
    FT_Done_FreeType(ctx->lib);
    if (ctx->os_state) fontsys_os_destroy(ctx->os_state);
    free(ctx);
}

int fontsys_debug_os_calls(fontsys_ctx *ctx) {
    return ctx ? ctx->os_calls : 0;
}

void **fontsys_os_state(fontsys_ctx *ctx) {
    return &ctx->os_state;
}

int fontsys_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                 char *path_out, int path_cap, int *index_out) {
    if (!ctx || !style || !path_out || path_cap <= 0 || !index_out) return -1;
    if (!lang) lang = ""; /* the OS's own language (see fontsys.h) */
    /* Not Unicode scalar values: no font maps them, so never ask the OS. */
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return 0;
    ctx->os_calls++;
    return fontsys_os_find(ctx, cp, lang, style, path_out, path_cap, index_out);
}

#ifdef _WIN32
/* FT_New_Face goes through fopen(), which reads the path in the ANSI code
   page. For other paths, open the file with _wfopen and hand FreeType a
   stream over it. */

static unsigned long win_stream_read(FT_Stream stream, unsigned long offset,
                                     unsigned char *buffer, unsigned long count) {
    FILE *f = (FILE *)stream->descriptor.pointer;
    if (_fseeki64(f, (__int64)offset, SEEK_SET) != 0)
        return count == 0 ? 1 : 0; /* count 0 = seek only: nonzero means error */
    if (count == 0) return 0;
    return (unsigned long)fread(buffer, 1, count, f);
}

static void win_stream_close(FT_Stream stream) {
    fclose((FILE *)stream->descriptor.pointer);
    free(stream);
}

static int has_non_ascii(const char *s) {
    for (; *s; s++)
        if ((unsigned char)*s >= 0x80) return 1;
    return 0;
}

static FT_Error win_open_face_wide(FT_Library lib, const char *path, int index, FT_Face *face) {
    wchar_t *wpath;
    FILE *f;
    __int64 size;
    FT_Stream stream;
    FT_Open_Args args;
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);

    if (wlen <= 0) return FT_Err_Cannot_Open_Resource;
    wpath = (wchar_t *)malloc(sizeof(wchar_t) * wlen);
    if (!wpath) return FT_Err_Out_Of_Memory;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath, wlen);
    if (_wfopen_s(&f, wpath, L"rb") != 0) f = NULL;
    free(wpath);
    if (!f) return FT_Err_Cannot_Open_Resource;

    if (_fseeki64(f, 0, SEEK_END) != 0 || (size = _ftelli64(f)) <= 0 || size > 0x7FFFFFFF) {
        fclose(f);
        return FT_Err_Cannot_Open_Resource;
    }
    stream = (FT_Stream)calloc(1, sizeof(*stream));
    if (!stream) {
        fclose(f);
        return FT_Err_Out_Of_Memory;
    }
    stream->size = (unsigned long)size;
    stream->descriptor.pointer = f;
    stream->read = win_stream_read;
    stream->close = win_stream_close;

    memset(&args, 0, sizeof(args));
    args.flags = FT_OPEN_STREAM;
    args.stream = stream;
    /* FreeType calls stream->close (here: fclose + free) when the face is
       done, and also when FT_Open_Face fails. */
    return FT_Open_Face(lib, &args, index, face);
}
#endif

/* FT_New_Face on a UTF-8 path. */
static FT_Error open_ft_face(FT_Library lib, const char *path, int index, FT_Face *face) {
    FT_Error err = FT_New_Face(lib, path, index, face);
#ifdef _WIN32
    if (err != 0 && has_non_ascii(path))
        err = win_open_face_wide(lib, path, index, face);
#endif
    return err;
}

/* 1 if the face at index in path has PostScript name ps_name. */
static int ps_name_is(FT_Library lib, const char *path, int index, const char *ps_name) {
    FT_Face face = NULL;
    const char *name;
    int same;

    if (open_ft_face(lib, path, index, &face) != 0) return 0;
    name = FT_Get_Postscript_Name(face);
    same = name && strcmp(name, ps_name) == 0;
    FT_Done_Face(face);
    return same;
}

int fontsys_file_face_index(fontsys_ctx *ctx, const char *path, const char *ps_name, unsigned cp) {
    FT_Face face = NULL;
    FT_Long count, i, instances, j;
    int first_covering = -1;

    if (!ctx || !path) return -1;
    if (open_ft_face(ctx->lib, path, 0, &face) != 0) return -1;
    count = face->num_faces;
    FT_Done_Face(face);
    for (i = 0; i < count && i <= 0xFFFF; i++) {
        const char *name;
        if (open_ft_face(ctx->lib, path, (int)i, &face) != 0) continue;
        name = FT_Get_Postscript_Name(face);
        if (ps_name && name && strcmp(name, ps_name) == 0) {
            FT_Done_Face(face);
            return (int)i;
        }
        if (first_covering < 0 && FT_Get_Char_Index(face, cp) != 0) first_covering = (int)i;
        instances = face->style_flags >> 16;
        FT_Done_Face(face);
        /* Variable fonts (the Apple system fonts among them): named
           instances are faces (instance << 16) | i, instance from 1. */
        for (j = 1; ps_name && j <= instances && j <= 0x7FFF; j++) {
            int index = (int)((j << 16) | i);
            if (ps_name_is(ctx->lib, path, index, ps_name)) return index;
        }
    }
    return first_covering;
}

int fontsys_file_has(fontsys_ctx *ctx, const char *path, int index, unsigned cp) {
    FT_Face face = NULL;
    int i, has;

    if (!ctx || !path || index < 0) return 0;
    for (i = 0; i < ctx->face_count; i++) {
        if (ctx->faces[i].index == index && strcmp(ctx->faces[i].path, path) == 0)
            return FT_Get_Char_Index(ctx->faces[i].face, cp) != 0;
    }
    if (open_ft_face(ctx->lib, path, index, &face) != 0) return 0;
    has = FT_Get_Char_Index(face, cp) != 0;
    FT_Done_Face(face);
    return has;
}

int fontsys_face_open(fontsys_ctx *ctx, const char *path, int index) {
    FT_Face face = NULL;
    char *copy;
    size_t len;
    int i;

    if (!ctx || !path || index < 0) return -1;
    for (i = 0; i < ctx->face_count; i++) {
        if (ctx->faces[i].index == index && strcmp(ctx->faces[i].path, path) == 0)
            return i;
    }
    if (ctx->face_count >= FONTSYS_MAX_FACES) return -2;

    if (open_ft_face(ctx->lib, path, index, &face) != 0) return -1;

    len = strlen(path);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        FT_Done_Face(face);
        return -3;
    }
    memcpy(copy, path, len + 1);

    i = ctx->face_count++;
    ctx->faces[i].path = copy;
    ctx->faces[i].index = index;
    ctx->faces[i].face = face;
    return i;
}

static FT_Face get_face(fontsys_ctx *ctx, int face) {
    if (!ctx || face < 0 || face >= ctx->face_count) return NULL;
    return ctx->faces[face].face;
}

int fontsys_face_has(fontsys_ctx *ctx, int face, unsigned cp) {
    FT_Face f = get_face(ctx, face);
    if (!f) return 0;
    return FT_Get_Char_Index(f, cp) != 0;
}

int fontsys_face_is_bold(fontsys_ctx *ctx, int face) {
    FT_Face f = get_face(ctx, face);
    return f && (f->style_flags & FT_STYLE_FLAG_BOLD) != 0;
}

int fontsys_face_is_italic(fontsys_ctx *ctx, int face) {
    FT_Face f = get_face(ctx, face);
    return f && (f->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
}

int fontsys_face_is_mono(fontsys_ctx *ctx, int face) {
    FT_Face f = get_face(ctx, face);
    return f && FT_IS_FIXED_WIDTH(f);
}

int fontsys_render_sdf(fontsys_ctx *ctx, int face, unsigned cp, int em_px, int spread_px,
                       unsigned char *buf, int buf_cap, fontsys_glyph *out) {
    FT_Face f = get_face(ctx, face);
    FT_Int spread = spread_px;
    FT_UInt gi;
    FT_GlyphSlot slot;
    const FT_Bitmap *bm;
    const unsigned char *src;
    int w, h, y;

    if (!f || !out || em_px <= 0 || spread_px < FONTSYS_MIN_SPREAD || spread_px > FONTSYS_MAX_SPREAD)
        return -2;
    gi = FT_Get_Char_Index(f, cp);
    if (gi == 0) return 0;

    if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)em_px) != 0) return -3;
    if (FT_Property_Set(ctx->lib, "bsdf", "spread", &spread) != 0) return -3;
    if (FT_Load_Glyph(f, gi, FT_LOAD_DEFAULT) != 0) return -3;
    slot = f->glyph;
    /* Rasterize first, then build the SDF from the coverage bitmap (the
       "bsdf" renderer). FreeType's outline "sdf" renderer gets the inside
       test wrong where contours overlap (CFF fonts such as Noto Sans CJK):
       notches at stroke crossings, and stray filled blocks in the padding.
       The raster is overlap-safe. Same padding (spread on every side), the
       same left/top shift and the same 128 = edge, inside-high encoding.
       For a glyph that loads as an embedded bitmap, NORMAL is a no-op. */
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return -3;
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_SDF) != 0) return -3;

    bm = &slot->bitmap;
    w = (int)bm->width;
    h = (int)bm->rows;
    /* An empty outline (a space) renders to a 0x0 bitmap. */
    if (w > 0 && h > 0 && bm->pixel_mode != FT_PIXEL_MODE_GRAY) return -3;

    out->width = w;
    out->height = h;
    out->left = slot->bitmap_left;
    out->top = slot->bitmap_top;
    out->advance = (float)(slot->advance.x / 64.0);

    if (w * h > buf_cap || (w * h > 0 && !buf)) return -1;

    /* A negative pitch means rows are stored bottom-up from buffer. */
    src = bm->pitch >= 0 ? bm->buffer : bm->buffer + (size_t)(h - 1) * (size_t)(-bm->pitch);
    for (y = 0; y < h; y++)
        memcpy(buf + (size_t)y * w, src + (ptrdiff_t)y * bm->pitch, (size_t)w);
    return 1;
}
