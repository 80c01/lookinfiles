/* TeX formulas laid out and drawn as paintables (see math_paintable.h). */
#include "markup/math_paintable.h"
#include "markup/tex_math.h"
#include "theme/fonts.h"
#include "theme/palette.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

/* --- Boxes: drawing operations around a baseline (y = 0, down positive) --- */

typedef enum {
    OP_TEXT, /* layout with its baseline at (x, y), scaled by sx, sy from there */
    OP_LINE, /* from (x, y) to (x2, y2), width wide */
} OpKind;

typedef struct {
    OpKind kind;
    PangoLayout* layout;
    gdouble baseline; /* OP_TEXT: of the layout, from its top */
    gdouble x, y, x2, y2;
    gdouble sx, sy;
    gdouble wide;
} Op;

typedef struct {
    GArray* ops;
    gdouble w, asc, desc;
} Box;

static void op_clear(gpointer data) {
    Op* op = data;
    g_clear_object(&op->layout);
}

static Box* box_new(void) {
    Box* b = g_new0(Box, 1);
    b->ops = g_array_new(FALSE, TRUE, sizeof(Op));
    g_array_set_clear_func(b->ops, op_clear);
    return b;
}

static void box_free(Box* b) {
    if (b == NULL) return;
    g_array_unref(b->ops);
    g_free(b);
}

/* Move the operations of src (freed) into dst, shifted by dx, dy */
static void box_put(Box* dst, Box* src, gdouble dx, gdouble dy) {
    for (guint i = 0; i < src->ops->len; i++) {
        Op op = g_array_index(src->ops, Op, i);
        op.x += dx;
        op.y += dy;
        op.x2 += dx;
        op.y2 += dy;
        if (op.layout != NULL) g_object_ref(op.layout);
        g_array_append_val(dst->ops, op);
    }
    box_free(src);
}

static void box_line(Box* b, gdouble x, gdouble y, gdouble x2, gdouble y2, gdouble wide) {
    Op op = {0};
    op.kind = OP_LINE;
    op.x = x;
    op.y = y;
    op.x2 = x2;
    op.y2 = y2;
    op.wide = wide;
    g_array_append_val(b->ops, op);
}

/* --- Layout context --- */

typedef struct {
    PangoContext* pc;
    gdouble base;    /* text size in pixels */
    gint level;      /* 0 text, 1 script, 2 scriptscript */
    gboolean display;
    gdouble stretch; /* height of the \middle delimiters of the enclosing \left \right, or 0 */
} Ctx;

static gdouble ctx_size(const Ctx* c) {
    /* TeX's 0.7 and 0.5, a little larger: legible at interface sizes */
    return c->base * (c->level <= 0 ? 1.0 : c->level == 1 ? 0.75 : 0.6);
}

static Ctx ctx_script(const Ctx* c) {
    Ctx s = *c;
    s.level = MIN(c->level + 1, 2);
    s.display = FALSE;
    s.stretch = 0;
    return s;
}

/* The math axis (fraction bars, centre of delimiters) above the baseline */
static gdouble axis(const Ctx* c) {
    return 0.25 * ctx_size(c);
}

/* Rule thickness */
static gdouble rule(const Ctx* c) {
    return MAX(1.0, 0.055 * ctx_size(c));
}

static Box* layout_node(const Ctx* c, const TexNode* n);

/* --- Text --- */

static PangoLayout* text_layout(const Ctx* c, const gchar* text, guint font, gdouble size) {
    PangoLayout* l = pango_layout_new(c->pc);
    PangoFontDescription* fd = pango_font_description_new();
    pango_font_description_set_family(fd, (font & TEX_FONT_MONO) ? fonts_family(FONT_CODE) : fonts_math_family());
    pango_font_description_set_absolute_size(fd, MAX(size, 1.0) * PANGO_SCALE);
    if (font & TEX_FONT_ITALIC) pango_font_description_set_style(fd, PANGO_STYLE_ITALIC);
    if (font & TEX_FONT_BOLD) pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, text, -1);
    return l;
}

/* Ink of a layout around its baseline: top (negative up), bottom, left, right */
typedef struct {
    gdouble top, bottom, left, right, advance, baseline;
} Ink;

static Ink layout_ink(PangoLayout* l) {
    PangoRectangle ink, logical;
    pango_layout_get_extents(l, &ink, &logical);
    Ink r;
    r.baseline = (gdouble)pango_layout_get_baseline(l) / PANGO_SCALE;
    r.advance = (gdouble)logical.width / PANGO_SCALE;
    if (ink.width <= 0 || ink.height <= 0) {
        r.top = r.bottom = 0;
        r.left = 0;
        r.right = r.advance;
    } else {
        r.top = (gdouble)ink.y / PANGO_SCALE - r.baseline;
        r.bottom = (gdouble)(ink.y + ink.height) / PANGO_SCALE - r.baseline;
        r.left = (gdouble)(ink.x - logical.x) / PANGO_SCALE;
        r.right = (gdouble)(ink.x + ink.width - logical.x) / PANGO_SCALE;
    }
    return r;
}

/* Text on the baseline; italic letters keep their overhang (italic correction) */
static Box* text_box(const Ctx* c, const gchar* text, guint font, gdouble size) {
    Box* b = box_new();
    PangoLayout* l = text_layout(c, text, font, size);
    Ink ink = layout_ink(l);
    Op op = {0};
    op.kind = OP_TEXT;
    op.layout = l;
    op.baseline = ink.baseline;
    op.sx = op.sy = 1.0;
    g_array_append_val(b->ops, op);
    b->w = MAX(ink.advance, ink.right);
    b->asc = MAX(0.0, -ink.top);
    b->desc = MAX(0.0, ink.bottom);
    return b;
}

/* Text whose ink is centred on the axis, stretched to height (0: natural) */
static Box* centred_box(const Ctx* c, const gchar* text, gdouble size, gdouble height) {
    Box* b = box_new();
    if (text == NULL || text[0] == '\0') {
        b->w = 0.1 * ctx_size(c);
        return b;
    }
    PangoLayout* l = text_layout(c, text, 0, size);
    Ink ink = layout_ink(l);
    gdouble h = ink.bottom - ink.top;
    gdouble sy = (height > 0 && h > 0 && height > h * 1.05) ? height / h : 1.0;
    gdouble sx = MIN(1.0 + (sy - 1.0) * 0.18, 1.8);
    gdouble centre = (ink.top + ink.bottom) / 2.0;
    Op op = {0};
    op.kind = OP_TEXT;
    op.layout = l;
    op.baseline = ink.baseline;
    op.sx = sx;
    op.sy = sy;
    op.y = -axis(c) - sy * centre;
    g_array_append_val(b->ops, op);
    b->w = sx * MAX(ink.advance, ink.right);
    b->asc = MAX(0.0, -(op.y + sy * ink.top));
    b->desc = MAX(0.0, op.y + sy * ink.bottom);
    return b;
}

static gboolean is_integral(const gchar* text) {
    return text != NULL && (strstr(text, "∫") != NULL || strstr(text, "∮") != NULL || strstr(text, "∬") != NULL ||
                            strstr(text, "∭") != NULL || strstr(text, "∯") != NULL);
}

static Box* atom_box(const Ctx* c, const TexNode* n) {
    gdouble s = ctx_size(c);
    if (n->delim) {
        static const gdouble BIG[] = {1.2, 1.8, 2.4, 3.0};
        gdouble height = n->big > 0 ? s * BIG[CLAMP(n->big, 1, 4) - 1] : c->stretch;
        return centred_box(c, n->text, s, height);
    }
    if (n->big) {
        /* Integrals are short in text fonts: stretched to their TeX height */
        if (is_integral(n->text)) return centred_box(c, n->text, s * (c->display ? 1.4 : 1.1), s * (c->display ? 2.2 : 1.35));
        return centred_box(c, n->text, s * (c->display ? 1.45 : 1.1), 0);
    }
    return text_box(c, n->text, n->font, s);
}

/* --- Rows --- */

/* Space between two classes in mu (1/18 em); negative: not in scripts */
static gint mu_space(TexClass l, TexClass r) {
    static const gint8 TABLE[8][8] = {
        /*          ORD  OP  BIN  REL OPEN CLOSE PUNCT INNER */
        /* ORD   */ {0, 3, -4, -5, 0, 0, 0, -3},
        /* OP    */ {3, 3, 0, -5, 0, 0, 0, -3},
        /* BIN   */ {-4, -4, 0, 0, -4, 0, 0, -4},
        /* REL   */ {-5, -5, 0, 0, -5, 0, 0, -5},
        /* OPEN  */ {0, 0, 0, 0, 0, 0, 0, 0},
        /* CLOSE */ {0, 3, -4, -5, 0, 0, 0, -3},
        /* PUNCT */ {-3, -3, 0, -3, -3, -3, -3, -3},
        /* INNER */ {-3, 3, -4, -5, -3, 0, -3, -3},
    };
    return TABLE[l][r];
}

/* A row; after_ord: as if an ordinary atom came before (an aligned
 * column after &, so that "&= b" spaces its relation) */
static Box* row_box(const Ctx* c, GPtrArray* items, gboolean after_ord) {
    Box* b = box_new();
    guint n = items != NULL ? items->len : 0;
    TexClass* cls = g_new(TexClass, n + 1);
    /* Classes, binary operators without operands made ordinary */
    gint prev = after_ord ? TEX_ORD : -1;
    for (guint i = 0; i < n; i++) {
        const TexNode* item = g_ptr_array_index(items, i);
        if (item->kind == TEX_SPACE) {
            cls[i] = TEX_ORD;
            continue;
        }
        cls[i] = tex_node_class(item);
        if (cls[i] == TEX_BIN && (prev < 0 || prev == TEX_BIN || prev == TEX_OP || prev == TEX_REL ||
                                  prev == TEX_OPEN || prev == TEX_PUNCT)) {
            cls[i] = TEX_ORD;
        }
        if ((cls[i] == TEX_REL || cls[i] == TEX_CLOSE || cls[i] == TEX_PUNCT) && prev == TEX_BIN) {
            /* the previous binary operator had no right operand */
            for (gint j = (gint)i - 1; j >= 0; j--) {
                if (((const TexNode*)g_ptr_array_index(items, j))->kind == TEX_SPACE) continue;
                cls[j] = TEX_ORD;
                break;
            }
        }
        prev = cls[i];
    }
    /* A binary operator at the end has no right operand */
    for (gint j = (gint)n - 1; j >= 0; j--) {
        if (((const TexNode*)g_ptr_array_index(items, j))->kind == TEX_SPACE) continue;
        if (cls[j] == TEX_BIN) cls[j] = TEX_ORD;
        break;
    }

    gdouble x = 0;
    gdouble mu = ctx_size(c) / 18.0;
    prev = after_ord ? TEX_ORD : -1;
    for (guint i = 0; i < n; i++) {
        const TexNode* item = g_ptr_array_index(items, i);
        if (item->kind != TEX_SPACE && prev >= 0) {
            gint space = mu_space((TexClass)prev, cls[i]);
            if (space < 0) space = c->level > 0 ? 0 : -space;
            x += space * mu;
        }
        Box* child = layout_node(c, item);
        gdouble w = child->w;
        b->asc = MAX(b->asc, child->asc);
        b->desc = MAX(b->desc, child->desc);
        box_put(b, child, x, 0);
        x += w;
        if (item->kind != TEX_SPACE) prev = cls[i];
    }
    b->w = MAX(x, 0.0);
    g_free(cls);
    return b;
}

/* --- Structures --- */

static Box* frac_box(const Ctx* c, const TexNode* n) {
    gboolean display = n->style == TEX_FRAC_DISPLAY || (n->style != TEX_FRAC_TEXT && c->display);
    Ctx child = *c;
    child.stretch = 0;
    if (display) {
        child.display = FALSE;
    } else {
        child = ctx_script(c);
    }
    gdouble s = ctx_size(c), t = rule(c), a = axis(c);
    Box* num = layout_node(&child, n->body);
    Box* den = layout_node(&child, n->under);
    gdouble pad = 0.12 * s;
    gdouble w = MAX(num->w, den->w) + 2 * pad;
    gdouble num_shift, den_shift;
    Box* b = box_new();
    if (n->style == TEX_FRAC_NO_BAR) {
        num_shift = MAX(display ? 0.68 * s : 0.44 * s, num->desc + 0.1 * s);
        den_shift = MAX(display ? 0.69 * s : 0.35 * s, den->asc);
        gdouble clearance = display ? 7 * t : 3 * t;
        gdouble gap = (num_shift - num->desc) - (den->asc - den_shift);
        if (gap < clearance) {
            num_shift += (clearance - gap) / 2;
            den_shift += (clearance - gap) / 2;
        }
    } else {
        gdouble gap = display ? MAX(3 * t, 0.15 * s) : MAX(t, 0.08 * s);
        num_shift = MAX(display ? 0.68 * s : 0.39 * s, a + t / 2 + gap + num->desc);
        den_shift = MAX(display ? 0.69 * s : 0.34 * s, -a + t / 2 + gap + den->asc);
        box_line(b, pad / 2, -a, w - pad / 2, -a, t);
    }
    b->w = w;
    b->asc = MAX(num_shift + num->asc, a + t);
    b->desc = MAX(den_shift + den->desc, 0.0);
    box_put(b, num, (w - num->w) / 2, -num_shift);
    box_put(b, den, (w - den->w) / 2, den_shift);
    return b;
}

static Box* sqrt_box(const Ctx* c, const TexNode* n) {
    Ctx inner = *c;
    inner.stretch = 0;
    gdouble s = ctx_size(c), t = rule(c);
    Box* body = layout_node(&inner, n->body);
    gdouble clearance = t + (c->display ? 0.15 : 0.08) * s;
    gdouble top = -(MAX(body->asc, 0.45 * s) + clearance + t / 2);
    gdouble bottom = MAX(body->desc, 0.0) + 0.05 * s;
    gdouble h = bottom - top;
    gdouble hook = MIN(h, 1.0 * s);
    gdouble slope = CLAMP(h * 0.2, 0.25 * s, 0.6 * s);

    /* The index, then the sign after it */
    Box* index = NULL;
    gdouble x0 = 0.05 * s;
    if (n->under != NULL) {
        Ctx small = *c;
        small.level = 2;
        small.display = FALSE;
        index = layout_node(&small, n->under);
        x0 = MAX(x0, index->w - 0.08 * s);
    }
    gdouble p0x = x0, p0y = bottom - 0.42 * hook;
    gdouble p1x = x0 + 0.1 * s, p1y = bottom - 0.5 * hook;
    gdouble p2x = x0 + 0.27 * s, p2y = bottom;
    gdouble p3x = p2x + slope, p3y = top;
    gdouble body_x = p3x + 0.08 * s;
    gdouble end_x = body_x + body->w + 0.08 * s;

    Box* b = box_new();
    box_line(b, p0x, p0y, p1x, p1y, t);
    box_line(b, p1x, p1y, p2x, p2y, 1.8 * t);
    box_line(b, p2x, p2y, p3x, p3y, t);
    box_line(b, p3x, p3y, end_x, top, t);
    b->w = end_x + 0.05 * s;
    b->asc = -top + t;
    b->desc = MAX(body->desc, bottom + t);
    box_put(b, body, body_x, 0);
    if (index != NULL) {
        gdouble iy = p1y - 0.1 * s - index->desc;
        b->asc = MAX(b->asc, -iy + index->asc);
        box_put(b, index, p1x - index->w + 0.02 * s, iy);
    }
    return b;
}

static Box* scripts_box(const Ctx* c, const TexNode* n) {
    gdouble s = ctx_size(c), t = rule(c);
    Box* base = layout_node(c, n->body);
    Ctx sc = ctx_script(c);
    Box* sup = n->over != NULL ? layout_node(&sc, n->over) : NULL;
    Box* sub = n->under != NULL ? layout_node(&sc, n->under) : NULL;
    gboolean limits = n->limits == TEX_LIMITS_ALWAYS || (n->limits == TEX_LIMITS_DISPLAY && c->display);
    Box* b = box_new();
    if (limits) {
        gdouble gap = 0.15 * s;
        gdouble w = MAX(base->w, MAX(sup != NULL ? sup->w : 0, sub != NULL ? sub->w : 0));
        b->w = w;
        b->asc = base->asc;
        b->desc = base->desc;
        if (sup != NULL) {
            gdouble y = -(base->asc + gap + sup->desc);
            b->asc = -y + sup->asc;
            box_put(b, sup, (w - sup->w) / 2, y);
        }
        if (sub != NULL) {
            gdouble y = base->desc + gap + sub->asc;
            b->desc = y + sub->desc;
            box_put(b, sub, (w - sub->w) / 2, y);
        }
        gdouble bw = base->w;
        box_put(b, base, (w - bw) / 2, 0);
        return b;
    }
    gboolean integral = n->body != NULL && n->body->kind == TEX_ATOM && n->body->big && is_integral(n->body->text);
    gdouble sup_x = base->w;
    gdouble sub_x = integral ? base->w * 0.55 : base->w;
    gdouble up = 0, down = 0;
    if (sup != NULL) {
        up = MAX(0.36 * s, base->asc - 0.27 * s);
        up = MAX(up, sup->desc + 0.11 * s);
    }
    if (sub != NULL) {
        down = sup != NULL ? MAX(base->desc + 0.05 * s, 0.25 * s) : MAX(base->desc + 0.05 * s, 0.15 * s);
        down = MAX(down, sub->asc - 0.36 * s);
        if (sup != NULL) {
            gdouble gap = (up - sup->desc) - (sub->asc - down);
            if (gap < 4 * t) down += 4 * t - gap;
        }
    }
    b->w = MAX(sup != NULL ? sup_x + sup->w : base->w, sub != NULL ? sub_x + sub->w : base->w) + 0.05 * s;
    b->asc = MAX(base->asc, sup != NULL ? up + sup->asc : 0);
    b->desc = MAX(base->desc, sub != NULL ? down + sub->desc : 0);
    box_put(b, base, 0, 0);
    if (sup != NULL) box_put(b, sup, sup_x, -up);
    if (sub != NULL) box_put(b, sub, sub_x, down);
    return b;
}

/* TRUE when a row of n (not inside a nested group) has a \middle */
static gboolean has_middle(const TexNode* n) {
    if (n == NULL || n->kind != TEX_ROW) return n != NULL && n->kind == TEX_ATOM && n->delim && n->big == 0;
    for (guint i = 0; i < n->items->len; i++) {
        const TexNode* item = g_ptr_array_index(n->items, i);
        if (item->kind == TEX_ATOM && item->delim && item->big == 0) return TRUE;
    }
    return FALSE;
}

static Box* fenced_box(const Ctx* c, const TexNode* n) {
    Ctx inner = *c;
    inner.stretch = 0;
    gdouble s = ctx_size(c), a = axis(c);
    Box* body = layout_node(&inner, n->body);
    gdouble half = MAX(body->asc - a, body->desc + a);
    gdouble height = MAX(2 * half * 0.92, 2 * half - 0.3 * s);
    if (has_middle(n->body)) {
        box_free(body);
        inner.stretch = height;
        body = layout_node(&inner, n->body);
    }
    Box* left = centred_box(c, n->left, s, height);
    Box* right = centred_box(c, n->right, s, height);
    Box* b = box_new();
    gdouble lw = left->w, bw = body->w, rw = right->w;
    b->asc = MAX(body->asc, MAX(left->asc, right->asc));
    b->desc = MAX(body->desc, MAX(left->desc, right->desc));
    box_put(b, left, 0, 0);
    box_put(b, body, lw, 0);
    box_put(b, right, lw + bw, 0);
    b->w = lw + bw + rw;
    return b;
}

/* An arrow from x1 to x2 at y, its head at x2 */
static void arrow(Box* b, gdouble x1, gdouble x2, gdouble y, gdouble s, gdouble t) {
    gdouble dir = x2 >= x1 ? 1.0 : -1.0;
    box_line(b, x1, y, x2, y, t);
    box_line(b, x2, y, x2 - dir * 0.13 * s, y - 0.08 * s, t);
    box_line(b, x2, y, x2 - dir * 0.13 * s, y + 0.08 * s, t);
}

/* A horizontal brace over (up) or under the width w at y */
static void brace(Box* b, gdouble w, gdouble y, gdouble d, gdouble s, gdouble t) {
    gdouble m = w / 2, e = MIN(0.12 * s, w / 4);
    gdouble pts[7][2] = {{0, y},           {e, y - d / 2},     {m - e, y - d / 2}, {m, y - d},
                         {m + e, y - d / 2}, {w - e, y - d / 2}, {w, y}};
    for (gint i = 0; i < 6; i++) box_line(b, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], t);
}

static Box* accent_box(const Ctx* c, const TexNode* n) {
    gdouble s = ctx_size(c), t = rule(c);
    Box* body = layout_node(c, n->body);
    gboolean italic = n->body != NULL && n->body->kind == TEX_ATOM && (n->body->font & TEX_FONT_ITALIC);
    gdouble skew = italic ? 0.06 * s : 0;
    gdouble top = MAX(body->asc, 0.45 * s);
    gdouble w = body->w;
    Box* b = box_new();
    b->w = w;
    b->asc = body->asc;
    b->desc = body->desc;
    switch ((TexAccent)n->style) {
    case TEX_ACCENT_GLYPH:
    case TEX_ACCENT_WIDE: {
        PangoLayout* l = text_layout(c, n->text != NULL ? n->text : "ˆ", 0, s);
        Ink ink = layout_ink(l);
        gdouble iw = MAX(ink.right - ink.left, 1.0);
        gdouble sx = n->style == TEX_ACCENT_WIDE ? MAX(1.0, w * 0.9 / iw) : 1.0;
        gdouble sy = MIN(1.0 + (sx - 1.0) * 0.15, 1.6);
        Op op = {0};
        op.kind = OP_TEXT;
        op.layout = l;
        op.baseline = ink.baseline;
        op.sx = sx;
        op.sy = sy;
        op.x = (w - iw * sx) / 2 - ink.left * sx + skew;
        op.y = -(top + 0.06 * s) - sy * ink.bottom;
        g_array_append_val(b->ops, op);
        b->asc = MAX(b->asc, -(op.y + sy * ink.top));
        break;
    }
    case TEX_ACCENT_OVERLINE: {
        gdouble y = -(body->asc + 0.1 * s + t / 2);
        box_line(b, 0.02 * s + skew, y, w - 0.02 * s + skew, y, t);
        b->asc = -y + t;
        break;
    }
    case TEX_ACCENT_UNDERLINE: {
        gdouble y = body->desc + 0.1 * s + t / 2;
        box_line(b, 0, y, w, y, t);
        b->desc = y + t;
        break;
    }
    case TEX_ACCENT_VEC: {
        gdouble len = CLAMP(w, 0.35 * s, 0.5 * s);
        gdouble x1 = (w - len) / 2 + skew;
        gdouble y = -(top + 0.14 * s);
        arrow(b, x1, x1 + len, y, s, t);
        b->asc = -y + 0.1 * s;
        break;
    }
    case TEX_ACCENT_RIGHTARROW:
    case TEX_ACCENT_LEFTARROW: {
        gdouble y = -(body->asc + 0.16 * s);
        gdouble len = MAX(w, 0.5 * s);
        if (n->style == TEX_ACCENT_RIGHTARROW) {
            arrow(b, 0, len, y, s, t);
        } else {
            arrow(b, len, 0, y, s, t);
        }
        b->w = MAX(w, len);
        b->asc = -y + 0.1 * s;
        break;
    }
    case TEX_ACCENT_OVERBRACE: {
        gdouble d = 0.2 * s;
        gdouble y = -(body->asc + 0.08 * s);
        brace(b, w, y, d, s, 1.2 * t);
        b->asc = -y + d + t;
        break;
    }
    case TEX_ACCENT_UNDERBRACE: {
        gdouble d = 0.2 * s;
        gdouble y = body->desc + 0.08 * s;
        brace(b, w, y, -d, s, 1.2 * t);
        b->desc = y + d + t;
        break;
    }
    }
    box_put(b, body, 0, 0);
    return b;
}

static Box* table_box(const Ctx* c, const TexNode* n) {
    Ctx cell_ctx = *c;
    cell_ctx.stretch = 0;
    cell_ctx.display = n->style == TEX_TABLE_ALIGNED;
    gdouble s = ctx_size(c);
    const gchar* aligns = n->text != NULL && n->text[0] != '\0' ? n->text : "c";
    gsize n_aligns = strlen(aligns);
    gboolean pairs = strcmp(aligns, "rl") == 0;
    gboolean compact = n->em <= 0.0;
    guint rows = n->items->len, cols = 0;
    for (guint r = 0; r < rows; r++) cols = MAX(cols, ((GPtrArray*)g_ptr_array_index(n->items, r))->len);
    if (rows == 0 || cols == 0) return box_new();

    Box** cells = g_new0(Box*, rows * cols);
    gdouble* col_w = g_new0(gdouble, cols);
    gdouble* row_asc = g_new0(gdouble, rows);
    gdouble* row_desc = g_new0(gdouble, rows);
    for (guint r = 0; r < rows; r++) {
        GPtrArray* row = g_ptr_array_index(n->items, r);
        row_asc[r] = compact ? 0.5 * s : 0.7 * s;
        row_desc[r] = compact ? 0.2 * s : 0.3 * s;
        for (guint k = 0; k < row->len; k++) {
            const TexNode* cell = g_ptr_array_index(row, k);
            Box* box = (pairs && k % 2 == 1 && cell->kind == TEX_ROW) ? row_box(&cell_ctx, cell->items, TRUE)
                                                                      : layout_node(&cell_ctx, cell);
            cells[r * cols + k] = box;
            col_w[k] = MAX(col_w[k], box->w);
            row_asc[r] = MAX(row_asc[r], box->asc);
            row_desc[r] = MAX(row_desc[r], box->desc);
        }
    }
    gdouble row_gap = compact ? 0.05 * s : n->style == TEX_TABLE_ALIGNED ? 0.3 * s : 0.2 * s;
    gdouble height = 0;
    for (guint r = 0; r < rows; r++) height += row_asc[r] + row_desc[r] + (r > 0 ? row_gap : 0);
    gdouble* col_x = g_new0(gdouble, cols);
    gdouble x = 0;
    for (guint k = 0; k < cols; k++) {
        if (k > 0) {
            gdouble gap = pairs ? (k % 2 == 1 ? 0 : n->em * s)
                                : (n->style == TEX_TABLE_ALIGNED && n_aligns > 1 ? 0.3 * s : n->em * s);
            x += gap;
        }
        col_x[k] = x;
        x += col_w[k];
    }

    Box* b = box_new();
    b->w = x;
    gdouble top = -axis(c) - height / 2;
    b->asc = -top;
    b->desc = height / 2 - axis(c);
    gdouble y = top;
    for (guint r = 0; r < rows; r++) {
        y += row_asc[r];
        for (guint k = 0; k < cols; k++) {
            Box* box = cells[r * cols + k];
            if (box == NULL) continue;
            gchar align = aligns[MIN(k, n_aligns - 1)];
            if (pairs) align = k % 2 == 0 ? 'r' : 'l';
            else if (n_aligns > 1) align = aligns[k % n_aligns];
            gdouble cx = col_x[k];
            if (align == 'r') cx += col_w[k] - box->w;
            else if (align == 'c') cx += (col_w[k] - box->w) / 2;
            box_put(b, box, cx, y);
        }
        y += row_desc[r] + row_gap;
    }
    g_free(cells);
    g_free(col_w);
    g_free(col_x);
    g_free(row_asc);
    g_free(row_desc);
    return b;
}

static Box* framed_box(const Ctx* c, const TexNode* n) {
    gdouble s = ctx_size(c), t = rule(c);
    Box* body = layout_node(c, n->body);
    Box* b = box_new();
    if (n->style == TEX_BOX_PHANTOM) {
        b->w = body->w;
        b->asc = body->asc;
        b->desc = body->desc;
        box_free(body);
        return b;
    }
    gdouble pad = 0.25 * s;
    gdouble x1 = t / 2, x2 = body->w + 2 * pad + t / 2;
    gdouble y1 = -(body->asc + pad), y2 = body->desc + pad;
    box_line(b, x1, y1, x2, y1, t);
    box_line(b, x2, y1, x2, y2, t);
    box_line(b, x2, y2, x1, y2, t);
    box_line(b, x1, y2, x1, y1, t);
    b->w = x2 + t / 2;
    b->asc = -y1 + t;
    b->desc = y2 + t;
    box_put(b, body, pad + t / 2, 0);
    return b;
}

static Box* layout_node(const Ctx* c, const TexNode* n) {
    if (n == NULL) return box_new();
    switch (n->kind) {
    case TEX_ROW:
        return row_box(c, n->items, FALSE);
    case TEX_ATOM:
        return atom_box(c, n);
    case TEX_FRAC:
        return frac_box(c, n);
    case TEX_SQRT:
        return sqrt_box(c, n);
    case TEX_SCRIPTS:
        return scripts_box(c, n);
    case TEX_FENCED:
        return fenced_box(c, n);
    case TEX_ACCENT:
        return accent_box(c, n);
    case TEX_TABLE:
        return table_box(c, n);
    case TEX_SPACE: {
        Box* b = box_new();
        b->w = n->em * ctx_size(c);
        return b;
    }
    case TEX_BOX:
        return framed_box(c, n);
    }
    return box_new();
}

/* --- The paintable --- */

#define MATH_TYPE_PAINTABLE (math_paintable_get_type())
G_DECLARE_FINAL_TYPE(MathPaintable, math_paintable, MATH, PAINTABLE, GObject)

struct _MathPaintable {
    GObject parent;
    GArray* ops;
    gint width, height;
    gdouble origin_x, origin_y; /* the formula's baseline start in the paintable */
};

static void math_paintable_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot, double width, double height) {
    MathPaintable* self = MATH_PAINTABLE(paintable);
    if (self->width <= 0 || self->height <= 0) return;
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height);
    cairo_t* cr = gtk_snapshot_append_cairo(GTK_SNAPSHOT(snapshot), &bounds);
    cairo_scale(cr, width / self->width, height / self->height);
    cairo_translate(cr, self->origin_x, self->origin_y);
    double r, g, b;
    palette_rgb(PAL_FG_TEXT, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (guint i = 0; i < self->ops->len; i++) {
        const Op* op = &g_array_index(self->ops, Op, i);
        if (op->kind == OP_TEXT) {
            cairo_save(cr);
            cairo_translate(cr, op->x, op->y);
            cairo_scale(cr, op->sx, op->sy);
            cairo_move_to(cr, 0, -op->baseline);
            pango_cairo_show_layout(cr, op->layout);
            cairo_restore(cr);
        } else {
            cairo_set_line_width(cr, op->wide);
            cairo_move_to(cr, op->x, op->y);
            cairo_line_to(cr, op->x2, op->y2);
            cairo_stroke(cr);
        }
    }
    cairo_destroy(cr);
}

static gint math_paintable_get_intrinsic_width(GdkPaintable* paintable) {
    return MATH_PAINTABLE(paintable)->width;
}

static gint math_paintable_get_intrinsic_height(GdkPaintable* paintable) {
    return MATH_PAINTABLE(paintable)->height;
}

static GdkPaintableFlags math_paintable_get_flags(GdkPaintable* paintable) {
    (void)paintable;
    return GDK_PAINTABLE_STATIC_SIZE; /* the colour follows the theme */
}

static void math_paintable_iface_init(GdkPaintableInterface* iface) {
    iface->snapshot = math_paintable_snapshot;
    iface->get_intrinsic_width = math_paintable_get_intrinsic_width;
    iface->get_intrinsic_height = math_paintable_get_intrinsic_height;
    iface->get_flags = math_paintable_get_flags;
}

G_DEFINE_TYPE_WITH_CODE(MathPaintable, math_paintable, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, math_paintable_iface_init))

static void math_paintable_finalize(GObject* object) {
    MathPaintable* self = MATH_PAINTABLE(object);
    if (self->ops != NULL) g_array_unref(self->ops);
    G_OBJECT_CLASS(math_paintable_parent_class)->finalize(object);
}

static void math_paintable_class_init(MathPaintableClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = math_paintable_finalize;
}

static void math_paintable_init(MathPaintable* self) {
    (void)self;
}

GdkPaintable* math_paintable_new(PangoContext* pc, const gchar* tex, gboolean display, gdouble size_px,
                                 gint* descent) {
    PangoContext* own = NULL;
    if (pc == NULL) pc = own = pango_font_map_create_context(pango_cairo_font_map_get_default());
    Ctx c = {0};
    c.pc = pc;
    /* Serif math looks smaller than the sans text around it at the same size */
    c.base = MAX(size_px, 4.0) * 1.12;
    c.display = display;
    TexNode* tree = tex_math_parse(tex);
    Box* box = layout_node(&c, tree);
    tex_node_free(tree);

    MathPaintable* self = g_object_new(MATH_TYPE_PAINTABLE, NULL);
    gdouble pad_x = display ? 0.1 * c.base : 0.06 * c.base, pad_y = 1.0;
    gint below = (gint)ceil(box->desc + pad_y);
    self->width = (gint)ceil(MAX(box->w, 1.0) + 2 * pad_x);
    self->height = (gint)ceil(box->asc + pad_y) + below;
    self->origin_x = pad_x;
    self->origin_y = self->height - below;
    self->ops = g_array_ref(box->ops);
    box_free(box);
    if (descent != NULL) *descent = below;
    if (own != NULL) g_object_unref(own);
    return GDK_PAINTABLE(self);
}
