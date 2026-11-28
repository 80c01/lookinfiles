/* OpenDocument presentations drawn like Impress (see odp_reader.h) */
#include "office/odp_reader.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#include "formats/zip_reader.h"
#include "office/odp_geometry.h"
#include "office/odp_styles.h"
#include "office/odp_text.h"

#define MAX_PART (64u * 1024 * 1024)
#define MAX_PICTURE (16u * 1024 * 1024)
#define MAX_SLIDES 2000
#define MAX_SHAPES 4000
#define MAX_GROUP_DEPTH 32
#define MAX_EQUATIONS 512
#define CM (72.0 / 2.54)
#define DEFAULT_FILL 0x729FCF     /* LibreOffice's default shape colours */
#define DEFAULT_LINE 0x3465A4

typedef struct {
    ZipReader* zip;
    OdpStyles* styles;
    SlideDeck* deck;
    GHashTable* pictures;       /* href -> GBytes* (NULL: unreadable) */
    GHashTable* decls;          /* "footer/name", "date-time/name", "header/name" -> text */
    guint picture_pages;        /* pages whose pictures are read */
    /* Current page */
    guint page_index;
    const gchar* master_name;
    const OdpNode* master;
    gboolean in_master;
    gboolean show_footer, show_date_time, show_header, show_page_number;
    const gchar* footer;
    const gchar* date_time;
    const gchar* header;
    Slide* slide;
} Reader;

/* ---- Pictures ---- */

static GBytes* picture(Reader* r, const gchar* href) {
    if (href == NULL || strstr(href, "://") != NULL || r->page_index >= r->picture_pages) {
        return NULL;
    }
    while (g_str_has_prefix(href, "./")) {
        href += 2;
    }
    if (g_hash_table_contains(r->pictures, href)) {
        GBytes* b = g_hash_table_lookup(r->pictures, href);
        return b != NULL ? g_bytes_ref(b) : NULL;
    }
    GBytes* b = zip_reader_has(r->zip, href) ? zip_reader_read(r->zip, href, MAX_PICTURE, NULL) : NULL;
    g_hash_table_insert(r->pictures, g_strdup(href), b);
    return b != NULL ? g_bytes_ref(b) : NULL;
}

/* Pictures cairo cannot draw through gdk-pixbuf (metafiles) */
static gboolean drawable_picture(const gchar* href) {
    const gchar* dot = href != NULL ? strrchr(href, '.') : NULL;
    if (dot == NULL) {
        return href != NULL;
    }
    return g_ascii_strcasecmp(dot, ".svm") != 0 && g_ascii_strcasecmp(dot, ".wmf") != 0 &&
           g_ascii_strcasecmp(dot, ".emf") != 0 && g_ascii_strcasecmp(dot, ".pdf") != 0;
}

/* The picture of a frame: a raster image before an SVG one, never a metafile */
static const gchar* frame_picture(const OdpNode* frame) {
    const gchar* svg = NULL;
    for (guint i = 0; i < frame->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(frame->children, i);
        const gchar* href = odp_node_is(n, "draw:image") ? odp_node_attr(n, "xlink:href") : NULL;
        if (!drawable_picture(href)) {
            continue;
        }
        if (g_str_has_suffix(href, ".svg") || g_str_has_suffix(href, ".SVG")) {
            if (svg == NULL) svg = href;
            continue;
        }
        return href;
    }
    return svg;
}

/* ---- Geometry ---- */

/* x' = a x + c y + e, y' = b x + d y + f */
typedef struct {
    gdouble a, b, c, d, e, f;
} Matrix;

static void matrix_then(Matrix* m, const Matrix* op) {
    Matrix r = { op->a * m->a + op->c * m->b, op->b * m->a + op->d * m->b, op->a * m->c + op->c * m->d,
                 op->b * m->c + op->d * m->d, op->a * m->e + op->c * m->f + op->e,
                 op->b * m->e + op->d * m->f + op->f };
    *m = r;
}

static void matrix_apply(const Matrix* m, gdouble x, gdouble y, gdouble* ox, gdouble* oy) {
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

/* Arguments of a transform operation: up to 6 values, lengths in points */
static gint transform_args(const gchar** p, gdouble* v) {
    const gchar* s = *p;
    while (*s == ' ') s++;
    if (*s != '(') return 0;
    s++;
    gint n = 0;
    while (*s != '\0' && *s != ')' && n < 6) {
        while (*s == ' ' || *s == ',') s++;
        gchar* end = NULL;
        gdouble x = g_ascii_strtod(s, &end);
        if (end == s) break;
        const gchar* unit = end;
        while (g_ascii_isalpha(*end)) end++;
        gchar* token = g_strndup(s, (gsize)(end - s));
        v[n++] = unit != end ? odp_length(token, x) : x;
        g_free(token);
        s = end;
        while (*s == ' ' || *s == ',') s++;
    }
    while (*s != '\0' && *s != ')') s++;
    if (*s == ')') s++;
    *p = s;
    return n;
}

/* draw:transform applied after m, operations in their written order */
static void parse_transform(const gchar* t, Matrix* m) {
    const gchar* p = t;
    while (p != NULL && *p != '\0') {
        while (*p == ' ' || *p == ',') p++;
        const gchar* name = p;
        while (g_ascii_isalpha(*p)) p++;
        gsize len = (gsize)(p - name);
        if (len == 0) break;
        gdouble v[6] = { 0, 0, 0, 0, 0, 0 };
        gint n = transform_args(&p, v);
        Matrix op = { 1, 0, 0, 1, 0, 0 };
        if (len == 6 && strncmp(name, "rotate", 6) == 0 && n >= 1) {
            /* Radians, counter-clockwise on the screen */
            op.a = cos(v[0]);
            op.b = -sin(v[0]);
            op.c = sin(v[0]);
            op.d = cos(v[0]);
        } else if (len == 9 && strncmp(name, "translate", 9) == 0 && n >= 1) {
            op.e = v[0];
            op.f = n >= 2 ? v[1] : 0;
        } else if (len == 5 && strncmp(name, "scale", 5) == 0 && n >= 1) {
            op.a = v[0];
            op.d = n >= 2 ? v[1] : v[0];
        } else if (len == 6 && strncmp(name, "matrix", 6) == 0 && n == 6) {
            op = (Matrix){ v[0], v[1], v[2], v[3], v[4], v[5] };
        } else if (len == 4 && strncmp(name, "skew", 4) == 0) {
            continue;
        }
        matrix_then(m, &op);
    }
}

/* Box of a shape from svg:x/y/width/height and draw:transform; FALSE if it
 * has no size */
static gboolean shape_box(const OdpNode* n, SlideShape* s) {
    const gchar* width = odp_node_attr(n, "svg:width");
    const gchar* height = odp_node_attr(n, "svg:height");
    if (width == NULL || height == NULL) {
        return FALSE;
    }
    gdouble w = odp_length(width, 0), h = odp_length(height, 0);
    Matrix m = { 1, 0, 0, 1, odp_length(odp_node_attr(n, "svg:x"), 0), odp_length(odp_node_attr(n, "svg:y"), 0) };
    const gchar* t = odp_node_attr(n, "draw:transform");
    if (t != NULL) {
        parse_transform(t, &m);
    }
    gdouble cx, cy;
    matrix_apply(&m, w / 2, h / 2, &cx, &cy);
    gdouble sx = hypot(m.a, m.b), sy = hypot(m.c, m.d);
    s->w = w * sx;
    s->h = h * sy;
    s->x = cx - s->w / 2;
    s->y = cy - s->h / 2;
    s->rotation = atan2(m.b, m.a) * 180.0 / G_PI;
    if (fabs(s->rotation) < 1e-6) s->rotation = 0;
    if (m.a * m.d - m.b * m.c < 0) s->flip_v = !s->flip_v;
    return TRUE;
}

static void line_box(const OdpNode* n, SlideShape* s) {
    gdouble x1 = odp_length(odp_node_attr(n, "svg:x1"), 0), y1 = odp_length(odp_node_attr(n, "svg:y1"), 0);
    gdouble x2 = odp_length(odp_node_attr(n, "svg:x2"), 0), y2 = odp_length(odp_node_attr(n, "svg:y2"), 0);
    const gchar* t = odp_node_attr(n, "draw:transform");
    if (t != NULL) {
        Matrix m = { 1, 0, 0, 1, 0, 0 };
        parse_transform(t, &m);
        matrix_apply(&m, x1, y1, &x1, &y1);
        matrix_apply(&m, x2, y2, &x2, &y2);
    }
    s->geometry = SLIDE_GEOM_LINE;
    s->x = MIN(x1, x2);
    s->y = MIN(y1, y2);
    s->w = fabs(x2 - x1);
    s->h = fabs(y2 - y1);
    s->flip_h = x2 < x1;
    s->flip_v = y2 < y1;
}

static void set_path(SlideShape* s, GArray* steps, gdouble w, gdouble h) {
    if (steps == NULL) {
        return;
    }
    s->geometry = SLIDE_GEOM_PATH;
    s->path = steps;
    s->path_w = w > 0 ? w : 1;
    s->path_h = h > 0 ? h : 1;
}

/* Path of a polygon, polyline or path element in its viewBox */
static void viewbox_path(const OdpNode* n, SlideShape* s) {
    gdouble box[4];
    if (!odp_geometry_viewbox(odp_node_attr(n, "svg:viewBox"), box)) {
        box[0] = box[1] = 0;
        box[2] = s->w;
        box[3] = s->h;
    }
    GArray* steps = odp_node_is(n, "draw:path")
                        ? odp_geometry_svg_path(odp_node_attr(n, "svg:d"), box[0], box[1])
                        : odp_geometry_points(odp_node_attr(n, "draw:points"), box[0], box[1],
                                              odp_node_is(n, "draw:polygon"));
    set_path(s, steps, box[2], box[3]);
}

/* Whether a path is closed: open paths are not filled */
static gboolean path_closed(const SlideShape* s) {
    if (s->path == NULL) {
        return TRUE;
    }
    for (guint i = 0; i < s->path->len; i++) {
        if (g_array_index(s->path, SlidePathStep, i).op == SLIDE_PATH_CLOSE) {
            return TRUE;
        }
    }
    return FALSE;
}

static void custom_geometry(const OdpNode* geo, SlideShape* s) {
    if (geo == NULL) {
        return;
    }
    if (g_strcmp0(odp_node_attr(geo, "draw:mirror-horizontal"), "true") == 0) s->flip_h = !s->flip_h;
    if (g_strcmp0(odp_node_attr(geo, "draw:mirror-vertical"), "true") == 0) s->flip_v = !s->flip_v;
    const gchar* type = odp_node_attr(geo, "draw:type");
    if (g_strcmp0(type, "rectangle") == 0 || g_strcmp0(type, "ooxml-rect") == 0) {
        return;
    }
    if (g_strcmp0(type, "ellipse") == 0 || g_strcmp0(type, "ooxml-ellipse") == 0) {
        s->geometry = SLIDE_GEOM_ELLIPSE;
        return;
    }
    OdpEnhanced g = { odp_node_attr(geo, "draw:enhanced-path"), odp_node_attr(geo, "draw:modifiers"), NULL, NULL, 0,
                      { 0, 0, 21600, 21600 }, s->w, s->h };
    gdouble box[4];
    if (odp_geometry_viewbox(odp_node_attr(geo, "svg:viewBox"), box)) {
        memcpy(g.box, box, sizeof box);
    }
    GPtrArray* names = g_ptr_array_new();
    GPtrArray* formulas = g_ptr_array_new();
    for (guint i = 0; i < geo->children->len && names->len < MAX_EQUATIONS; i++) {
        const OdpNode* eq = g_ptr_array_index(geo->children, i);
        if (odp_node_is(eq, "draw:equation")) {
            g_ptr_array_add(names, (gpointer)odp_node_attr(eq, "draw:name"));
            g_ptr_array_add(formulas, (gpointer)odp_node_attr(eq, "draw:formula"));
        }
    }
    g.eq_names = (const gchar* const*)names->pdata;
    g.eq_formulas = (const gchar* const*)formulas->pdata;
    g.n_equations = names->len;
    GArray* steps = odp_geometry_enhanced(&g);
    if (steps != NULL) {
        set_path(s, steps, g.box[2], g.box[3]);
    } else if (type != NULL) {
        /* No path: the shapes the model knows */
        static const struct { const gchar* name; SlideGeometry geometry; } TYPES[] = {
            { "round-rectangle", SLIDE_GEOM_ROUND_RECT }, { "isosceles-triangle", SLIDE_GEOM_TRIANGLE },
            { "right-triangle", SLIDE_GEOM_RIGHT_TRIANGLE }, { "diamond", SLIDE_GEOM_DIAMOND },
            { "parallelogram", SLIDE_GEOM_PARALLELOGRAM }, { "hexagon", SLIDE_GEOM_HEXAGON },
            { "right-arrow", SLIDE_GEOM_RIGHT_ARROW }, { "left-arrow", SLIDE_GEOM_LEFT_ARROW },
            { "up-arrow", SLIDE_GEOM_UP_ARROW }, { "down-arrow", SLIDE_GEOM_DOWN_ARROW },
            { "chevron", SLIDE_GEOM_CHEVRON }, { "pentagon-right", SLIDE_GEOM_PENTAGON },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(TYPES); i++) {
            if (strcmp(type, TYPES[i].name) == 0) {
                s->geometry = TYPES[i].geometry;
            }
        }
    }
    g_ptr_array_free(names, TRUE);
    g_ptr_array_free(formulas, TRUE);
}

/* ---- Fill and outline ---- */

typedef struct {
    guint32 color, end;     /* end: gradient, SLIDE_NO_COLOR: solid */
    gdouble angle;
    GBytes* picture;
    gdouble tile_w, tile_h; /* repeated picture */
} Fill;

/* Colour, gradient or picture of a fill described by a cascade; FALSE if
 * it sets no fill. frame: frames and pages have no fill by default. */
static gboolean read_fill(Reader* r, const OdpCascade* c, gboolean frame, Fill* out) {
    guint32* color = &out->color;
    guint32* end = &out->end;
    gdouble* angle = &out->angle;
    out->color = out->end = SLIDE_NO_COLOR;
    out->angle = out->tile_w = out->tile_h = 0;
    out->picture = NULL;
    const gchar* fill = odp_cascade_get(c, "draw:fill");
    const gchar* fill_color = odp_cascade_get(c, "draw:fill-color");
    if (fill == NULL) {
        if (frame || fill_color == NULL) {
            return FALSE;
        }
        fill = "solid";
    }
    if (strcmp(fill, "solid") == 0) {
        *color = odp_color(fill_color, DEFAULT_FILL);
        return TRUE;
    }
    if (strcmp(fill, "gradient") == 0) {
        const gchar* name = odp_cascade_get(c, "draw:fill-gradient-name");
        const OdpGradient* g = name != NULL ? g_hash_table_lookup(r->styles->gradients, name) : NULL;
        if (g == NULL) {
            *color = odp_color(fill_color, DEFAULT_FILL);
            return TRUE;
        }
        if (g->radial) {
            /* Drawn as the average of its colours */
            *color = ((((g->start >> 16) & 0xFF) + ((g->end >> 16) & 0xFF)) / 2) << 16 |
                     ((((g->start >> 8) & 0xFF) + ((g->end >> 8) & 0xFF)) / 2) << 8 |
                     (((g->start & 0xFF) + (g->end & 0xFF)) / 2);
            return TRUE;
        }
        /* Axial: from the edge to the middle colour, on half the length */
        *color = g->start;
        *end = g->end;
        *angle = 90.0 - g->angle;
        return TRUE;
    }
    if (strcmp(fill, "bitmap") == 0) {
        const gchar* name = odp_cascade_get(c, "draw:fill-image-name");
        const gchar* href = name != NULL ? g_hash_table_lookup(r->styles->images, name) : NULL;
        out->picture = drawable_picture(href) ? picture(r, href) : NULL;
        if (g_strcmp0(odp_cascade_get(c, "style:repeat"), "repeat") == 0) {
            out->tile_w = odp_length(odp_cascade_get(c, "draw:fill-image-width"), 0);
            out->tile_h = odp_length(odp_cascade_get(c, "draw:fill-image-height"), 0);
        }
        return out->picture != NULL;
    }
    if (strcmp(fill, "hatch") == 0 && g_strcmp0(odp_cascade_get(c, "draw:fill-hatch-solid"), "true") == 0) {
        *color = odp_color(fill_color, DEFAULT_FILL);
        return TRUE;
    }
    return FALSE;
}

static void apply_graphic(Reader* r, const OdpCascade* c, SlideShape* s, gboolean frame) {
    Fill fill;
    if (read_fill(r, c, frame, &fill)) {
        if (fill.picture != NULL) {
            if (s->picture == NULL) {
                s->picture = fill.picture;
                s->picture_tile_w = fill.tile_w;
                s->picture_tile_h = fill.tile_h;
            } else {
                g_bytes_unref(fill.picture);
            }
        } else {
            s->fill = fill.color;
            s->fill_end = fill.end;
            s->gradient_angle = fill.angle;
        }
    }
    gdouble opacity = odp_percent(odp_cascade_get(c, "draw:opacity"));
    if (opacity >= 0) {
        s->fill_opacity = CLAMP(opacity, 0, 1);
    }
    const gchar* stroke = odp_cascade_get(c, "draw:stroke");
    if (stroke == NULL ? !frame : strcmp(stroke, "none") != 0) {
        s->line = odp_color(odp_cascade_get(c, "svg:stroke-color"), DEFAULT_LINE);
        s->line_dashed = g_strcmp0(stroke, "dash") == 0;
        s->line_width = MAX(odp_length(odp_cascade_get(c, "svg:stroke-width"), 0), 0.5);
        gdouble line_opacity = odp_percent(odp_cascade_get(c, "svg:stroke-opacity"));
        if (line_opacity >= 0) {
            s->line_opacity = CLAMP(line_opacity, 0, 1);
        }
    }
    if (g_strcmp0(odp_cascade_get(c, "draw:shadow"), "visible") == 0) {
        s->shadow = odp_color(odp_cascade_get(c, "draw:shadow-color"), 0x808080);
        s->shadow_dx = odp_length(odp_cascade_get(c, "draw:shadow-offset-x"), 0.2 * CM);
        s->shadow_dy = odp_length(odp_cascade_get(c, "draw:shadow-offset-y"), 0.2 * CM);
        gdouble opacity_shadow = odp_percent(odp_cascade_get(c, "draw:shadow-opacity"));
        s->shadow_opacity = opacity_shadow >= 0 ? opacity_shadow : 1.0;
    }
    const gchar* padding = odp_cascade_get(c, "fo:padding");
    s->inset_left = odp_length(odp_cascade_get(c, "fo:padding-left"), odp_length(padding, 0.25 * CM));
    s->inset_right = odp_length(odp_cascade_get(c, "fo:padding-right"), odp_length(padding, 0.25 * CM));
    s->inset_top = odp_length(odp_cascade_get(c, "fo:padding-top"), odp_length(padding, 0.125 * CM));
    s->inset_bottom = odp_length(odp_cascade_get(c, "fo:padding-bottom"), odp_length(padding, 0.125 * CM));
    const gchar* valign = odp_cascade_get(c, "draw:textarea-vertical-align");
    s->anchor = g_strcmp0(valign, "middle") == 0 ? SLIDE_ANCHOR_MIDDLE
                : g_strcmp0(valign, "bottom") == 0 ? SLIDE_ANCHOR_BOTTOM
                                                   : SLIDE_ANCHOR_TOP;
    if (!frame) {
        /* Only an explicit setting: the default style says no-wrap */
        s->no_wrap = g_strcmp0(odp_cascade_get_explicit(c, "fo:wrap-option"), "no-wrap") == 0;
    }
    const gchar* fit = odp_cascade_get(c, "draw:fit-to-size");
    s->shrink_to_fit = g_strcmp0(odp_cascade_get(c, "style:shrink-to-fit"), "true") == 0 ||
                       g_strcmp0(fit, "shrink-to-fit") == 0 || g_strcmp0(fit, "autofit") == 0;
}

/* ---- Shapes ---- */

static const gchar* shape_class(const OdpNode* n) {
    return odp_node_attr(n, "presentation:class");
}

/* Styles of a shape: its text style, its presentation or graphic style,
 * then the defaults */
static void shape_cascade(Reader* r, const OdpNode* n, OdpCascade* c) {
    c->n = 0;
    odp_cascade_add(c, r->styles, "paragraph", odp_node_attr(n, "draw:text-style-name"));
    const gchar* pres = odp_node_attr(n, "presentation:style-name");
    if (pres != NULL) {
        odp_cascade_add(c, r->styles, "presentation", pres);
    } else if (shape_class(n) != NULL && r->master_name != NULL) {
        /* No style: the master's style of the class */
        const gchar* cls = shape_class(n);
        gchar* name = g_strdup_printf("%s-%s", r->master_name, strcmp(cls, "outline") == 0 ? "outline1" : cls);
        odp_cascade_add(c, r->styles, "presentation", name);
        g_free(name);
    }
    odp_cascade_add(c, r->styles, "graphic", odp_node_attr(n, "draw:style-name"));
    odp_cascade_add_default(c, r->styles, "graphic");
    odp_cascade_add_default(c, r->styles, "paragraph");
}

/* The master's frame of a presentation class, NULL if none */
static const OdpNode* master_frame(const Reader* r, const gchar* cls) {
    if (r->master == NULL || cls == NULL) {
        return NULL;
    }
    const gchar* alt = strcmp(cls, "subtitle") == 0 ? "outline" : strcmp(cls, "outline") == 0 ? "subtitle" : NULL;
    const OdpNode* found = NULL;
    for (guint i = 0; i < r->master->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(r->master->children, i);
        const gchar* c = shape_class(n);
        if (c == NULL) continue;
        if (strcmp(c, cls) == 0) return n;
        if (found == NULL && alt != NULL && strcmp(c, alt) == 0) found = n;
    }
    return found;
}

/* Box of a frame without its own: the master's frame, or Impress' layout */
static void placeholder_box(const Reader* r, const OdpNode* n, SlideShape* s) {
    const OdpNode* m = master_frame(r, shape_class(n));
    if (m != NULL && shape_box(m, s)) {
        return;
    }
    gdouble w = r->deck->width, h = r->deck->height;
    gboolean title = g_strcmp0(shape_class(n), "title") == 0;
    s->x = 0.05 * w;
    s->w = 0.9 * w;
    s->y = title ? 0.04 * h : 0.25 * h;
    s->h = title ? 0.17 * h : 0.68 * h;
}

static guint32 text_background(const Reader* r, const SlideShape* s) {
    return s->fill != SLIDE_NO_COLOR && s->fill_opacity >= 0.5 ? s->fill : r->slide->background;
}

static void shape_text(Reader* r, const OdpNode* n, const OdpCascade* c, SlideShape* s, const OdpNode* container) {
    OdpTextContext ctx = { r->styles, *c, NULL, NULL, r->page_index + 1, r->footer, r->date_time, r->header,
                           text_background(r, s), 0 };
    const gchar* cls = shape_class(n);
    gchar* prefix = NULL;
    if (g_strcmp0(cls, "outline") == 0 && r->master_name != NULL) {
        prefix = g_strdup_printf("%s-outline", r->master_name);
        ctx.outline_prefix = prefix;
    }
    ctx.list = odp_styles_style_list(r->styles, "presentation", odp_node_attr(n, "presentation:style-name"));
    if (ctx.list == NULL) {
        ctx.list = odp_styles_style_list(r->styles, "graphic", odp_node_attr(n, "draw:style-name"));
    }
    /* Sizes of Impress' default styles when the file has none */
    gboolean body = g_strcmp0(cls, "outline") == 0 || g_strcmp0(cls, "subtitle") == 0;
    ctx.default_size = g_strcmp0(cls, "title") == 0 ? 44 : body ? 32 : 18;
    odp_text_paragraphs(&ctx, container, s->paragraphs);
    g_free(prefix);
}

static void add_shapes(Reader* r, const OdpNode* parent, gint depth);

static SlideShape* frame_shape(Reader* r, const OdpNode* n) {
    SlideShape* s = slide_shape_new();
    if (!shape_box(n, s)) {
        placeholder_box(r, n, s);
    }
    OdpCascade c;
    shape_cascade(r, n, &c);
    apply_graphic(r, &c, s, TRUE);
    const OdpNode* box = odp_node_child(n, "draw:text-box");
    const OdpNode* table = odp_node_child(n, "table:table");
    if (table != NULL) {
        /* A table paints its cells, not the frame */
        s->fill = s->line = s->shadow = SLIDE_NO_COLOR;
        g_clear_pointer(&s->picture, g_bytes_unref);
        OdpTextContext ctx = { r->styles, c, NULL, NULL, r->page_index + 1, r->footer, r->date_time, r->header,
                               text_background(r, s), 0 };
        s->table = odp_text_table(&ctx, table, s->w, s->h);
    } else if (box != NULL) {
        shape_text(r, n, &c, s, box);
    } else {
        const gchar* href = frame_picture(n);
        GBytes* pic = href != NULL ? picture(r, href) : NULL;
        if (pic != NULL) {
            if (s->picture != NULL) g_bytes_unref(s->picture);
            s->picture = pic;
        }
    }
    return s;
}

static SlideShape* drawing_shape(Reader* r, const OdpNode* n) {
    SlideShape* s = slide_shape_new();
    gboolean line = odp_node_is(n, "draw:line") || odp_node_is(n, "draw:connector");
    if (line) {
        line_box(n, s);
    } else if (!shape_box(n, s)) {
        slide_shape_free(s);
        return NULL;
    }
    if (odp_node_is(n, "draw:ellipse") || odp_node_is(n, "draw:circle")) {
        s->geometry = SLIDE_GEOM_ELLIPSE;
    } else if (odp_node_is(n, "draw:rect") && odp_node_attr(n, "draw:corner-radius") != NULL &&
               odp_length(odp_node_attr(n, "draw:corner-radius"), 0) > 0) {
        s->geometry = SLIDE_GEOM_ROUND_RECT;
    } else if (odp_node_is(n, "draw:polygon") || odp_node_is(n, "draw:polyline") || odp_node_is(n, "draw:path")) {
        viewbox_path(n, s);
    } else if (odp_node_is(n, "draw:custom-shape")) {
        custom_geometry(odp_node_child(n, "draw:enhanced-geometry"), s);
    }
    OdpCascade c;
    shape_cascade(r, n, &c);
    apply_graphic(r, &c, s, FALSE);
    if (line || odp_node_is(n, "draw:polyline") || (odp_node_is(n, "draw:path") && !path_closed(s))) {
        s->fill = SLIDE_NO_COLOR;
        if (s->picture != NULL) {
            g_bytes_unref(s->picture);
            s->picture = NULL;
        }
    }
    shape_text(r, n, &c, s, n);
    return s;
}

static gboolean field_hidden(const Reader* r, const gchar* cls) {
    if (cls == NULL) return FALSE;
    if (strcmp(cls, "footer") == 0) return !r->show_footer;
    if (strcmp(cls, "date-time") == 0) return !r->show_date_time;
    if (strcmp(cls, "header") == 0) return !r->show_header;
    if (strcmp(cls, "page-number") == 0) return !r->show_page_number;
    return FALSE;
}

static void add_shape(Reader* r, const OdpNode* n, gint depth) {
    if (r->slide->shapes->len >= MAX_SHAPES || n->name == NULL) {
        return;
    }
    if (odp_node_is(n, "draw:g") || odp_node_is(n, "draw:a")) {
        if (depth < MAX_GROUP_DEPTH) {
            add_shapes(r, n, depth + 1);
        }
        return;
    }
    /* Placeholders show nothing in a slide show; a master's fields only
     * when the page shows them */
    if (g_strcmp0(odp_node_attr(n, "presentation:placeholder"), "true") == 0) {
        return;
    }
    if (r->in_master && field_hidden(r, shape_class(n))) {
        return;
    }
    SlideShape* s = NULL;
    if (odp_node_is(n, "draw:frame")) {
        s = frame_shape(r, n);
    } else if (odp_node_is(n, "draw:rect") || odp_node_is(n, "draw:ellipse") || odp_node_is(n, "draw:circle") ||
               odp_node_is(n, "draw:line") || odp_node_is(n, "draw:connector") || odp_node_is(n, "draw:polygon") ||
               odp_node_is(n, "draw:polyline") || odp_node_is(n, "draw:path") ||
               odp_node_is(n, "draw:custom-shape") || odp_node_is(n, "draw:caption")) {
        s = drawing_shape(r, n);
    }
    if (s != NULL) {
        g_ptr_array_add(r->slide->shapes, s);
    }
}

static void add_shapes(Reader* r, const OdpNode* parent, gint depth) {
    for (guint i = 0; i < parent->children->len; i++) {
        add_shape(r, g_ptr_array_index(parent->children, i), depth);
    }
}

/* ---- Pages ---- */

static void page_background(Reader* r, Slide* slide, const OdpNode* page) {
    OdpCascade c = { { 0 }, 0 };
    odp_cascade_add(&c, r->styles, "drawing-page", odp_node_attr(page, "draw:style-name"));
    const gchar* kind = odp_cascade_get(&c, "draw:fill");
    if ((kind == NULL || strcmp(kind, "none") == 0) &&
        g_strcmp0(odp_cascade_get(&c, "presentation:background-visible"), "false") != 0) {
        c.n = 0;
        odp_cascade_add(&c, r->styles, "drawing-page", odp_node_attr(r->master, "draw:style-name"));
    }
    Fill fill;
    if (read_fill(r, &c, TRUE, &fill)) {
        if (fill.picture != NULL) {
            slide->background_picture = fill.picture;
            slide->background_tile_w = fill.tile_w;
            slide->background_tile_h = fill.tile_h;
        } else {
            slide->background = fill.color;
            slide->background_end = fill.end;
        }
    }
}

/* Whether the page (or its master) shows a field; default otherwise */
static gboolean page_shows(Reader* r, const OdpNode* page, const gchar* key, gboolean fallback) {
    OdpCascade c = { { 0 }, 0 };
    odp_cascade_add(&c, r->styles, "drawing-page", odp_node_attr(page, "draw:style-name"));
    odp_cascade_add(&c, r->styles, "drawing-page", odp_node_attr(r->master, "draw:style-name"));
    const gchar* v = odp_cascade_get(&c, key);
    return v != NULL ? strcmp(v, "true") == 0 : fallback;
}

static void read_decls(Reader* r, const OdpNode* presentation) {
    for (guint i = 0; i < presentation->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(presentation->children, i);
        const gchar* kind = odp_node_is(n, "presentation:footer-decl")      ? "footer"
                            : odp_node_is(n, "presentation:header-decl")    ? "header"
                            : odp_node_is(n, "presentation:date-time-decl") ? "date-time"
                                                                            : NULL;
        const gchar* name = odp_node_attr(n, "presentation:name");
        if (kind == NULL || name == NULL) {
            continue;
        }
        gchar* text;
        if (strcmp(kind, "date-time") == 0 && g_strcmp0(odp_node_attr(n, "presentation:source"), "fixed") != 0) {
            GDateTime* now = g_date_time_new_now_local();
            text = g_date_time_format(now, "%x");
            g_date_time_unref(now);
        } else {
            text = odp_node_text(n);
        }
        g_hash_table_replace(r->decls, g_strconcat(kind, "/", name, NULL), text);
    }
}

static const gchar* decl(Reader* r, const gchar* kind, const gchar* name) {
    if (name == NULL) {
        return NULL;
    }
    gchar* key = g_strconcat(kind, "/", name, NULL);
    const gchar* text = g_hash_table_lookup(r->decls, key);
    g_free(key);
    return text;
}

static gchar* page_notes(const OdpNode* page) {
    const OdpNode* notes = odp_node_child(page, "presentation:notes");
    if (notes == NULL) {
        return NULL;
    }
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < notes->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(notes->children, i);
        if (odp_node_is(n, "draw:frame") && (shape_class(n) == NULL || strcmp(shape_class(n), "notes") == 0)) {
            gchar* text = odp_node_text(odp_node_child(n, "draw:text-box"));
            if (text[0] != '\0') {
                if (out->len > 0) g_string_append_c(out, '\n');
                g_string_append(out, text);
            }
            g_free(text);
        }
    }
    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

static gchar* page_title(const OdpNode* page) {
    for (guint i = 0; i < page->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(page->children, i);
        if (odp_node_is(n, "draw:frame") && g_strcmp0(shape_class(n), "title") == 0) {
            gchar* text = odp_node_text(odp_node_child(n, "draw:text-box"));
            if (text[0] != '\0') {
                return text;
            }
            g_free(text);
        }
    }
    return NULL;
}

static Slide* read_page(Reader* r, const OdpNode* page) {
    Slide* slide = slide_new();
    r->slide = slide;
    r->master_name = odp_node_attr(page, "draw:master-page-name");
    if (r->master_name == NULL || !g_hash_table_contains(r->styles->masters, r->master_name)) {
        r->master_name = r->styles->first_master;
    }
    r->master = r->master_name != NULL ? g_hash_table_lookup(r->styles->masters, r->master_name) : NULL;
    page_background(r, slide, page);
    r->show_footer = page_shows(r, page, "presentation:display-footer", TRUE);
    r->show_date_time = page_shows(r, page, "presentation:display-date-time", TRUE);
    r->show_header = page_shows(r, page, "presentation:display-header", TRUE);
    r->show_page_number = page_shows(r, page, "presentation:display-page-number", FALSE);
    r->footer = decl(r, "footer", odp_node_attr(page, "presentation:use-footer-name"));
    r->date_time = decl(r, "date-time", odp_node_attr(page, "presentation:use-date-time-name"));
    r->header = decl(r, "header", odp_node_attr(page, "presentation:use-header-name"));
    if (r->master != NULL && page_shows(r, page, "presentation:background-objects-visible", TRUE)) {
        r->in_master = TRUE;
        add_shapes(r, r->master, 0);
        r->in_master = FALSE;
    }
    add_shapes(r, page, 0);
    slide->title = page_title(page);
    slide->notes = page_notes(page);
    return slide;
}

static SlideDeck* read_deck(const gchar* path, guint picture_pages, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    if (!zip_reader_has(zip, "content.xml")) {
        zip_reader_free(zip);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an OpenDocument file (no content.xml)"));
        return NULL;
    }
    OdpNode* content = odp_node_parse(zip, "content.xml", MAX_PART, FALSE, error);
    if (content == NULL) {
        zip_reader_free(zip);
        return NULL;
    }
    const OdpNode* body = odp_node_child(content, "office:body");
    const OdpNode* presentation = odp_node_child(body, "office:presentation");
    if (presentation == NULL) {
        odp_node_free(content);
        zip_reader_free(zip);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an OpenDocument presentation"));
        return NULL;
    }
    OdpNode* styles_root = odp_node_parse(zip, "styles.xml", MAX_PART, TRUE, NULL);
    Reader r;
    memset(&r, 0, sizeof r);
    r.zip = zip;
    r.styles = odp_styles_new();
    r.pictures = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
    r.decls = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    r.picture_pages = picture_pages;
    odp_styles_add(r.styles, styles_root);
    odp_styles_add(r.styles, content);
    read_decls(&r, presentation);

    /* Page size of the first page's master */
    const OdpNode* first = odp_node_child(presentation, "draw:page");
    const gchar* master = odp_node_attr(first, "draw:master-page-name");
    gdouble width = 28 * CM, height = 15.75 * CM;
    if (!odp_styles_page_size(r.styles, master, &width, &height)) {
        odp_styles_page_size(r.styles, r.styles->first_master, &width, &height);
    }
    r.deck = slide_deck_new(width, height);
    OdpCascade defaults = { { 0 }, 0 };
    odp_cascade_add_default(&defaults, r.styles, "graphic");
    const gchar* face = odp_cascade_get(&defaults, "style:font-name");
    const gchar* family = odp_styles_font(r.styles, face);
    r.deck->font = family != NULL ? g_strdup(family) : odp_first_family(odp_cascade_get(&defaults, "fo:font-family"));

    for (guint i = 0; i < presentation->children->len && r.deck->slides->len < MAX_SLIDES; i++) {
        const OdpNode* page = g_ptr_array_index(presentation->children, i);
        if (!odp_node_is(page, "draw:page")) {
            continue;
        }
        r.page_index = r.deck->slides->len;
        g_ptr_array_add(r.deck->slides, read_page(&r, page));
    }
    SlideDeck* deck = r.deck;
    g_hash_table_destroy(r.pictures);
    g_hash_table_destroy(r.decls);
    odp_styles_free(r.styles);
    odp_node_free(styles_root);
    odp_node_free(content);
    zip_reader_free(zip);
    return deck;
}

SlideDeck* odp_read_deck(const gchar* path, GError** error) {
    return read_deck(path, G_MAXUINT, error);
}

PreviewDoc* odp_file_preview(const gchar* path, const gchar* name, const gchar* size_text, guint* n_slides,
                             gdouble zoom, guint max_drawn, GError** error) {
    /* Pictures of the slides drawn only */
    SlideDeck* deck = read_deck(path, max_drawn, error);
    if (deck == NULL) {
        return NULL;
    }
    guint n = deck->slides->len;
    if (n_slides != NULL) {
        *n_slides = n;
    }
    GString* subtitle = g_string_new(_("OpenDocument presentation"));
    g_string_append(subtitle, " · ");
    g_string_append_printf(subtitle, ngettext("%u slide", "%u slides", n), n);
    if (size_text != NULL) {
        g_string_append_printf(subtitle, " · %s", size_text);
    }
    PreviewDoc* doc = slide_deck_preview(deck, name != NULL ? name : "", subtitle->str, max_drawn, zoom);
    g_string_free(subtitle, TRUE);
    slide_deck_free(deck);
    return doc;
}
