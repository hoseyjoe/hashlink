/*
 * fontsys_find for platforms without an OS font lookup: nothing is ever
 * found, and the OS is never asked.
 */
#include "fontsys.h"

int fontsys_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                 char *path_out, int path_cap, int *index_out) {
    (void)ctx; (void)cp; (void)lang; (void)style;
    (void)path_out; (void)path_cap; (void)index_out;
    return 0;
}
