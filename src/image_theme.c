/* A theme's colours from the colours of an image (see image_theme.h). */
#include "image_theme.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Colour spaces: sRGB, OKLab and OKLCH (L 0..1, C 0..~0.37, h in degrees)   */
/* ------------------------------------------------------------------------- */

typedef struct {
    gdouble l, a, b;
} Lab;

typedef struct {
    gdouble l, c, h;
} Lch;

static gdouble to_linear(gdouble c) {
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static gdouble from_linear(gdouble c) {
    return (c <= 0.0031308) ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

static Lab lab_from_rgb8(guint8 r8, guint8 g8, guint8 b8) {
    gdouble r = to_linear(r8 / 255.0), g = to_linear(g8 / 255.0), b = to_linear(b8 / 255.0);
    gdouble l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    gdouble m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    gdouble s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return (Lab){0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

/* Linear RGB of an OKLCH colour; FALSE when outside sRGB */
static gboolean lch_to_linear(Lch c, gdouble rgb[3]) {
    gdouble a = c.c * cos(c.h * G_PI / 180.0), b = c.c * sin(c.h * G_PI / 180.0);
    gdouble l = c.l + 0.3963377774 * a + 0.2158037573 * b;
    gdouble m = c.l - 0.1055613458 * a - 0.0638541728 * b;
    gdouble s = c.l - 0.0894841775 * a - 1.2914855480 * b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    rgb[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    rgb[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    rgb[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    const gdouble eps = 1e-6;
    for (gint i = 0; i < 3; i++) {
        if (rgb[i] < -eps || rgb[i] > 1.0 + eps) {
            return FALSE;
        }
    }
    return TRUE;
}

/* "#rrggbb" of an OKLCH colour; chroma reduced until it fits in sRGB */
static void lch_to_hex(Lch c, gchar out[8]) {
    c.l = CLAMP(c.l, 0.0, 1.0);
    c.c = MAX(c.c, 0.0);
    gdouble rgb[3];
    if (!lch_to_linear(c, rgb)) {
        gdouble lo = 0.0, hi = c.c;
        for (gint i = 0; i < 24; i++) {
            Lch t = {c.l, (lo + hi) / 2.0, c.h};
            if (lch_to_linear(t, rgb)) {
                lo = t.c;
            } else {
                hi = t.c;
            }
        }
        c.c = lo;
        lch_to_linear(c, rgb);
    }
    guint v[3];
    for (gint i = 0; i < 3; i++) {
        v[i] = (guint)lround(CLAMP(from_linear(CLAMP(rgb[i], 0.0, 1.0)), 0.0, 1.0) * 255.0);
    }
    g_snprintf(out, 8, "#%02x%02x%02x", v[0], v[1], v[2]);
}

static gdouble hue_diff(gdouble a, gdouble b) {
    return fmod(a - b + 540.0, 360.0) - 180.0;
}

static gdouble luminance_hex(const gchar* hex) {
    gchar norm[8];
    if (!palette_parse_color(hex, norm)) {
        return -1.0;
    }
    guint v = (guint)strtoul(norm + 1, NULL, 16);
    return 0.2126 * to_linear(((v >> 16) & 0xff) / 255.0) + 0.7152 * to_linear(((v >> 8) & 0xff) / 255.0) +
           0.0722 * to_linear((v & 0xff) / 255.0);
}

gdouble image_theme_contrast(const gchar* a, const gchar* b) {
    gdouble la = luminance_hex(a), lb = luminance_hex(b);
    if (la < 0.0 || lb < 0.0) {
        return 0.0;
    }
    return (MAX(la, lb) + 0.05) / (MIN(la, lb) + 0.05);
}

/* ------------------------------------------------------------------------- */
/* Main colours of the image: weighted k-means over a colour histogram       */
/* ------------------------------------------------------------------------- */

#define BIN_BITS 4
#define BIN_COUNT (1 << (3 * BIN_BITS))
#define CLUSTERS 8
#define MAX_SAMPLES 65536

typedef struct {
    Lab lab;
    gdouble weight; /* share of the pixels, 0..1 */
    Lch lch;
} Cluster;

static gdouble lab_dist2(Lab x, Lab y) {
    gdouble dl = x.l - y.l, da = x.a - y.a, db = x.b - y.b;
    return dl * dl + da * da + db * db;
}

/* Clusters by decreasing weight; their count, 0 without opaque pixels */
static gint find_clusters(const guint8* pixels, gint width, gint height, gint rowstride, gint channels,
                          Cluster out[CLUSTERS]) {
    Lab* sum = g_new0(Lab, BIN_COUNT);
    gdouble* count = g_new0(gdouble, BIN_COUNT);
    gint64 total_px = (gint64)width * height;
    gint step = MAX(1, (gint)ceil(sqrt((gdouble)total_px / MAX_SAMPLES)));
    gdouble total = 0.0;
    for (gint y = 0; y < height; y += step) {
        const guint8* row = pixels + (gsize)y * rowstride;
        for (gint x = 0; x < width; x += step) {
            const guint8* p = row + (gsize)x * channels;
            if (channels == 4 && p[3] < 128) {
                continue;
            }
            gint bin = ((p[0] >> (8 - BIN_BITS)) << (2 * BIN_BITS)) | ((p[1] >> (8 - BIN_BITS)) << BIN_BITS) |
                       (p[2] >> (8 - BIN_BITS));
            Lab lab = lab_from_rgb8(p[0], p[1], p[2]);
            sum[bin].l += lab.l;
            sum[bin].a += lab.a;
            sum[bin].b += lab.b;
            count[bin] += 1.0;
            total += 1.0;
        }
    }
    gint n_points = 0;
    Lab* point = g_new(Lab, BIN_COUNT);
    gdouble* weight = g_new(gdouble, BIN_COUNT);
    for (gint i = 0; i < BIN_COUNT; i++) {
        if (count[i] > 0.0) {
            point[n_points] = (Lab){sum[i].l / count[i], sum[i].a / count[i], sum[i].b / count[i]};
            weight[n_points] = count[i] / total;
            n_points++;
        }
    }
    g_free(sum);
    g_free(count);

    /* Seeds: the heaviest colour, then the colours far from the seeds and
     * frequent (deterministic k-means++) */
    Lab center[CLUSTERS];
    gint k = 0;
    gdouble* dmin = g_new(gdouble, MAX(n_points, 1));
    for (gint i = 0; i < n_points; i++) {
        dmin[i] = G_MAXDOUBLE;
    }
    while (k < CLUSTERS && k < n_points) {
        gint best = -1;
        gdouble best_score = 0.0;
        for (gint i = 0; i < n_points; i++) {
            gdouble score = (k == 0) ? weight[i] : weight[i] * dmin[i];
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        center[k++] = point[best];
        for (gint i = 0; i < n_points; i++) {
            dmin[i] = MIN(dmin[i], lab_dist2(point[i], point[best]));
        }
    }
    g_free(dmin);

    gint* owner = g_new0(gint, MAX(n_points, 1));
    gdouble cw[CLUSTERS] = {0};
    for (gint iter = 0; iter < 16 && k > 0; iter++) {
        Lab acc[CLUSTERS] = {{0}};
        memset(cw, 0, sizeof cw);
        for (gint i = 0; i < n_points; i++) {
            gint best = 0;
            gdouble best_d = G_MAXDOUBLE;
            for (gint c = 0; c < k; c++) {
                gdouble d = lab_dist2(point[i], center[c]);
                if (d < best_d) {
                    best_d = d;
                    best = c;
                }
            }
            owner[i] = best;
            acc[best].l += point[i].l * weight[i];
            acc[best].a += point[i].a * weight[i];
            acc[best].b += point[i].b * weight[i];
            cw[best] += weight[i];
        }
        for (gint c = 0; c < k; c++) {
            if (cw[c] > 0.0) {
                center[c] = (Lab){acc[c].l / cw[c], acc[c].a / cw[c], acc[c].b / cw[c]};
            }
        }
    }
    g_free(owner);
    g_free(point);
    g_free(weight);

    gint n = 0;
    for (gint c = 0; c < k; c++) {
        if (cw[c] <= 0.0) {
            continue;
        }
        Cluster cl = {center[c], cw[c],
                      {center[c].l, hypot(center[c].a, center[c].b), atan2(center[c].b, center[c].a) * 180.0 / G_PI}};
        if (cl.lch.h < 0.0) {
            cl.lch.h += 360.0;
        }
        gint at = n++;
        while (at > 0 && out[at - 1].weight < cl.weight) {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = cl;
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* Roles                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    ImageTheme* out;
    const Cluster* cl;
    gint n;
    gboolean dark;
    gdouble bg_l;   /* lightness of bg_view */
    gdouble hue;    /* hue of the surfaces */
    gdouble tint;   /* chroma of the surfaces */
    gdouble acc_h;  /* accent */
    gdouble acc_c;
} Ctx;

static void set(Ctx* x, PaletteRole role, gdouble l, gdouble c, gdouble h) {
    lch_to_hex((Lch){l, c, h}, x->out->colors[role]);
}

/* Lightness moved away from bg_view until the contrast reaches target */
static void set_readable(Ctx* x, PaletteRole role, gdouble l, gdouble c, gdouble h, gdouble target) {
    const gchar* bg = x->out->colors[PAL_BG_VIEW];
    gdouble dir = x->dark ? 0.01 : -0.01;
    set(x, role, l, c, h);
    while (image_theme_contrast(x->out->colors[role], bg) < target && l > 0.0 && l < 1.0) {
        l += dir;
        set(x, role, l, c, h);
    }
}

/* A surface: bg_view lightness moved by delta (toward the text) */
static void surface(Ctx* x, PaletteRole role, gdouble delta) {
    set(x, role, x->bg_l + (x->dark ? delta : -delta), x->tint, x->hue);
}

/* Hue of a meaning (errors are red...), pulled toward a near hue of the image */
static gdouble snap_hue(const Ctx* x, gdouble h) {
    gdouble best = 26.0;
    gdouble pull = 0.0;
    for (gint i = 0; i < x->n; i++) {
        gdouble d = hue_diff(x->cl[i].lch.h, h);
        if (x->cl[i].lch.c >= 0.05 && fabs(d) < best) {
            best = fabs(d);
            pull = d;
        }
    }
    return fmod(h + 0.6 * pull + 360.0, 360.0);
}

/* Colour of a meaning: lightness for a dark and a light theme */
static void meaning(Ctx* x, PaletteRole role, gdouble h, gdouble l_dark, gdouble l_light, gdouble c) {
    set(x, role, x->dark ? l_dark : l_light, c, snap_hue(x, h));
}

static void meaning_bg(Ctx* x, PaletteRole role, gdouble h, gdouble delta, gdouble c) {
    set(x, role, x->bg_l + (x->dark ? delta : -delta), c, snap_hue(x, h));
}

static void surfaces_and_text(Ctx* x) {
    surface(x, PAL_BG_VIEW, 0.0);
    surface(x, PAL_BG_BAR, 0.025);
    surface(x, PAL_BG_WINDOW, 0.03);
    surface(x, PAL_BG_TERMINAL, 0.045);
    surface(x, PAL_BG_BLOCK, 0.035);
    surface(x, PAL_BG_CODE_INLINE, 0.07);
    surface(x, PAL_BG_HOVER, 0.08);
    surface(x, PAL_BG_SELECTED_INACTIVE, 0.12);
    surface(x, PAL_BORDER, 0.06);
    surface(x, PAL_BORDER_STRONG, 0.14);
    surface(x, PAL_RULE, 0.10);
    surface(x, PAL_HOVER_OVERLAY, 0.25);
    surface(x, PAL_SCROLLBAR, 0.40);
    surface(x, PAL_EDITOR_CURRENT_LINE, 0.025);
    surface(x, PAL_EDITOR_BRACKET, 0.18);
    surface(x, PAL_EDITOR_OCCURRENCE, 0.10);
    /* Menus and entries stand out from a dark view; light ones are white-ish */
    if (x->dark) {
        surface(x, PAL_BG_RAISED, 0.075);
        surface(x, PAL_BG_INPUT, 0.11);
    } else {
        set(x, PAL_BG_RAISED, MIN(1.0, x->bg_l + 0.02), x->tint, x->hue);
        set(x, PAL_BG_INPUT, MIN(1.0, x->bg_l + 0.02), x->tint, x->hue);
    }
    set(x, PAL_SHADOW, 0.0, 0.0, 0.0);

    gdouble c = MIN(x->tint, 0.015);
    gboolean d = x->dark;
    set_readable(x, PAL_FG, d ? 0.86 : 0.33, c, x->hue, 7.0);
    set_readable(x, PAL_FG_TEXT, d ? 0.88 : 0.25, c, x->hue, 7.0);
    set_readable(x, PAL_FG_STRONG, d ? 0.93 : 0.18, c, x->hue, 7.0);
    set_readable(x, PAL_FG_BRIGHT, d ? 0.99 : 0.10, 0.0, x->hue, 7.0);
    set_readable(x, PAL_FG_MUTED, d ? 0.72 : 0.45, c, x->hue, 4.5);
    set_readable(x, PAL_FG_SUBTLE, d ? 0.65 : 0.52, c, x->hue, 3.5);
    set_readable(x, PAL_FG_FAINT, d ? 0.58 : 0.58, c, x->hue, 2.5);
    set_readable(x, PAL_SYN_COMMENT, d ? 0.62 : 0.52, MAX(x->tint, 0.04), x->hue, 3.0);
    memcpy(x->out->colors[PAL_EDITOR_CURSOR], x->out->colors[PAL_FG], 8);
    memcpy(x->out->colors[PAL_TERM_FG], x->out->colors[PAL_FG], 8);
    memcpy(x->out->colors[PAL_TERM_CURSOR], x->out->colors[PAL_FG_BRIGHT], 8);
}

/* Text on a fill: white or near black, the one that reads better */
static void text_on(Ctx* x, PaletteRole role, PaletteRole fill) {
    gchar light[8], dark[8];
    lch_to_hex((Lch){0.99, 0.0, 0.0}, light);
    lch_to_hex((Lch){0.16, 0.0, 0.0}, dark);
    const gchar* f = x->out->colors[fill];
    memcpy(x->out->colors[role], image_theme_contrast(light, f) >= image_theme_contrast(dark, f) ? light : dark, 8);
}

static void accent(Ctx* x) {
    gdouble best = 0.0;
    gint found = -1;
    for (gint i = 0; i < x->n; i++) {
        gdouble score = x->cl[i].lch.c * (0.25 + sqrt(x->cl[i].weight));
        if (x->cl[i].lch.c >= 0.04 && score > best) {
            best = score;
            found = i;
        }
    }
    if (found >= 0) {
        x->acc_h = x->cl[found].lch.h;
        x->acc_c = CLAMP(x->cl[found].lch.c, 0.10, 0.20);
    } else {
        /* A grey image: the tint of the surfaces, or blue */
        x->acc_h = (x->tint >= 0.01) ? x->hue : 255.0;
        x->acc_c = 0.13;
    }
    gboolean d = x->dark;
    gdouble h = x->acc_h, c = x->acc_c;
    set(x, PAL_ACCENT, d ? 0.58 : 0.55, c, h);
    set(x, PAL_ACCENT_HOVER, d ? 0.54 : 0.50, c, h);
    set(x, PAL_FOCUS_RING, d ? 0.62 : 0.55, c, h);
    set(x, PAL_GUTTER_MODIFIED, d ? 0.58 : 0.55, c, h);
    set_readable(x, PAL_ACCENT_FG, d ? 0.78 : 0.48, c * 0.8, h, 4.5);
    set_readable(x, PAL_LINK, d ? 0.72 : 0.50, c, h, 4.5);
    text_on(x, PAL_ACCENT_TEXT, PAL_ACCENT);
    set(x, PAL_BG_SELECTED, x->bg_l + (d ? 0.12 : -0.10), c * (d ? 0.45 : 0.35), h);
    set(x, PAL_EDITOR_SELECTION, x->bg_l + (d ? 0.15 : -0.13), c * (d ? 0.4 : 0.3), h);
}

/* Syntax: the distinct hues of the image (accent first), then hues turned
 * from the last one for the roles left */
static void syntax(Ctx* x) {
    static const PaletteRole ROLES[] = {PAL_SYN_KEYWORD,  PAL_SYN_STRING,   PAL_SYN_FUNCTION,
                                        PAL_SYN_TYPE,     PAL_SYN_CONTROL,  PAL_SYN_VARIABLE,
                                        PAL_SYN_NUMBER,   PAL_SYN_ESCAPE,   PAL_SYN_CONSTANT};
    static const gdouble L_DARK[] = {0.70, 0.76, 0.86, 0.78, 0.72, 0.84, 0.82, 0.80, 0.74};
    gdouble hues[G_N_ELEMENTS(ROLES)];
    gsize n = 0;
    hues[n++] = x->acc_h;
    for (gint i = 0; i < x->n && n < G_N_ELEMENTS(ROLES); i++) {
        if (x->cl[i].lch.c < 0.04) {
            continue;
        }
        gboolean distinct = TRUE;
        for (gsize j = 0; j < n; j++) {
            distinct = distinct && fabs(hue_diff(x->cl[i].lch.h, hues[j])) >= 30.0;
        }
        if (distinct) {
            hues[n++] = x->cl[i].lch.h;
        }
    }
    for (gsize i = n; i < G_N_ELEMENTS(ROLES); i++) {
        hues[i] = fmod(hues[i - 1] + 137.5, 360.0);
    }
    for (gsize i = 0; i < G_N_ELEMENTS(ROLES); i++) {
        gdouble l = x->dark ? L_DARK[i] : 0.95 - L_DARK[i] * 0.6;
        set_readable(x, ROLES[i], l, x->dark ? 0.12 : 0.14, hues[i], 4.5);
    }
}

/* OKLCH hues of colours with a meaning */
#define H_RED 27.0
#define H_ORANGE 55.0
#define H_AMBER 80.0
#define H_YELLOW 100.0
#define H_GREEN 145.0
#define H_CYAN 200.0
#define H_BLUE 255.0
#define H_PURPLE 305.0
#define H_MAGENTA 330.0
#define H_PINK 355.0

static void meanings(Ctx* x) {
    meaning(x, PAL_ERROR, H_RED, 0.66, 0.55, 0.19);
    memcpy(x->out->colors[PAL_SYN_ERROR], x->out->colors[PAL_ERROR], 8);
    memcpy(x->out->colors[PAL_EDITOR_RULER], x->out->colors[PAL_ERROR], 8);
    meaning(x, PAL_WARNING, H_AMBER, 0.75, 0.60, 0.15);
    meaning(x, PAL_SUCCESS, H_GREEN, 0.78, 0.55, 0.14);
    meaning(x, PAL_HIGHLIGHT, H_YELLOW, 0.82, 0.86, 0.16);
    text_on(x, PAL_HIGHLIGHT_FG, PAL_HIGHLIGHT);

    meaning(x, PAL_BADGE_RED, H_RED, 0.55, 0.55, 0.16);
    meaning(x, PAL_BADGE_GREEN, H_GREEN, 0.52, 0.52, 0.12);
    meaning(x, PAL_BADGE_BLUE, H_BLUE, 0.50, 0.50, 0.12);
    meaning(x, PAL_BADGE_ORANGE, H_AMBER, 0.62, 0.62, 0.13);
    text_on(x, PAL_BADGE_FG, PAL_BADGE_BLUE);

    meaning(x, PAL_GIT_MODIFIED, H_AMBER, 0.82, 0.55, 0.10);
    meaning(x, PAL_GIT_ADDED, H_GREEN, 0.76, 0.55, 0.13);
    meaning(x, PAL_GIT_DELETED, H_RED, 0.62, 0.52, 0.16);
    meaning(x, PAL_GIT_CONFLICT, H_RED, 0.58, 0.50, 0.22);
    meaning(x, PAL_GIT_RENAMED, H_CYAN, 0.78, 0.55, 0.12);

    meaning(x, PAL_ICON_BLUE, H_BLUE - 20.0, 0.65, 0.58, 0.09);
    meaning(x, PAL_ICON_GREEN, H_GREEN - 15.0, 0.75, 0.62, 0.16);
    meaning(x, PAL_ICON_ORANGE, H_ORANGE, 0.67, 0.62, 0.16);
    meaning(x, PAL_ICON_PINK, H_PINK, 0.66, 0.60, 0.20);
    meaning(x, PAL_ICON_PURPLE, H_PURPLE, 0.62, 0.55, 0.13);
    meaning(x, PAL_ICON_RED, H_RED, 0.57, 0.55, 0.17);
    meaning(x, PAL_ICON_YELLOW, H_YELLOW + 5.0, 0.80, 0.68, 0.16);
    set(x, PAL_ICON_WHITE, x->dark ? 0.87 : 0.45, 0.005, x->hue);
    set(x, PAL_ICON_GREY, x->dark ? 0.43 : 0.50, 0.02, x->hue);
    set(x, PAL_ICON_GREY_LIGHT, x->dark ? 0.55 : 0.58, 0.025, x->hue);
    set(x, PAL_ICON_IGNORE, x->dark ? 0.42 : 0.65, 0.03, x->hue);

    meaning(x, PAL_GUTTER_ADDED, H_GREEN, 0.62, 0.60, 0.15);
    meaning(x, PAL_GUTTER_DELETED, H_RED, 0.63, 0.60, 0.19);
    meaning(x, PAL_DIFF_ADDED_FG, H_GREEN, 0.82, 0.40, 0.08);
    meaning(x, PAL_DIFF_REMOVED_FG, H_RED, 0.80, 0.42, 0.08);
    meaning(x, PAL_DIFF_HUNK_FG, H_CYAN - 15.0, 0.76, 0.48, 0.10);
    meaning_bg(x, PAL_DIFF_ADDED_BG, H_GREEN, 0.05, 0.04);
    meaning_bg(x, PAL_DIFF_REMOVED_BG, H_RED, 0.05, 0.04);
    meaning_bg(x, PAL_DIFF_HUNK_BG, H_BLUE - 25.0, 0.04, 0.03);
    meaning_bg(x, PAL_EDITOR_MATCH, H_ORANGE, 0.15, 0.07);
    meaning(x, PAL_EDITOR_MATCH_CURRENT, H_ORANGE, 0.50, 0.78, 0.12);
}

/* The 16 terminal colours: normal then bright, each with its hue */
static void terminal(Ctx* x) {
    static const gdouble HUES[] = {0.0, H_RED, H_GREEN, H_YELLOW, H_BLUE, H_MAGENTA, H_CYAN + 10.0, 0.0};
    static const gdouble L_DARK[] = {0.0, 0.60, 0.70, 0.87, 0.55, 0.58, 0.68, 0.91};
    static const gdouble L_LIGHT[] = {0.0, 0.52, 0.55, 0.60, 0.48, 0.50, 0.55, 0.75};
    static const gdouble CHROMA[] = {0.0, 0.19, 0.16, 0.17, 0.16, 0.19, 0.12, 0.0};
    for (gint bright = 0; bright < 2; bright++) {
        for (gint i = 0; i < 8; i++) {
            PaletteRole role = (PaletteRole)(PAL_ANSI_BLACK + bright * 8 + i);
            gdouble l = x->dark ? L_DARK[i] : L_LIGHT[i];
            if (bright) {
                l = (i == 0) ? 0.50 : (i == 7) ? 0.98 : MIN(1.0, l + 0.08);
            }
            gdouble h = (CHROMA[i] > 0.0) ? snap_hue(x, HUES[i]) : x->hue;
            set(x, role, l, CHROMA[i], h);
        }
    }
}

gboolean image_theme_derive(const guint8* pixels, gint width, gint height, gint rowstride, gint channels,
                            ImageTheme* out) {
    g_return_val_if_fail(out != NULL, FALSE);
    if (pixels == NULL || width <= 0 || height <= 0 || (channels != 3 && channels != 4) ||
        rowstride < width * channels) {
        return FALSE;
    }
    Cluster cl[CLUSTERS];
    gint n = find_clusters(pixels, width, height, rowstride, channels, cl);
    if (n == 0) {
        return FALSE;
    }
    memset(out, 0, sizeof *out);
    gdouble mean_l = 0.0;
    for (gint i = 0; i < n; i++) {
        mean_l += cl[i].lch.l * cl[i].weight;
    }
    gboolean dark = mean_l < 0.62;
    /* A tint shows more on light surfaces */
    Ctx x = {out, cl, n, dark, 0.0, cl[0].lch.h, MIN(cl[0].lch.c * 0.35, dark ? 0.035 : 0.018), 0.0, 0.0};
    /* The view takes the lightness of the main colour when it suits a background */
    if (x.dark) {
        x.bg_l = (cl[0].lch.l < 0.45) ? CLAMP(cl[0].lch.l, 0.16, 0.27) : 0.20;
    } else {
        x.bg_l = (cl[0].lch.l > 0.75) ? CLAMP(cl[0].lch.l, 0.94, 0.985) : 0.97;
    }
    out->dark = x.dark;
    surfaces_and_text(&x);
    accent(&x);
    syntax(&x);
    meanings(&x);
    terminal(&x);
    return TRUE;
}
