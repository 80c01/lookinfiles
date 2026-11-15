/* Outlines of OpenDocument drawing shapes (see odp_geometry.h) */
#include "office/odp_geometry.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STEPS 100000
#define MAX_EXPR_DEPTH 64
#define MAX_MODIFIERS 32
#define KAPPA 0.5522847498

gboolean odp_geometry_viewbox(const gchar* value, gdouble box[4]) {
    if (value == NULL) {
        return FALSE;
    }
    const gchar* p = value;
    for (gint i = 0; i < 4; i++) {
        while (*p == ' ' || *p == ',') p++;
        gchar* end = NULL;
        box[i] = g_ascii_strtod(p, &end);
        if (end == p || !isfinite(box[i])) {
            return FALSE;
        }
        p = end;
    }
    return box[2] > 0 && box[3] > 0;
}

/* ---- Pen: steps with the current point ---- */

typedef struct {
    GArray* steps;
    gdouble x, y;           /* current point */
    gdouble start_x, start_y;
    gboolean has_point;
    gdouble ox, oy;         /* origin moved to 0, 0 */
} Pen;

static void pen_add(Pen* pen, SlidePathOp op, const gdouble* xs, const gdouble* ys, gint n) {
    if (pen->steps->len >= MAX_STEPS) {
        return;
    }
    SlidePathStep st = { op, { 0, 0, 0 }, { 0, 0, 0 } };
    for (gint i = 0; i < n; i++) {
        st.x[i] = xs[i] - pen->ox;
        st.y[i] = ys[i] - pen->oy;
    }
    g_array_append_val(pen->steps, st);
}

static void pen_move(Pen* pen, gdouble x, gdouble y) {
    pen_add(pen, SLIDE_PATH_MOVE, &x, &y, 1);
    pen->x = pen->start_x = x;
    pen->y = pen->start_y = y;
    pen->has_point = TRUE;
}

static void pen_line(Pen* pen, gdouble x, gdouble y) {
    if (!pen->has_point) {
        pen_move(pen, x, y);
        return;
    }
    pen_add(pen, SLIDE_PATH_LINE, &x, &y, 1);
    pen->x = x;
    pen->y = y;
}

static void pen_curve(Pen* pen, gdouble x1, gdouble y1, gdouble x2, gdouble y2, gdouble x, gdouble y) {
    if (!pen->has_point) {
        pen_move(pen, x1, y1);
    }
    gdouble xs[3] = { x1, x2, x }, ys[3] = { y1, y2, y };
    pen_add(pen, SLIDE_PATH_CURVE, xs, ys, 3);
    pen->x = x;
    pen->y = y;
}

static void pen_close(Pen* pen) {
    if (pen->has_point) {
        pen_add(pen, SLIDE_PATH_CLOSE, NULL, NULL, 0);
        pen->x = pen->start_x;
        pen->y = pen->start_y;
    }
}

/* Quadratic curve as a cubic one */
static void pen_quad(Pen* pen, gdouble qx, gdouble qy, gdouble x, gdouble y) {
    gdouble x0 = pen->has_point ? pen->x : qx, y0 = pen->has_point ? pen->y : qy;
    pen_curve(pen, x0 + 2.0 / 3.0 * (qx - x0), y0 + 2.0 / 3.0 * (qy - y0), x + 2.0 / 3.0 * (qx - x),
              y + 2.0 / 3.0 * (qy - y), x, y);
}

/* Elliptical arc from angle a0 to a1 (radians, screen coordinates: y down,
 * positive angles clockwise), as curves of at most a quarter turn; the
 * pen moves (move) or draws a line to the start */
static void pen_arc(Pen* pen, gdouble cx, gdouble cy, gdouble rx, gdouble ry, gdouble a0, gdouble a1, gboolean move) {
    gdouble sx = cx + rx * cos(a0), sy = cy + ry * sin(a0);
    if (move || !pen->has_point) {
        pen_move(pen, sx, sy);
    } else {
        pen_line(pen, sx, sy);
    }
    gdouble sweep = a1 - a0;
    gint n = (gint)ceil(fabs(sweep) / (G_PI / 2) - 1e-9);
    n = CLAMP(n, 1, 16);
    gdouble d = sweep / n;
    gdouble k = 4.0 / 3.0 * tan(d / 4);
    gdouble a = a0;
    for (gint i = 0; i < n; i++) {
        gdouble b = a + d;
        gdouble x1 = cx + rx * (cos(a) - k * sin(a)), y1 = cy + ry * (sin(a) + k * cos(a));
        gdouble x2 = cx + rx * (cos(b) + k * sin(b)), y2 = cy + ry * (sin(b) - k * cos(b));
        pen_curve(pen, x1, y1, x2, y2, cx + rx * cos(b), cy + ry * sin(b));
        a = b;
    }
}

static GArray* pen_result(Pen* pen) {
    gboolean drawable = FALSE;
    for (guint i = 0; i < pen->steps->len && !drawable; i++) {
        drawable = g_array_index(pen->steps, SlidePathStep, i).op != SLIDE_PATH_MOVE;
    }
    if (!drawable) {
        g_array_free(pen->steps, TRUE);
        return NULL;
    }
    return pen->steps;
}

static void pen_init(Pen* pen, gdouble ox, gdouble oy) {
    memset(pen, 0, sizeof *pen);
    pen->steps = g_array_new(FALSE, FALSE, sizeof(SlidePathStep));
    pen->ox = ox;
    pen->oy = oy;
}

/* ---- SVG paths ---- */

static gboolean read_number(const gchar** p, gdouble* out) {
    const gchar* s = *p;
    while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (!(g_ascii_isdigit(*s) || *s == '-' || *s == '+' || *s == '.')) {
        return FALSE;
    }
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(s, &end);
    if (end == s || !isfinite(v)) {
        return FALSE;
    }
    *out = v;
    *p = end;
    return TRUE;
}

/* An arc flag: "0" or "1", maybe without a separator */
static gboolean read_flag(const gchar** p, gboolean* out) {
    const gchar* s = *p;
    while (*s == ' ' || *s == ',') s++;
    if (*s != '0' && *s != '1') {
        return FALSE;
    }
    *out = *s == '1';
    *p = s + 1;
    return TRUE;
}

/* SVG arc by its end point (SVG 1.1 appendix F.6) */
static void svg_arc(Pen* pen, gdouble rx, gdouble ry, gdouble rot_deg, gboolean large, gboolean sweep, gdouble x,
                    gdouble y) {
    gdouble x0 = pen->x, y0 = pen->y;
    rx = fabs(rx);
    ry = fabs(ry);
    if (rx < 1e-9 || ry < 1e-9 || (x0 == x && y0 == y)) {
        pen_line(pen, x, y);
        return;
    }
    gdouble phi = rot_deg * G_PI / 180.0, cp = cos(phi), sp = sin(phi);
    gdouble dx = (x0 - x) / 2, dy = (y0 - y) / 2;
    gdouble x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    gdouble lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (lambda > 1) {
        rx *= sqrt(lambda);
        ry *= sqrt(lambda);
    }
    gdouble num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    gdouble den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    gdouble coef = den > 0 ? sqrt(MAX(num / den, 0)) : 0;
    if (large == sweep) {
        coef = -coef;
    }
    gdouble cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
    gdouble cx = cp * cx1 - sp * cy1 + (x0 + x) / 2, cy = sp * cx1 + cp * cy1 + (y0 + y) / 2;
    gdouble a0 = atan2((y1 - cy1) / ry, (x1 - cx1) / rx);
    gdouble a1 = atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx);
    gdouble d = a1 - a0;
    if (sweep && d < 0) d += 2 * G_PI;
    if (!sweep && d > 0) d -= 2 * G_PI;
    /* Drawn unrotated around the centre, then rotated point by point */
    guint first = pen->steps->len;
    pen_arc(pen, 0, 0, rx, ry, a0, a0 + d, FALSE);
    /* pen_arc started with a line to the start point: drop it */
    if (pen->steps->len > first) {
        g_array_remove_index(pen->steps, first);
    }
    for (guint i = first; i < pen->steps->len; i++) {
        SlidePathStep* st = &g_array_index(pen->steps, SlidePathStep, i);
        for (gint k = 0; k < 3; k++) {
            gdouble ux = st->x[k] + pen->ox, uy = st->y[k] + pen->oy;
            st->x[k] = cp * ux - sp * uy + cx - pen->ox;
            st->y[k] = sp * ux + cp * uy + cy - pen->oy;
        }
    }
    pen->x = x;
    pen->y = y;
}

GArray* odp_geometry_svg_path(const gchar* d, gdouble min_x, gdouble min_y) {
    if (d == NULL) {
        return NULL;
    }
    Pen pen;
    pen_init(&pen, min_x, min_y);
    const gchar* p = d;
    gchar cmd = 0;
    gdouble last_cx = 0, last_cy = 0;   /* last control point (S, T) */
    gchar last_cmd = 0;
    while (*p != '\0' && pen.steps->len < MAX_STEPS) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '\0') break;
        if (g_ascii_isalpha(*p)) {
            cmd = *p++;
            if (cmd == 'Z' || cmd == 'z') {
                pen_close(&pen);
                last_cmd = cmd;
                continue;
            }
        } else if (cmd == 0) {
            break;
        }
        gboolean rel = g_ascii_islower(cmd);
        gdouble bx = rel ? pen.x : 0, by = rel ? pen.y : 0;
        gdouble v[7];
        gboolean ok = TRUE;
        switch (g_ascii_toupper(cmd)) {
        case 'M':
            ok = read_number(&p, &v[0]) && read_number(&p, &v[1]);
            if (ok) {
                pen_move(&pen, bx + v[0], by + v[1]);
                cmd = rel ? 'l' : 'L';   /* further pairs are lines */
            }
            break;
        case 'L':
            ok = read_number(&p, &v[0]) && read_number(&p, &v[1]);
            if (ok) pen_line(&pen, bx + v[0], by + v[1]);
            break;
        case 'H':
            ok = read_number(&p, &v[0]);
            if (ok) pen_line(&pen, bx + v[0], pen.y);
            break;
        case 'V':
            ok = read_number(&p, &v[0]);
            if (ok) pen_line(&pen, pen.x, by + v[0]);
            break;
        case 'C':
            for (gint i = 0; i < 6 && ok; i++) ok = read_number(&p, &v[i]);
            if (ok) {
                pen_curve(&pen, bx + v[0], by + v[1], bx + v[2], by + v[3], bx + v[4], by + v[5]);
                last_cx = bx + v[2];
                last_cy = by + v[3];
            }
            break;
        case 'S':
            for (gint i = 0; i < 4 && ok; i++) ok = read_number(&p, &v[i]);
            if (ok) {
                gboolean smooth = strchr("CcSs", last_cmd) != NULL && last_cmd != 0;
                gdouble x1 = smooth ? 2 * pen.x - last_cx : pen.x, y1 = smooth ? 2 * pen.y - last_cy : pen.y;
                pen_curve(&pen, x1, y1, bx + v[0], by + v[1], bx + v[2], by + v[3]);
                last_cx = bx + v[0];
                last_cy = by + v[1];
            }
            break;
        case 'Q':
            for (gint i = 0; i < 4 && ok; i++) ok = read_number(&p, &v[i]);
            if (ok) {
                pen_quad(&pen, bx + v[0], by + v[1], bx + v[2], by + v[3]);
                last_cx = bx + v[0];
                last_cy = by + v[1];
            }
            break;
        case 'T':
            ok = read_number(&p, &v[0]) && read_number(&p, &v[1]);
            if (ok) {
                gboolean smooth = strchr("QqTt", last_cmd) != NULL && last_cmd != 0;
                gdouble qx = smooth ? 2 * pen.x - last_cx : pen.x, qy = smooth ? 2 * pen.y - last_cy : pen.y;
                pen_quad(&pen, qx, qy, bx + v[0], by + v[1]);
                last_cx = qx;
                last_cy = qy;
            }
            break;
        case 'A': {
            gboolean large = FALSE, sweep = FALSE;
            ok = read_number(&p, &v[0]) && read_number(&p, &v[1]) && read_number(&p, &v[2]) && read_flag(&p, &large) &&
                 read_flag(&p, &sweep) && read_number(&p, &v[3]) && read_number(&p, &v[4]);
            if (ok) svg_arc(&pen, v[0], v[1], v[2], large, sweep, bx + v[3], by + v[4]);
            break;
        }
        default:
            ok = FALSE;
            break;
        }
        if (!ok) {
            break;
        }
        last_cmd = cmd;
    }
    return pen_result(&pen);
}

GArray* odp_geometry_points(const gchar* points, gdouble min_x, gdouble min_y, gboolean close) {
    if (points == NULL) {
        return NULL;
    }
    Pen pen;
    pen_init(&pen, min_x, min_y);
    const gchar* p = points;
    gdouble x, y;
    guint n = 0;
    while (read_number(&p, &x) && read_number(&p, &y) && pen.steps->len < MAX_STEPS) {
        n++ == 0 ? pen_move(&pen, x, y) : pen_line(&pen, x, y);
    }
    if (n < 2) {
        g_array_free(pen.steps, TRUE);
        return NULL;
    }
    if (close) {
        pen_close(&pen);
    }
    return pen.steps;
}

/* ---- Formulas ---- */

typedef struct {
    const OdpEnhanced* g;
    gdouble mods[MAX_MODIFIERS];
    guint n_mods;
    gdouble* values;    /* per equation */
    guint8* state;      /* 0 not computed, 1 computing, 2 done */
} Eval;

typedef struct {
    const gchar* p;
    Eval* ev;
    gint depth;
} Expr;

static gdouble parse_expr(Expr* e);

static void skip_spaces(Expr* e) {
    while (*e->p == ' ' || *e->p == '\t') e->p++;
}

static gdouble equation_value(Eval* ev, guint i) {
    if (i >= ev->g->n_equations || ev->state[i] == 1) {
        return 0;   /* unknown or a cycle */
    }
    if (ev->state[i] == 0) {
        ev->state[i] = 1;
        Expr sub = { ev->g->eq_formulas[i] != NULL ? ev->g->eq_formulas[i] : "0", ev, 0 };
        gdouble v = parse_expr(&sub);
        ev->values[i] = isfinite(v) ? v : 0;
        ev->state[i] = 2;
    }
    return ev->values[i];
}

static gdouble named_value(Eval* ev, const gchar* name, gsize len) {
    for (guint i = 0; i < ev->g->n_equations; i++) {
        const gchar* n = ev->g->eq_names[i];
        if (n != NULL && strlen(n) == len && strncmp(n, name, len) == 0) {
            return equation_value(ev, i);
        }
    }
    return 0;
}

static gdouble constant(Eval* ev, const gchar* id, gsize len) {
    const gdouble* b = ev->g->box;
#define IS(s) (len == strlen(s) && strncmp(id, s, len) == 0)
    if (IS("pi")) return G_PI;
    if (IS("left")) return b[0];
    if (IS("top")) return b[1];
    if (IS("right")) return b[0] + b[2];
    if (IS("bottom")) return b[1] + b[3];
    if (IS("width")) return b[2];
    if (IS("height")) return b[3];
    /* Logical size: 1/100 mm */
    if (IS("logwidth")) return ev->g->width * 2540.0 / 72.0;
    if (IS("logheight")) return ev->g->height * 2540.0 / 72.0;
    if (IS("hasstroke") || IS("hasfill")) return 1;
    return 0;   /* xstretch, ystretch and unknown names */
#undef IS
}

static gdouble call(const gchar* f, gsize len, const gdouble* a, gint n) {
#define IS(s) (len == strlen(s) && strncmp(f, s, len) == 0)
    if (IS("abs") && n >= 1) return fabs(a[0]);
    if (IS("sqrt") && n >= 1) return a[0] >= 0 ? sqrt(a[0]) : 0;
    if (IS("sin") && n >= 1) return sin(a[0]);
    if (IS("cos") && n >= 1) return cos(a[0]);
    if (IS("tan") && n >= 1) return tan(a[0]);
    if (IS("atan") && n >= 1) return atan(a[0]);
    if (IS("atan2") && n >= 2) return atan2(a[0], a[1]);
    if (IS("min") && n >= 2) return MIN(a[0], a[1]);
    if (IS("max") && n >= 2) return MAX(a[0], a[1]);
    if (IS("if") && n >= 3) return a[0] > 0 ? a[1] : a[2];
    return 0;
#undef IS
}

static gdouble parse_factor(Expr* e) {
    skip_spaces(e);
    if (++e->depth > MAX_EXPR_DEPTH) {
        e->p += strlen(e->p);
        return 0;
    }
    gdouble v = 0;
    const gchar* s = e->p;
    if (*s == '-') {
        e->p++;
        v = -parse_factor(e);
    } else if (*s == '+') {
        e->p++;
        v = parse_factor(e);
    } else if (*s == '(') {
        e->p++;
        v = parse_expr(e);
        skip_spaces(e);
        if (*e->p == ')') e->p++;
    } else if (*s == '?') {
        const gchar* name = ++e->p;
        while (g_ascii_isalnum(*e->p) || *e->p == '_') e->p++;
        v = named_value(e->ev, name, (gsize)(e->p - name));
    } else if (*s == '$') {
        e->p++;
        gchar* end = NULL;
        guint64 i = g_ascii_strtoull(e->p, &end, 10);
        if (end != e->p) {
            e->p = end;
            v = i < e->ev->n_mods ? e->ev->mods[i] : 0;
        }
    } else if (g_ascii_isdigit(*s) || *s == '.') {
        gchar* end = NULL;
        v = g_ascii_strtod(s, &end);
        e->p = end != s ? end : s + 1;
    } else if (g_ascii_isalpha(*s)) {
        while (g_ascii_isalnum(*e->p)) e->p++;
        gsize len = (gsize)(e->p - s);
        skip_spaces(e);
        if (*e->p == '(') {
            e->p++;
            gdouble args[3] = { 0, 0, 0 };
            gint n = 0;
            for (;;) {
                gdouble a = parse_expr(e);
                if (n < 3) args[n] = a;
                n++;
                skip_spaces(e);
                if (*e->p == ',') {
                    e->p++;
                    continue;
                }
                if (*e->p == ')') e->p++;
                break;
            }
            v = call(s, len, args, MIN(n, 3));
        } else {
            v = constant(e->ev, s, len);
        }
    } else if (*s != '\0') {
        e->p++;
    }
    e->depth--;
    return v;
}

static gdouble parse_term(Expr* e) {
    gdouble v = parse_factor(e);
    for (;;) {
        skip_spaces(e);
        if (*e->p == '*') {
            e->p++;
            v *= parse_factor(e);
        } else if (*e->p == '/') {
            e->p++;
            gdouble d = parse_factor(e);
            v = d != 0 ? v / d : 0;
        } else {
            return v;
        }
    }
}

static gdouble parse_expr(Expr* e) {
    if (++e->depth > MAX_EXPR_DEPTH) {
        e->p += strlen(e->p);
        return 0;
    }
    gdouble v = parse_term(e);
    for (;;) {
        skip_spaces(e);
        if (*e->p == '+') {
            e->p++;
            v += parse_term(e);
        } else if (*e->p == '-') {
            e->p++;
            v -= parse_term(e);
        } else {
            break;
        }
    }
    e->depth--;
    return v;
}

static void eval_init(Eval* ev, const OdpEnhanced* g) {
    memset(ev, 0, sizeof *ev);
    ev->g = g;
    ev->values = g_new0(gdouble, MAX(g->n_equations, 1));
    ev->state = g_new0(guint8, MAX(g->n_equations, 1));
    const gchar* p = g->modifiers;
    gdouble v;
    while (p != NULL && ev->n_mods < MAX_MODIFIERS && read_number(&p, &v)) {
        ev->mods[ev->n_mods++] = v;
    }
}

static void eval_clear(Eval* ev) {
    g_free(ev->values);
    g_free(ev->state);
}

gdouble odp_geometry_formula(const OdpEnhanced* g, const gchar* formula) {
    Eval ev;
    eval_init(&ev, g);
    Expr e = { formula != NULL ? formula : "0", &ev, 0 };
    gdouble v = parse_expr(&e);
    eval_clear(&ev);
    return isfinite(v) ? v : 0;
}

/* ---- Enhanced paths ---- */

/* Next parameter of a path command: a number, ?equation or $modifier */
static gboolean path_param(const gchar** p, Eval* ev, gdouble* out) {
    const gchar* s = *p;
    while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '?' || *s == '$') {
        const gchar* start = s++;
        while (g_ascii_isalnum(*s) || *s == '_') s++;
        gchar* token = g_strndup(start, (gsize)(s - start));
        Expr e = { token, ev, 0 };
        *out = parse_factor(&e);
        g_free(token);
        *p = s;
        return TRUE;
    }
    *p = s;
    return read_number(p, out);
}

/* Screen angle (radians) of a point seen from the centre of an ellipse */
static gdouble ellipse_angle(gdouble cx, gdouble cy, gdouble rx, gdouble ry, gdouble x, gdouble y) {
    return atan2((y - cy) / (ry != 0 ? ry : 1), (x - cx) / (rx != 0 ? rx : 1));
}

GArray* odp_geometry_enhanced(const OdpEnhanced* g) {
    if (g == NULL || g->path == NULL) {
        return NULL;
    }
    Eval ev;
    eval_init(&ev, g);
    Pen pen;
    pen_init(&pen, g->box[0], g->box[1]);
    const gchar* p = g->path;
    gchar cmd = 0;
    gboolean quadrant_x = TRUE;   /* X / Y alternate */
    while (*p != '\0' && pen.steps->len < MAX_STEPS) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '\0') break;
        if (g_ascii_isalpha(*p)) {
            cmd = *p++;
            if (cmd == 'X') quadrant_x = TRUE;
            if (cmd == 'Y') quadrant_x = FALSE;
            if (cmd == 'Z') pen_close(&pen);
            if (strchr("ZNFS", cmd) != NULL) {
                if (cmd == 'N') pen.has_point = FALSE;
                cmd = 0;
            }
            continue;
        }
        if (cmd == 0) {
            break;
        }
        gdouble v[8];
        gint need = strchr("ML", cmd) ? 2 : cmd == 'C' ? 6 : cmd == 'Q' ? 4 : strchr("TU", cmd) ? 6
                    : strchr("ABWV", cmd) ? 8 : strchr("XY", cmd) ? 2 : cmd == 'G' ? 4 : 0;
        gboolean ok = need > 0;
        for (gint i = 0; i < need && ok; i++) {
            ok = path_param(&p, &ev, &v[i]);
        }
        if (!ok) {
            break;
        }
        switch (cmd) {
        case 'M':
            pen_move(&pen, v[0], v[1]);
            cmd = 'L';
            break;
        case 'L':
            pen_line(&pen, v[0], v[1]);
            break;
        case 'C':
            pen_curve(&pen, v[0], v[1], v[2], v[3], v[4], v[5]);
            break;
        case 'Q':
            pen_quad(&pen, v[0], v[1], v[2], v[3]);
            break;
        case 'T':
        case 'U': {
            /* Centre, radii, angles in degrees counter-clockwise */
            gdouble t0 = v[4], t1 = v[5];
            while (t1 <= t0) t1 += 360;
            pen_arc(&pen, v[0], v[1], v[2], v[3], -t0 * G_PI / 180.0, -t1 * G_PI / 180.0, cmd == 'U');
            break;
        }
        case 'A':
        case 'B':
        case 'W':
        case 'V': {
            /* Bounding box, start and end rays; A, B counter-clockwise */
            gdouble cx = (v[0] + v[2]) / 2, cy = (v[1] + v[3]) / 2;
            gdouble rx = fabs(v[2] - v[0]) / 2, ry = fabs(v[3] - v[1]) / 2;
            gdouble a0 = ellipse_angle(cx, cy, rx, ry, v[4], v[5]);
            gdouble a1 = ellipse_angle(cx, cy, rx, ry, v[6], v[7]);
            gboolean clockwise = cmd == 'W' || cmd == 'V';
            if (clockwise) {
                while (a1 <= a0) a1 += 2 * G_PI;
            } else {
                while (a1 >= a0) a1 -= 2 * G_PI;
            }
            pen_arc(&pen, cx, cy, rx, ry, a0, a1, cmd == 'B' || cmd == 'V');
            break;
        }
        case 'X':
        case 'Y': {
            gdouble x0 = pen.x, y0 = pen.y, x = v[0], y = v[1];
            if (quadrant_x) {
                pen_curve(&pen, x0 + KAPPA * (x - x0), y0, x, y - KAPPA * (y - y0), x, y);
            } else {
                pen_curve(&pen, x0, y0 + KAPPA * (y - y0), x - KAPPA * (x - x0), y, x, y);
            }
            quadrant_x = !quadrant_x;
            break;
        }
        case 'G': {
            /* Radii, start and sweep angles in degrees, clockwise */
            gdouble a0 = v[2] * G_PI / 180.0, a1 = (v[2] + v[3]) * G_PI / 180.0;
            gdouble cx = pen.x - v[0] * cos(a0), cy = pen.y - v[1] * sin(a0);
            pen_arc(&pen, cx, cy, v[0], v[1], a0, a1, FALSE);
            break;
        }
        default:
            break;
        }
    }
    eval_clear(&ev);
    return pen_result(&pen);
}
