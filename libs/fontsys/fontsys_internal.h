/*
 * fontsys private interface between the core (fontsys.c) and the per-OS
 * font lookup (fontsys_find_<os>). Not part of the public API, not installed.
 *
 * fontsys_find (fontsys.c) validates its arguments, rejects surrogates and
 * code points above U+10FFFF, counts the call for fontsys_debug_os_calls,
 * then hands over to fontsys_os_find.
 */
#ifndef FONTSYS_INTERNAL_H
#define FONTSYS_INTERNAL_H

#include "fontsys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented once per OS. Called with ctx, lang, style, path_out and
   index_out non-NULL, path_cap > 0 and cp a Unicode scalar value.
   Same return contract as fontsys_find; return -2 when the path does not
   fit in path_cap bytes (NUL included) and write nothing past path_cap. */
int fontsys_os_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                    char *path_out, int path_cap, int *index_out);

/* Implemented once per OS. Frees the per-context OS state; called by
   fontsys_destroy when *fontsys_os_state(ctx) is not NULL. */
void fontsys_os_destroy(void *os_state);

/* The per-context OS state slot, owned by the OS lookup: NULL on a new context. */
void **fontsys_os_state(fontsys_ctx *ctx);

/* The face index in the font file at path (UTF-8) whose PostScript name is
   ps_name, else the first face that maps cp, else -1 (also when the file
   cannot be opened). ps_name may be NULL. A variable font's named instances
   are searched too: those match as (instance << 16) | face, FreeType's own
   numbering, which fontsys_face_open accepts. For OS lookups that report a
   font file and a PostScript name but no face index (Apple). Each face is
   opened only for the comparison: none enter the face cache. */
int fontsys_file_face_index(fontsys_ctx *ctx, const char *path, const char *ps_name, unsigned cp);

#ifdef __cplusplus
}
#endif

#endif
