/*
 * HashLink bindings for fontsys (fontsys.h). The only file that uses
 * HashLink types; see fontsys/FontSys.hx for the Haxe side.
 *
 * HL/C calls natives as <lib>_<name>, so these primitives carry an "hl_"
 * prefix: "fontsys", "create" would be fontsys_create, the C API itself.
 */
#define HL_NAME(n) fontsys_##n
#include <hl.h>
#include <string.h>

#include "fontsys.h"

/* Big enough for any font path the OS lookups return (MAX_PATH-ish on
   Windows, PATH_MAX on Unix); a longer one is reported as not found. */
#define FONTSYS_HL_PATH_CAP 4096

/* GC-allocated with a finalizer: freed by fontsys_hl_destroy, or by the
   GC if the Haxe object is dropped without dispose(). */
typedef struct {
	void (*finalize)(void *);
	fontsys_ctx *ctx;
} fontsys_hl_ctx;

static void fontsys_hl_finalize(void *p) {
	fontsys_hl_ctx *h = (fontsys_hl_ctx *)p;
	if (h->ctx) {
		fontsys_destroy(h->ctx);
		h->ctx = NULL;
	}
}

static fontsys_ctx *get_ctx(fontsys_hl_ctx *h) {
	if (!h || !h->ctx) hl_error("fontsys: context disposed");
	return h->ctx;
}

HL_PRIM fontsys_hl_ctx *HL_NAME(hl_create)(void) {
	fontsys_hl_ctx *h;
	fontsys_ctx *ctx = fontsys_create();
	if (!ctx) return NULL;
	h = (fontsys_hl_ctx *)hl_gc_alloc_finalizer(sizeof(fontsys_hl_ctx));
	h->finalize = fontsys_hl_finalize;
	h->ctx = ctx;
	return h;
}

HL_PRIM void HL_NAME(hl_destroy)(fontsys_hl_ctx *h) {
	if (h) fontsys_hl_finalize(h);
}

/* Returns the UTF-8, NUL-terminated font path (GC bytes) and sets *index,
   or NULL when nothing covers cp or the lookup failed. lang is UTF-8. */
HL_PRIM vbyte *HL_NAME(hl_find)(fontsys_hl_ctx *h, int cp, vbyte *lang, int weight, bool italic,
                                bool mono, int *index) {
	char path[FONTSYS_HL_PATH_CAP];
	fontsys_style style;
	int r;
	style.weight = weight;
	style.italic = italic ? 1 : 0;
	style.monospace = mono ? 1 : 0;
	*index = 0;
	r = fontsys_find(get_ctx(h), (unsigned)cp, lang ? (const char *)lang : "", &style,
	                 path, FONTSYS_HL_PATH_CAP, index);
	if (r != 1) return NULL;
	return hl_copy_bytes((vbyte *)path, (int)strlen(path) + 1);
}

HL_PRIM int HL_NAME(hl_face_open)(fontsys_hl_ctx *h, vbyte *path, int index) {
	if (!path) return -1;
	return fontsys_face_open(get_ctx(h), (const char *)path, index);
}

HL_PRIM bool HL_NAME(hl_face_has)(fontsys_hl_ctx *h, int face, int cp) {
	return fontsys_face_has(get_ctx(h), face, (unsigned)cp) == 1;
}

/* fontsys_render_sdf with the glyph metrics returned through refs. Returns
   1 ok, 0 face lacks cp, -1 buf too small (width/height still set), <0 error. */
HL_PRIM int HL_NAME(hl_render_sdf)(fontsys_hl_ctx *h, int face, int cp, int em_px, int spread_px,
                                   vbyte *buf, int buf_cap, int *width, int *height, int *left,
                                   int *top, double *advance) {
	fontsys_glyph g;
	int r;
	memset(&g, 0, sizeof(g));
	r = fontsys_render_sdf(get_ctx(h), face, (unsigned)cp, em_px, spread_px,
	                       (unsigned char *)buf, buf ? buf_cap : 0, &g);
	*width = g.width;
	*height = g.height;
	*left = g.left;
	*top = g.top;
	*advance = g.advance;
	return r;
}

HL_PRIM bool HL_NAME(hl_face_is_color)(fontsys_hl_ctx *h, int face) {
	return fontsys_face_is_color(get_ctx(h), face) == 1;
}

/* fontsys_shape_cluster over n code points passed as int32 bytes. Returns 1
   with *glyph set when they shape to one glyph, 0 if not, <0 on error. */
HL_PRIM int HL_NAME(hl_shape_cluster)(fontsys_hl_ctx *h, int face, vbyte *cps, int n, int *glyph) {
	unsigned g = 0;
	int r;
	*glyph = 0;
	if (!cps || n <= 0) return -2;
	r = fontsys_shape_cluster(get_ctx(h), face, (const unsigned *)cps, n, &g);
	if (r == 1) *glyph = (int)g;
	return r;
}

/* fontsys_render_color with the metrics returned through refs. Returns 1 ok
   (RGBA, straight alpha), 0 no colour image, -1 buf too small (width/height
   still set), <0 error. */
HL_PRIM int HL_NAME(hl_render_color)(fontsys_hl_ctx *h, int face, int glyph, int em_px, vbyte *buf,
                                     int buf_cap, int *width, int *height, int *left, int *top,
                                     double *advance) {
	fontsys_glyph g;
	int r;
	memset(&g, 0, sizeof(g));
	r = fontsys_render_color(get_ctx(h), face, (unsigned)glyph, em_px, (unsigned char *)buf,
	                         buf ? buf_cap : 0, &g);
	*width = g.width;
	*height = g.height;
	*left = g.left;
	*top = g.top;
	*advance = g.advance;
	return r;
}

#define _CTX _ABSTRACT(fontsys_hl_ctx)

DEFINE_PRIM(_CTX, hl_create, _NO_ARG);
DEFINE_PRIM(_VOID, hl_destroy, _CTX);
DEFINE_PRIM(_BYTES, hl_find, _CTX _I32 _BYTES _I32 _BOOL _BOOL _REF(_I32));
DEFINE_PRIM(_I32, hl_face_open, _CTX _BYTES _I32);
DEFINE_PRIM(_BOOL, hl_face_has, _CTX _I32 _I32);
DEFINE_PRIM(_I32, hl_render_sdf, _CTX _I32 _I32 _I32 _I32 _BYTES _I32 _REF(_I32) _REF(_I32) _REF(_I32) _REF(_I32) _REF(_F64));
DEFINE_PRIM(_BOOL, hl_face_is_color, _CTX _I32);
DEFINE_PRIM(_I32, hl_shape_cluster, _CTX _I32 _BYTES _I32 _REF(_I32));
DEFINE_PRIM(_I32, hl_render_color, _CTX _I32 _I32 _I32 _BYTES _I32 _REF(_I32) _REF(_I32) _REF(_I32) _REF(_I32) _REF(_F64));
