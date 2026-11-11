/*
 * fontsys colour emoji: HarfBuzz joins an emoji sequence (skin tone, ZWJ
 * family, flag, keycap, tag flag) into the font's one glyph for it, and
 * FreeType renders that glyph in colour: COLR (v0) layers blended by
 * FreeType, or a colour bitmap strike (sbix/CBDT, PNG; needs
 * FT_CONFIG_OPTION_USE_PNG) scaled to the requested size.
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

#include "fontsys.h"
#include "fontsys_internal.h"

/* The face's HarfBuzz font, created on first use. HarfBuzz reads the
   font's tables through FreeType (hb_ft_face) but maps characters and
   applies GSUB itself (its own OpenType functions), so shaping never
   changes the FreeType face's size or glyph slot. */
static hb_font_t *hb_font_for(fontsys_ctx *ctx, int face) {
    void **slot = fontsys_hb_font_slot(ctx, face);
    FT_Face f = fontsys_ft_face(ctx, face);
    if (!slot || !f) return NULL;
    if (!*slot) {
        hb_face_t *hf = hb_ft_face_create_referenced(f);
        hb_font_t *font = hb_font_create(hf);
        hb_face_destroy(hf);
        if (font == hb_font_get_empty()) return NULL;
        *slot = font;
    }
    return (hb_font_t *)*slot;
}

int fontsys_face_is_color(fontsys_ctx *ctx, int face) {
    FT_Face f = fontsys_ft_face(ctx, face);
    return f && FT_HAS_COLOR(f);
}

int fontsys_shape_cluster(fontsys_ctx *ctx, int face, const unsigned *cps, int n,
                          unsigned *glyph_out) {
    hb_font_t *font;
    hb_buffer_t *buf;
    hb_glyph_info_t *info;
    unsigned len = 0;
    int r;

    if (!cps || n <= 0 || !glyph_out) return -2;
    font = hb_font_for(ctx, face);
    if (!font) return -2;
    buf = hb_buffer_create();
    if (!hb_buffer_allocation_successful(buf)) {
        hb_buffer_destroy(buf);
        return -3;
    }
    hb_buffer_add_codepoints(buf, (const hb_codepoint_t *)cps, n, 0, n);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buf);
    /* Drop what the font leaves of U+FE0F, ZWJ and tags rather than keeping
       them as invisible glyphs: "one glyph" then means the joined picture. */
    hb_buffer_set_flags(buf, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
    hb_shape(font, buf, NULL, 0);
    info = hb_buffer_get_glyph_infos(buf, &len);
    r = len == 1 && info[0].codepoint != 0;
    if (r) *glyph_out = info[0].codepoint;
    hb_buffer_destroy(buf);
    return r;
}

/* The strike to draw at em_px: the smallest at least that big, else the
   biggest. */
static int pick_strike(FT_Face f, int em_px) {
    int i, best = -1, biggest = 0;
    FT_Pos want = (FT_Pos)em_px * 64;
    for (i = 0; i < f->num_fixed_sizes; i++) {
        FT_Pos ppem = f->available_sizes[i].y_ppem;
        if (ppem > f->available_sizes[biggest].y_ppem) biggest = i;
        if (ppem >= want && (best < 0 || ppem < f->available_sizes[best].y_ppem)) best = i;
    }
    return best >= 0 ? best : biggest;
}

/* Writes the slot's BGRA (premultiplied) bitmap into buf as dw x dh RGBA with
   straight alpha, scaled by s with an area (box) filter in premultiplied
   space, which handles both shrinking a strike and the unscaled case. */
static void convert_bgra(const FT_Bitmap *bm, double s, int dw, int dh, unsigned char *buf) {
    int w = (int)bm->width, h = (int)bm->rows, dx, dy, sx, sy;
    const unsigned char *src = bm->pitch >= 0 ? bm->buffer
                                              : bm->buffer + (size_t)(h - 1) * (size_t)(-bm->pitch);
    for (dy = 0; dy < dh; dy++) {
        double y0 = dy / s, y1 = (dy + 1) / s;
        int sy0 = (int)floor(y0), sy1 = (int)ceil(y1);
        if (sy1 > h) sy1 = h;
        for (dx = 0; dx < dw; dx++) {
            double x0 = dx / s, x1 = (dx + 1) / s;
            int sx0 = (int)floor(x0), sx1 = (int)ceil(x1);
            double acc[4] = {0, 0, 0, 0}, area = 0;
            unsigned char *out = buf + ((size_t)dy * dw + dx) * 4;
            if (sx1 > w) sx1 = w;
            for (sy = sy0; sy < sy1; sy++) {
                double wy = fmin(y1, sy + 1.0) - fmax(y0, (double)sy);
                const unsigned char *row = src + (ptrdiff_t)sy * bm->pitch;
                if (wy <= 0) continue;
                for (sx = sx0; sx < sx1; sx++) {
                    double wx = fmin(x1, sx + 1.0) - fmax(x0, (double)sx), k = wx * wy;
                    const unsigned char *p = row + (size_t)sx * 4;
                    if (wx <= 0) continue;
                    acc[0] += p[2] * k; /* B G R A -> R G B A */
                    acc[1] += p[1] * k;
                    acc[2] += p[0] * k;
                    acc[3] += p[3] * k;
                    area += k;
                }
            }
            if (area <= 0 || acc[3] <= 0) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            {
                double a = acc[3] / area;
                int c;
                for (c = 0; c < 3; c++) {
                    double v = acc[c] / acc[3] * 255.0 + 0.5; /* un-premultiply */
                    out[c] = (unsigned char)(v > 255 ? 255 : v);
                }
                out[3] = (unsigned char)(a + 0.5 > 255 ? 255 : a + 0.5);
            }
        }
    }
}

int fontsys_render_color(fontsys_ctx *ctx, int face, unsigned glyph, int em_px,
                         unsigned char *buf, int buf_cap, fontsys_glyph *out) {
    FT_Face f = fontsys_ft_face(ctx, face);
    FT_GlyphSlot slot;
    const FT_Bitmap *bm;
    double s = 1.0;
    int dw, dh;

    if (!f || !out || em_px <= 0 || glyph == 0 || glyph >= (unsigned)f->num_glyphs) return -2;
    if (!FT_HAS_COLOR(f)) return 0;

    if (FT_HAS_FIXED_SIZES(f)) {
        /* Colour bitmap strike (sbix, CBDT). A build without PNG support
           cannot load it: no colour image. */
        int strike = pick_strike(f, em_px);
        if (FT_Select_Size(f, strike) != 0) return -3;
        if (FT_Load_Glyph(f, glyph, FT_LOAD_COLOR) != 0) return 0;
        s = (double)em_px * 64.0 / (double)f->available_sizes[strike].y_ppem;
    } else {
        /* COLR v0: FreeType blends the layers into a BGRA bitmap. A glyph
           with no v0 layers (outline only, COLRv1 only) renders grey. */
        if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)em_px) != 0) return -3;
        if (FT_Load_Glyph(f, glyph, FT_LOAD_COLOR) != 0) return -3;
        if (f->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
            FT_Render_Glyph(f->glyph, FT_RENDER_MODE_NORMAL) != 0)
            return -3;
    }
    slot = f->glyph;
    bm = &slot->bitmap;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP || bm->pixel_mode != FT_PIXEL_MODE_BGRA) return 0;
    if (bm->width == 0 || bm->rows == 0) return 0;

    dw = (int)(bm->width * s + 0.5);
    dh = (int)(bm->rows * s + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    out->width = dw;
    out->height = dh;
    out->left = (int)floor(slot->bitmap_left * s + 0.5);
    out->top = (int)floor(slot->bitmap_top * s + 0.5);
    out->advance = (float)(slot->advance.x / 64.0 * s);
    if (!buf || (size_t)dw * (size_t)dh * 4 > (size_t)(buf_cap < 0 ? 0 : buf_cap)) return -1;

    convert_bgra(bm, s, dw, dh, buf);
    return 1;
}
