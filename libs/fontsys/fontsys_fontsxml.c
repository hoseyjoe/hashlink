/*
 * Android /system/etc/fonts.xml: a small parser for the subset Android
 * writes (no DTD, no entities, no CDATA) and the font choice used by the
 * API 24-28 lookup. See fontsys_fontsxml.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontsys_fontsxml.h"

/* ---- parser ---- */

typedef struct {
    const char *p, *end;
} xml_cursor;

typedef struct {
    const char *s; /* points into the input, not NUL-terminated; NULL when absent */
    size_t len;
} xml_str;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int str_is(xml_str v, const char *lit) {
    size_t n = strlen(lit);
    return v.s && v.len == n && memcmp(v.s, lit, n) == 0;
}

/* Moves past the next occurrence of lit; to the end when there is none. */
static void skip_past(xml_cursor *c, const char *lit) {
    size_t n = strlen(lit);
    while (c->p < c->end) {
        if ((size_t)(c->end - c->p) >= n && memcmp(c->p, lit, n) == 0) {
            c->p += n;
            return;
        }
        c->p++;
    }
}

static int at(const xml_cursor *c, const char *lit) {
    size_t n = strlen(lit);
    return (size_t)(c->end - c->p) >= n && memcmp(c->p, lit, n) == 0;
}

static xml_str read_name(xml_cursor *c) {
    xml_str r;
    r.s = c->p;
    while (c->p < c->end && !is_space(*c->p) && *c->p != '>' && *c->p != '/' && *c->p != '=')
        c->p++;
    r.len = (size_t)(c->p - r.s);
    return r;
}

#define MAX_ATTRS 8

typedef struct {
    xml_str name;
    int self_closing;
    int complete; /* the closing '>' was found */
    int attr_count;
    xml_str attr_names[MAX_ATTRS];
    xml_str attr_values[MAX_ATTRS];
} xml_tag;

/* Reads a start tag; c->p is just past '<'. Leaves c->p past its '>'. */
static void read_start_tag(xml_cursor *c, xml_tag *tag) {
    memset(tag, 0, sizeof(*tag));
    tag->name = read_name(c);
    while (c->p < c->end) {
        xml_str name, value = {NULL, 0};
        char ch = *c->p;
        if (is_space(ch)) {
            c->p++;
            continue;
        }
        if (ch == '>') {
            c->p++;
            tag->complete = 1;
            return;
        }
        if (ch == '/') {
            c->p++;
            if (c->p < c->end && *c->p == '>') {
                c->p++;
                tag->self_closing = 1;
                tag->complete = 1;
                return;
            }
            continue;
        }
        name = read_name(c);
        if (name.len == 0) { /* a stray '=' */
            c->p++;
            continue;
        }
        while (c->p < c->end && is_space(*c->p)) c->p++;
        if (c->p < c->end && *c->p == '=') {
            c->p++;
            while (c->p < c->end && is_space(*c->p)) c->p++;
            if (c->p < c->end && (*c->p == '"' || *c->p == '\'')) {
                char q = *c->p++;
                value.s = c->p;
                while (c->p < c->end && *c->p != q) c->p++;
                if (c->p >= c->end) return; /* unterminated value */
                value.len = (size_t)(c->p - value.s);
                c->p++;
            } else {
                value = read_name(c);
            }
        }
        if (tag->attr_count < MAX_ATTRS) {
            tag->attr_names[tag->attr_count] = name;
            tag->attr_values[tag->attr_count] = value;
            tag->attr_count++;
        }
    }
}

static xml_str attr(const xml_tag *tag, const char *name) {
    xml_str none = {NULL, 0};
    int i;
    for (i = 0; i < tag->attr_count; i++)
        if (str_is(tag->attr_names[i], name)) return tag->attr_values[i];
    return none;
}

/* A non-negative decimal integer, else def. */
static int attr_int(const xml_tag *tag, const char *name, int def) {
    xml_str v = attr(tag, name);
    size_t i;
    long n = 0;
    if (!v.s || v.len == 0 || v.len > 9) return def;
    for (i = 0; i < v.len; i++) {
        if (v.s[i] < '0' || v.s[i] > '9') return def;
        n = n * 10 + (v.s[i] - '0');
    }
    return (int)n;
}

/* A decimal number ("700", "-12.5", "87.5") in *out: 1, else 0. Not strtod,
   whose decimal point follows the C locale. */
static int parse_number(xml_str v, float *out) {
    size_t i = 0;
    double n = 0, scale = 1;
    int neg = 0, digits = 0, frac = 0;
    if (!v.s || v.len == 0) return 0;
    if (v.s[0] == '-' || v.s[0] == '+') {
        neg = v.s[0] == '-';
        i++;
    }
    for (; i < v.len; i++) {
        char ch = v.s[i];
        if (ch == '.' && !frac) {
            frac = 1;
        } else if (ch >= '0' && ch <= '9') {
            if (frac) {
                scale /= 10;
                n += (ch - '0') * scale;
            } else {
                n = n * 10 + (ch - '0');
            }
            digits++;
        } else {
            return 0;
        }
    }
    if (!digits) return 0;
    *out = (float)(neg ? -n : n);
    return 1;
}

/* <axis tag="wght" stylevalue="700"/> of the font being read: appended to its axes. */
static void add_axis(fontsys_xml_font *f, const xml_tag *tag) {
    xml_str t = attr(tag, "tag");
    float value;
    if (f->axis_count >= FONTSYS_XML_MAX_AXES || t.len != 4) return;
    if (!parse_number(attr(tag, "stylevalue"), &value)) return;
    f->axes[f->axis_count].tag = FONTSYS_AXIS_TAG((unsigned char)t.s[0], (unsigned char)t.s[1],
                                                  (unsigned char)t.s[2], (unsigned char)t.s[3]);
    f->axes[f->axis_count].value = value;
    f->axis_count++;
}

static char *dup_concat(const char *a, const char *b, size_t blen) {
    size_t alen = a ? strlen(a) : 0;
    char *r = (char *)malloc(alen + blen + 1);
    if (!r) return NULL;
    if (alen) memcpy(r, a, alen);
    if (blen) memcpy(r + alen, b, blen);
    r[alen + blen] = 0;
    return r;
}

static char *dup_str(xml_str v) {
    return dup_concat(NULL, v.s ? v.s : "", v.s ? v.len : 0);
}

static int add_family(fontsys_xml_table *t, int *cap, const xml_tag *tag) {
    fontsys_xml_family *f;
    if (t->family_count == *cap) {
        int n = *cap ? *cap * 2 : 32;
        fontsys_xml_family *grown =
            (fontsys_xml_family *)realloc(t->families, sizeof(*grown) * (size_t)n);
        if (!grown) return -1;
        t->families = grown;
        *cap = n;
    }
    f = &t->families[t->family_count];
    f->name = dup_str(attr(tag, "name"));
    f->lang = dup_str(attr(tag, "lang"));
    f->first_font = t->font_count;
    f->font_count = 0;
    if (!f->name || !f->lang) {
        free(f->name);
        free(f->lang);
        return -1;
    }
    t->family_count++;
    return 0;
}

static int add_font(fontsys_xml_table *t, int *cap, const xml_tag *tag, int family,
                    const char *font_dir, xml_str file) {
    fontsys_xml_font *f;
    if (t->font_count == *cap) {
        int n = *cap ? *cap * 2 : 64;
        fontsys_xml_font *grown = (fontsys_xml_font *)realloc(t->fonts, sizeof(*grown) * (size_t)n);
        if (!grown) return -1;
        t->fonts = grown;
        *cap = n;
    }
    f = &t->fonts[t->font_count];
    f->path = dup_concat(file.s[0] == '/' ? NULL : font_dir, file.s, file.len);
    if (!f->path) return -1;
    f->weight = attr_int(tag, "weight", 400);
    f->italic = str_is(attr(tag, "style"), "italic");
    f->index = attr_int(tag, "index", 0);
    f->serif_fallback = attr(tag, "fallbackFor").len > 0;
    f->family = family;
    f->axis_count = 0;
    t->font_count++;
    t->families[family].font_count++;
    return 0;
}

/* The file name of a <font>: its first non-blank text, comments skipped.
   c->p is just past the start tag; leaves it at the '<' that ends the text.
   len 0 when there is none, or when the input ends inside the text. */
static xml_str font_file(xml_cursor *c) {
    xml_str file = {NULL, 0};
    while (c->p < c->end) {
        const char *s = c->p, *e;
        while (c->p < c->end && *c->p != '<') c->p++;
        if (c->p >= c->end) break; /* truncated */
        e = c->p;
        while (s < e && is_space(*s)) s++;
        while (e > s && is_space(e[-1])) e--;
        if (!file.s && e > s) {
            file.s = s;
            file.len = (size_t)(e - s);
        }
        if (!at(c, "<!--")) return file;
        skip_past(c, "-->");
    }
    file.s = NULL;
    file.len = 0;
    return file;
}

int fontsys_xml_parse(const char *xml, size_t len, const char *font_dir, fontsys_xml_table *out) {
    xml_cursor c;
    int family_cap = 0, font_cap = 0, family = -1;
    int font = -1; /* the <font> whose <axis> children are being read */

    memset(out, 0, sizeof(*out));
    if (!xml) return 0;
    c.p = xml;
    c.end = xml + len;
    while (c.p < c.end) {
        xml_tag tag;
        if (*c.p != '<') {
            c.p++;
            continue;
        }
        if (at(&c, "<!--")) {
            skip_past(&c, "-->");
            continue;
        }
        if (at(&c, "<?")) {
            skip_past(&c, "?>");
            continue;
        }
        if (at(&c, "<!") || at(&c, "</")) {
            if (at(&c, "</family")) family = font = -1;
            if (at(&c, "</font")) font = -1;
            skip_past(&c, ">");
            continue;
        }
        c.p++;
        read_start_tag(&c, &tag);
        if (!tag.complete) break;
        if (str_is(tag.name, "family")) {
            if (add_family(out, &family_cap, &tag) < 0) goto oom;
            family = tag.self_closing ? -1 : out->family_count - 1;
        } else if (str_is(tag.name, "font") && family >= 0 && !tag.self_closing) {
            xml_str file = font_file(&c);
            font = -1;
            if (file.len) {
                if (add_font(out, &font_cap, &tag, family, font_dir, file) < 0) goto oom;
                font = out->font_count - 1;
            }
        } else if (str_is(tag.name, "axis") && font >= 0) {
            add_axis(&out->fonts[font], &tag);
        }
    }
    return 0;

oom:
    fontsys_xml_free(out);
    return -1;
}

int fontsys_xml_load(const char *path, const char *font_dir, fontsys_xml_table *out) {
    FILE *f;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int r;

    memset(out, 0, sizeof(*out));
    f = fopen(path, "rb");
    if (!f) return -1;
    for (;;) {
        size_t n;
        if (len == cap) {
            char *grown;
            cap = cap ? cap * 2 : 64 * 1024;
            grown = (char *)realloc(buf, cap);
            if (!grown) {
                free(buf);
                fclose(f);
                return -1;
            }
            buf = grown;
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }
    r = ferror(f) ? -1 : 0;
    fclose(f);
    if (r == 0) r = fontsys_xml_parse(buf, len, font_dir, out);
    free(buf);
    return r;
}

void fontsys_xml_free(fontsys_xml_table *t) {
    int i;
    if (!t) return;
    for (i = 0; i < t->family_count; i++) {
        free(t->families[i].name);
        free(t->families[i].lang);
    }
    for (i = 0; i < t->font_count; i++) free(t->fonts[i].path);
    free(t->families);
    free(t->fonts);
    memset(t, 0, sizeof(*t));
}

/* ---- font choice ---- */

/* The parts of a BCP 47 tag that matter here, lower case. */
typedef struct {
    char lang[9];
    char script[5];
    char region[4];
} lang_tag;

static void parse_tag(const char *s, size_t n, lang_tag *t) {
    size_t i = 0;
    int first = 1;

    memset(t, 0, sizeof(*t));
    while (i < n) {
        size_t start = i, len, k;
        while (i < n && s[i] != '-' && s[i] != '_') i++;
        len = i - start;
        if (i < n) i++; /* the separator */
        if (first) {
            first = 0;
            if (len == 0 || len >= sizeof(t->lang)) return;
            for (k = 0; k < len; k++) t->lang[k] = (char)lower((unsigned char)s[start + k]);
        } else if (len == 1) {
            return; /* extensions and private use: not needed */
        } else if (len == 4 && !t->script[0] && !t->region[0]) {
            for (k = 0; k < 4; k++) t->script[k] = (char)lower((unsigned char)s[start + k]);
        } else if ((len == 2 || len == 3) && !t->region[0]) {
            for (k = 0; k < len; k++) t->region[k] = (char)lower((unsigned char)s[start + k]);
        }
    }
    /* Chinese without a script: the one its region writes in. */
    if (!strcmp(t->lang, "zh") && !t->script[0]) {
        if (!strcmp(t->region, "tw") || !strcmp(t->region, "hk") || !strcmp(t->region, "mo"))
            memcpy(t->script, "hant", 5);
        else
            memcpy(t->script, "hans", 5);
    }
}

/* Family ranks, tried in this order (see fontsys_xml_select). */
enum {
    RANK_GENERIC,      /* "sans-serif", or "monospace" for monospace */
    RANK_SANS_FOR_MONO,
    RANK_LANG_EXACT,
    RANK_LANG_PRIMARY,
    RANK_NEUTRAL,      /* no lang, or und-<script> */
    RANK_OTHER_LANG,
    RANK_OTHER_NAMED,
    RANK_COUNT
};

static int lang_rank(const lang_tag *want, const char *fam_tag, size_t n) {
    lang_tag have;
    parse_tag(fam_tag, n, &have);
    if (!have.lang[0] || !strcmp(have.lang, "und")) return RANK_NEUTRAL;
    if (!want->lang[0] || strcmp(want->lang, have.lang) != 0) return RANK_OTHER_LANG;
    if (want->script[0] && have.script[0])
        return strcmp(want->script, have.script) ? RANK_OTHER_LANG : RANK_LANG_EXACT;
    return (want->script[0] || have.script[0]) ? RANK_LANG_PRIMARY : RANK_LANG_EXACT;
}

static int family_rank(const fontsys_xml_family *f, const lang_tag *want, int mono) {
    const char *s = f->lang;
    int best = RANK_COUNT;

    if (f->name[0]) {
        if (!strcmp(f->name, mono ? "monospace" : "sans-serif")) return RANK_GENERIC;
        if (mono && !strcmp(f->name, "sans-serif")) return RANK_SANS_FOR_MONO;
        return RANK_OTHER_NAMED;
    }
    /* lang is a list: "zh-Hant,zh-Bopo" (or space separated); the best tag counts */
    while (*s) {
        size_t n;
        int r;
        while (*s == ',' || is_space(*s)) s++;
        n = 0;
        while (s[n] && s[n] != ',' && !is_space(s[n])) n++;
        if (n == 0) break;
        r = lang_rank(want, s, n);
        if (r < best) best = r;
        s += n;
    }
    return best == RANK_COUNT ? RANK_NEUTRAL : best;
}

/* Lower is better: the right slant first, then the closest weight; the serif
   form of a CJK family (fallbackFor) only when nothing else has it. */
static int style_score(const fontsys_xml_font *f, int weight, int italic) {
    int d = f->weight - weight;
    return (d < 0 ? -d : d) + ((f->italic != 0) != (italic != 0) ? 1000 : 0) +
           (f->serif_fallback ? 100000 : 0);
}

static int same_face(const fontsys_xml_font *a, const fontsys_xml_font *b) {
    return a->index == b->index && strcmp(a->path, b->path) == 0;
}

/* (score, position) order: 1 when font a comes before font b. */
static int before(int score_a, int a, int score_b, int b) {
    return score_a < score_b || (score_a == score_b && a < b);
}

/* The best font of family f that covers cp, or -1. Like Android, a family
   whose upright regular font lacks cp is skipped without opening the rest. */
static int pick_in_family(const fontsys_xml_table *t, const fontsys_xml_family *f, unsigned cp,
                          int weight, int italic, fontsys_xml_covers_fn covers, void *user) {
    int end = f->first_font + f->font_count;
    int rep = -1, rep_score = 0;
    int prev = -1, prev_score = 0;
    int i;

    for (i = f->first_font; i < end; i++) {
        int s = style_score(&t->fonts[i], 400, 0);
        if (rep < 0 || s < rep_score) {
            rep = i;
            rep_score = s;
        }
    }
    if (rep < 0 || covers(user, &t->fonts[rep], cp) != 1) return -1;

    /* the candidates in style order; each face is checked once */
    for (;;) {
        int next = -1, next_score = 0, j, tried = 0;
        for (i = f->first_font; i < end; i++) {
            int s = style_score(&t->fonts[i], weight, italic);
            if (prev >= 0 && !before(prev_score, prev, s, i)) continue;
            if (next < 0 || before(s, i, next_score, next)) {
                next = i;
                next_score = s;
            }
        }
        if (next < 0) return -1; /* not reached: rep covers */
        if (same_face(&t->fonts[next], &t->fonts[rep])) return next;
        /* a face already tried (and failed) under an earlier candidate */
        for (j = f->first_font; j < end && !tried; j++) {
            int s = style_score(&t->fonts[j], weight, italic);
            if (before(s, j, next_score, next) && same_face(&t->fonts[j], &t->fonts[next]))
                tried = 1;
        }
        if (!tried && covers(user, &t->fonts[next], cp) == 1) return next;
        prev = next;
        prev_score = next_score;
    }
}

int fontsys_xml_select(const fontsys_xml_table *t, unsigned cp, const char *lang,
                       const fontsys_style *style, fontsys_xml_covers_fn covers, void *user) {
    lang_tag want;
    int weight, rank, i;

    if (!t || !style || !covers) return -1;
    if (!lang) lang = "";
    parse_tag(lang, strlen(lang), &want);
    weight = style->weight < 1 ? 400 : style->weight > 1000 ? 1000 : style->weight;
    for (rank = 0; rank < RANK_COUNT; rank++) {
        for (i = 0; i < t->family_count; i++) {
            const fontsys_xml_family *f = &t->families[i];
            int r;
            if (f->font_count == 0 || family_rank(f, &want, style->monospace) != rank) continue;
            r = pick_in_family(t, f, cp, weight, style->italic, covers, user);
            if (r >= 0) return r;
        }
    }
    return -1;
}
