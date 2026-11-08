/* Slides drawn like PowerPoint (see slide_render.h) */
#include "office/slide_render.h"
#include "core/i18n.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#define RENDER_WIDTH 960       /* pixels of a slide picture */
#define DEFAULT_FONT "sans"    /* fallback family of Pango */
#define ROUND_RECT_RATIO 0.1667

/* ---- Model ---- */

SlideDeck* slide_deck_new(gdouble width, gdouble height) {
    SlideDeck* deck = g_new0(SlideDeck, 1);
    deck->width = width > 0 ? width : 720;
    deck->height = height > 0 ? height : 540;
    deck->slides = g_ptr_array_new_with_free_func((GDestroyNotify)slide_free);
    return deck;
}

void slide_deck_free(SlideDeck* deck) {
    if (deck == NULL) {
        return;
    }
    g_ptr_array_free(deck->slides, TRUE);
    g_free(deck->font);
    g_free(deck);
}

Slide* slide_new(void) {
    Slide* s = g_new0(Slide, 1);
    s->background = 0xFFFFFF;
    s->background_end = SLIDE_NO_COLOR;
    s->shapes = g_ptr_array_new_with_free_func((GDestroyNotify)slide_shape_free);
    return s;
}

void slide_free(Slide* slide) {
    if (slide == NULL) {
        return;
    }
    g_ptr_array_free(slide->shapes, TRUE);
    if (slide->background_picture != NULL) {
        g_bytes_unref(slide->background_picture);
    }
    g_free(slide->title);
    g_free(slide->notes);
    g_free(slide);
}

SlideShape* slide_shape_new(void) {
    SlideShape* s = g_new0(SlideShape, 1);
    s->fill = s->fill_end = s->line = SLIDE_NO_COLOR;
    s->line_width = 0.75;
    s->paragraphs = g_ptr_array_new_with_free_func((GDestroyNotify)slide_paragraph_free);
    s->inset_left = s->inset_right = 7.2;
    s->inset_top = s->inset_bottom = 3.6;
    s->font_scale = 1.0;
    return s;
}

void slide_shape_free(SlideShape* shape) {
    if (shape == NULL) {
        return;
    }
    g_ptr_array_free(shape->paragraphs, TRUE);
    if (shape->path != NULL) {
        g_array_free(shape->path, TRUE);
    }
    if (shape->picture != NULL) {
        g_bytes_unref(shape->picture);
    }
    slide_table_free(shape->table);
    g_free(shape);
}

static void run_free(gpointer data) {
    SlideRun* r = data;
    g_free(r->text);
    g_free(r->font);
    g_free(r);
}

SlideParagraph* slide_paragraph_new(void) {
    SlideParagraph* p = g_new0(SlideParagraph, 1);
    p->runs = g_ptr_array_new_with_free_func(run_free);
    p->bullet_color = SLIDE_NO_COLOR;
    p->size = 18;
    return p;
}

void slide_paragraph_free(SlideParagraph* para) {
    if (para == NULL) {
        return;
    }
    g_ptr_array_free(para->runs, TRUE);
    g_free(para->bullet);
    g_free(para->bullet_font);
    g_free(para);
}

SlideRun* slide_paragraph_add_run(SlideParagraph* para, const gchar* text, gssize len) {
    SlideRun* r = g_new0(SlideRun, 1);
    r->text = g_utf8_make_valid(text != NULL ? text : "", len);
    r->size = para->size;
    g_ptr_array_add(para->runs, r);
    return r;
}

SlideTable* slide_table_new(guint rows, guint cols) {
    SlideTable* t = g_new0(SlideTable, 1);
    t->rows = rows;
    t->cols = cols;
    t->col_widths = g_new0(gdouble, MAX(cols, 1));
    t->row_heights = g_new0(gdouble, MAX(rows, 1));
    t->cells = g_new0(SlideCell, MAX(rows * cols, 1));
    for (guint i = 0; i < rows * cols; i++) {
        t->cells[i].paragraphs = g_ptr_array_new_with_free_func((GDestroyNotify)slide_paragraph_free);
        t->cells[i].fill = SLIDE_NO_COLOR;
        t->cells[i].col_span = t->cells[i].row_span = 1;
    }
    t->border = 0x000000;
    return t;
}

void slide_table_free(SlideTable* table) {
    if (table == NULL) {
        return;
    }
    for (guint i = 0; i < table->rows * table->cols; i++) {
        g_ptr_array_free(table->cells[i].paragraphs, TRUE);
    }
    g_free(table->cells);
    g_free(table->col_widths);
    g_free(table->row_heights);
    g_free(table);
}

SlideCell* slide_table_cell(SlideTable* table, guint row, guint col) {
    return row < table->rows && col < table->cols ? &table->cells[row * table->cols + col] : NULL;
}

static void append_paragraphs_text(GString* out, const GPtrArray* paragraphs) {
    for (guint i = 0; i < paragraphs->len; i++) {
        const SlideParagraph* p = g_ptr_array_index(paragraphs, i);
        GString* line = g_string_new(NULL);
        for (guint k = 0; k < p->runs->len; k++) {
            g_string_append(line, ((SlideRun*)g_ptr_array_index(p->runs, k))->text);
        }
        g_strstrip(line->str);
        if (line->str[0] != '\0') {
            if (out->len > 0) {
                g_string_append_c(out, '\n');
            }
            g_string_append(out, line->str);
        }
        g_string_free(line, TRUE);
    }
}

gchar* slide_shape_text(const SlideShape* shape) {
    GString* out = g_string_new(NULL);
    append_paragraphs_text(out, shape->paragraphs);
    if (shape->table != NULL) {
        for (guint i = 0; i < shape->table->rows * shape->table->cols; i++) {
            append_paragraphs_text(out, shape->table->cells[i].paragraphs);
        }
    }
    return g_string_free(out, FALSE);
}

/* ---- Drawing ---- */

static void set_color(cairo_t* cr, guint32 rgb) {
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0);
}

/* Linear gradient over the box (x, y, w, h), angle in degrees */
static void set_gradient(cairo_t* cr, guint32 from, guint32 to, gdouble angle, gdouble x, gdouble y, gdouble w,
                         gdouble h) {
    gdouble a = angle * G_PI / 180.0;
    gdouble cx = x + w / 2, cy = y + h / 2;
    gdouble dx = cos(a) * w / 2, dy = sin(a) * h / 2;
    cairo_pattern_t* pat = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy);
    cairo_pattern_add_color_stop_rgb(pat, 0, ((from >> 16) & 0xFF) / 255.0, ((from >> 8) & 0xFF) / 255.0,
                                     (from & 0xFF) / 255.0);
    cairo_pattern_add_color_stop_rgb(pat, 1, ((to >> 16) & 0xFF) / 255.0, ((to >> 8) & 0xFF) / 255.0,
                                     (to & 0xFF) / 255.0);
    cairo_set_source(cr, pat);
    cairo_pattern_destroy(pat);
}

/* Cairo surface of an encoded picture (premultiplied ARGB), NULL if the
 * format cannot be decoded */
static cairo_surface_t* picture_surface(GBytes* data) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gsize len = 0;
    const guchar* bytes = g_bytes_get_data(data, &len);
    gboolean ok = gdk_pixbuf_loader_write(loader, bytes, len, NULL);
    ok = gdk_pixbuf_loader_close(loader, NULL) && ok;
    GdkPixbuf* pix = ok ? gdk_pixbuf_loader_get_pixbuf(loader) : NULL;
    cairo_surface_t* surface = NULL;
    if (pix != NULL) {
        gint w = gdk_pixbuf_get_width(pix), h = gdk_pixbuf_get_height(pix);
        gint n = gdk_pixbuf_get_n_channels(pix), stride = gdk_pixbuf_get_rowstride(pix);
        gboolean alpha = gdk_pixbuf_get_has_alpha(pix);
        const guchar* src = gdk_pixbuf_read_pixels(pix);
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        guchar* dst = cairo_image_surface_get_data(surface);
        gint dstride = cairo_image_surface_get_stride(surface);
        for (gint y = 0; y < h; y++) {
            const guchar* s = src + (gsize)y * stride;
            guint32* d = (guint32*)(dst + (gsize)y * dstride);
            for (gint x = 0; x < w; x++, s += n) {
                guint a = alpha ? s[3] : 255;
                guint r = s[0] * a / 255, g = s[1] * a / 255, b = s[2] * a / 255;
                d[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(surface);
    }
    g_object_unref(loader);
    return surface;
}

static void draw_picture(cairo_t* cr, GBytes* data, gdouble x, gdouble y, gdouble w, gdouble h) {
    cairo_surface_t* surface = picture_surface(data);
    if (surface == NULL || w <= 0 || h <= 0) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return;
    }
    gint pw = cairo_image_surface_get_width(surface), ph = cairo_image_surface_get_height(surface);
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w / pw, h / ph);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(surface);
}

/* Outline of a geometry in the box (0, 0, w, h) */
static void geometry_path(cairo_t* cr, const SlideShape* s, gdouble w, gdouble h) {
    gdouble m = MIN(w, h);
    switch (s->geometry) {
    case SLIDE_GEOM_ROUND_RECT: {
        gdouble r = m * ROUND_RECT_RATIO;
        cairo_new_sub_path(cr);
        cairo_arc(cr, w - r, r, r, -G_PI / 2, 0);
        cairo_arc(cr, w - r, h - r, r, 0, G_PI / 2);
        cairo_arc(cr, r, h - r, r, G_PI / 2, G_PI);
        cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
        break;
    }
    case SLIDE_GEOM_ELLIPSE:
        if (w > 0 && h > 0) {
            cairo_save(cr);
            cairo_translate(cr, w / 2, h / 2);
            cairo_scale(cr, w / 2, h / 2);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
            cairo_restore(cr);
        }
        break;
    case SLIDE_GEOM_LINE:
        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, w, h);
        break;
    case SLIDE_GEOM_TRIANGLE:
        cairo_move_to(cr, w / 2, 0);
        cairo_line_to(cr, w, h);
        cairo_line_to(cr, 0, h);
        cairo_close_path(cr);
        break;
    case SLIDE_GEOM_RIGHT_TRIANGLE:
        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, w, h);
        cairo_line_to(cr, 0, h);
        cairo_close_path(cr);
        break;
    case SLIDE_GEOM_DIAMOND:
        cairo_move_to(cr, w / 2, 0);
        cairo_line_to(cr, w, h / 2);
        cairo_line_to(cr, w / 2, h);
        cairo_line_to(cr, 0, h / 2);
        cairo_close_path(cr);
        break;
    case SLIDE_GEOM_PARALLELOGRAM:
        cairo_move_to(cr, m * 0.25, 0);
        cairo_line_to(cr, w, 0);
        cairo_line_to(cr, w - m * 0.25, h);
        cairo_line_to(cr, 0, h);
        cairo_close_path(cr);
        break;
    case SLIDE_GEOM_HEXAGON:
        cairo_move_to(cr, m * 0.25, 0);
        cairo_line_to(cr, w - m * 0.25, 0);
        cairo_line_to(cr, w, h / 2);
        cairo_line_to(cr, w - m * 0.25, h);
        cairo_line_to(cr, m * 0.25, h);
        cairo_line_to(cr, 0, h / 2);
        cairo_close_path(cr);
        break;
    case SLIDE_GEOM_RIGHT_ARROW:
    case SLIDE_GEOM_LEFT_ARROW: {
        gdouble head = MIN(m * 0.5, w), t = h * 0.25;
        gboolean right = s->geometry == SLIDE_GEOM_RIGHT_ARROW;
        gdouble pts[7][2] = { { 0, t }, { w - head, t }, { w - head, 0 }, { w, h / 2 },
                              { w - head, h }, { w - head, h - t }, { 0, h - t } };
        for (gint i = 0; i < 7; i++) {
            gdouble px = right ? pts[i][0] : w - pts[i][0];
            i == 0 ? cairo_move_to(cr, px, pts[i][1]) : cairo_line_to(cr, px, pts[i][1]);
        }
        cairo_close_path(cr);
        break;
    }
    case SLIDE_GEOM_UP_ARROW:
    case SLIDE_GEOM_DOWN_ARROW: {
        gdouble head = MIN(m * 0.5, h), t = w * 0.25;
        gboolean down = s->geometry == SLIDE_GEOM_DOWN_ARROW;
        gdouble pts[7][2] = { { t, h }, { t, head }, { 0, head }, { w / 2, 0 },
                              { w, head }, { w - t, head }, { w - t, h } };
        for (gint i = 0; i < 7; i++) {
            gdouble py = down ? h - pts[i][1] : pts[i][1];
            i == 0 ? cairo_move_to(cr, pts[i][0], py) : cairo_line_to(cr, pts[i][0], py);
        }
        cairo_close_path(cr);
        break;
    }
    case SLIDE_GEOM_CHEVRON:
    case SLIDE_GEOM_PENTAGON: {
        gdouble tip = MIN(m * 0.5, w);
        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, w - tip, 0);
        cairo_line_to(cr, w, h / 2);
        cairo_line_to(cr, w - tip, h);
        cairo_line_to(cr, 0, h);
        if (s->geometry == SLIDE_GEOM_CHEVRON) {
            cairo_line_to(cr, tip, h / 2);
        }
        cairo_close_path(cr);
        break;
    }
    case SLIDE_GEOM_PATH:
        if (s->path != NULL && s->path_w > 0 && s->path_h > 0) {
            gdouble sx = w / s->path_w, sy = h / s->path_h;
            for (guint i = 0; i < s->path->len; i++) {
                const SlidePathStep* st = &g_array_index(s->path, SlidePathStep, i);
                switch (st->op) {
                case SLIDE_PATH_MOVE: cairo_move_to(cr, st->x[0] * sx, st->y[0] * sy); break;
                case SLIDE_PATH_LINE: cairo_line_to(cr, st->x[0] * sx, st->y[0] * sy); break;
                case SLIDE_PATH_CURVE:
                    cairo_curve_to(cr, st->x[0] * sx, st->y[0] * sy, st->x[1] * sx, st->y[1] * sy,
                                   st->x[2] * sx, st->y[2] * sy);
                    break;
                case SLIDE_PATH_CLOSE: cairo_close_path(cr); break;
                }
            }
        }
        break;
    case SLIDE_GEOM_RECT:
    default:
        cairo_rectangle(cr, 0, 0, w, h);
        break;
    }
}

/* ---- Text ---- */

static PangoAlignment pango_align(SlideAlign a) {
    return a == SLIDE_ALIGN_CENTER ? PANGO_ALIGN_CENTER : a == SLIDE_ALIGN_RIGHT ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
}

static void add_attr(PangoAttrList* list, PangoAttribute* attr, guint start, guint end) {
    attr->start_index = start;
    attr->end_index = end;
    pango_attr_list_insert(list, attr);
}

static void add_color_attr(PangoAttrList* list, guint32 rgb, guint start, guint end) {
    add_attr(list, pango_attr_foreground_new((guint16)(((rgb >> 16) & 0xFF) * 257), (guint16)(((rgb >> 8) & 0xFF) * 257),
                                             (guint16)((rgb & 0xFF) * 257)), start, end);
}

/* Layout of a paragraph width wide (pixels); scale: pixels per point */
static PangoLayout* paragraph_layout(cairo_t* cr, const SlideDeck* deck, const SlideParagraph* p, gdouble width,
                                     gdouble scale, gdouble font_scale, gboolean wrap, gdouble* x_offset) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    GString* text = g_string_new(NULL);
    PangoAttrList* attrs = pango_attr_list_new();
    const gchar* deck_font = deck->font != NULL ? deck->font : DEFAULT_FONT;
    const SlideRun* first = p->runs->len > 0 ? g_ptr_array_index(p->runs, 0) : NULL;
    gdouble first_size = first != NULL ? first->size : p->size;
    gdouble left = p->left * scale, indent = p->indent * scale;
    gboolean has_text = FALSE;
    for (guint i = 0; i < p->runs->len && !has_text; i++) {
        has_text = ((SlideRun*)g_ptr_array_index(p->runs, i))->text[0] != '\0';
    }

    /* Bullet, then a tab to the text margin */
    if (p->bullet != NULL && has_text) {
        guint start = (guint)text->len;
        g_string_append(text, p->bullet);
        guint end = (guint)text->len;
        gdouble size = first_size * (p->bullet_scale > 0 ? p->bullet_scale : 1.0) * font_scale * scale;
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, p->bullet_font != NULL ? p->bullet_font
                                              : first != NULL && first->font != NULL ? first->font : deck_font);
        pango_font_description_set_absolute_size(fd, MAX(size, 1.0) * PANGO_SCALE);
        add_attr(attrs, pango_attr_font_desc_new(fd), start, end);
        pango_font_description_free(fd);
        guint32 color = p->bullet_color != SLIDE_NO_COLOR ? p->bullet_color : first != NULL ? first->color : 0;
        add_color_attr(attrs, color, start, end);
        g_string_append_c(text, indent < 0 ? '\t' : ' ');
        if (indent < 0) {
            PangoTabArray* tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT, (gint)(-indent * PANGO_SCALE));
            pango_layout_set_tabs(layout, tabs);
            pango_tab_array_free(tabs);
        }
    }
    for (guint i = 0; i < p->runs->len; i++) {
        const SlideRun* r = g_ptr_array_index(p->runs, i);
        guint start = (guint)text->len;
        g_string_append(text, r->text);
        guint end = (guint)text->len;
        if (end == start) {
            continue;
        }
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, r->font != NULL ? r->font : deck_font);
        gdouble size = r->size * font_scale * scale * (r->baseline != 0 ? 0.7 : 1.0);
        pango_font_description_set_absolute_size(fd, MAX(size, 1.0) * PANGO_SCALE);
        pango_font_description_set_weight(fd, r->bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        pango_font_description_set_style(fd, r->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
        add_attr(attrs, pango_attr_font_desc_new(fd), start, end);
        pango_font_description_free(fd);
        add_color_attr(attrs, r->color, start, end);
        if (r->underline) {
            add_attr(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), start, end);
        }
        if (r->strike) {
            add_attr(attrs, pango_attr_strikethrough_new(TRUE), start, end);
        }
        if (r->baseline != 0) {
            add_attr(attrs, pango_attr_rise_new((gint)(r->baseline * r->size * font_scale * scale * 0.33 * PANGO_SCALE)),
                     start, end);
        }
    }
    if (text->len == 0) {
        /* An empty paragraph keeps the height of a line */
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, deck_font);
        pango_font_description_set_absolute_size(fd, MAX(p->size * font_scale * scale, 1.0) * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
    }
    pango_layout_set_text(layout, text->str, (gint)text->len);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    g_string_free(text, TRUE);

    gdouble start_x = indent < 0 ? MAX(left + indent, 0) : left;
    *x_offset = start_x;
    if (wrap) {
        pango_layout_set_width(layout, (gint)(MAX(width - start_x, 1.0) * PANGO_SCALE));
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_alignment(layout, pango_align(p->align));
    pango_layout_set_justify(layout, p->align == SLIDE_ALIGN_JUSTIFY);
    pango_layout_set_indent(layout, (gint)((indent < 0 ? MAX(indent, -left) : indent) * PANGO_SCALE));
    if (p->line_spacing > 0 && fabs(p->line_spacing - 1.0) > 0.01) {
        pango_layout_set_line_spacing(layout, (float)p->line_spacing);
    }
    return layout;
}

/* Draw paragraphs in the box (pixels), anchored */
static void draw_paragraphs(cairo_t* cr, const SlideDeck* deck, const GPtrArray* paragraphs, gdouble x, gdouble y,
                            gdouble w, gdouble h, SlideAnchor anchor, gdouble scale, gdouble font_scale,
                            gboolean wrap) {
    if (paragraphs->len == 0) {
        return;
    }
    PangoLayout** layouts = g_new0(PangoLayout*, paragraphs->len);
    gdouble* offsets = g_new0(gdouble, paragraphs->len);
    gdouble total = 0;
    for (guint i = 0; i < paragraphs->len; i++) {
        const SlideParagraph* p = g_ptr_array_index(paragraphs, i);
        layouts[i] = paragraph_layout(cr, deck, p, w, scale, font_scale, wrap, &offsets[i]);
        gint lh = 0;
        pango_layout_get_pixel_size(layouts[i], NULL, &lh);
        total += lh + (i > 0 ? p->space_before * scale : 0) + (i + 1 < paragraphs->len ? p->space_after * scale : 0);
    }
    gdouble top = anchor == SLIDE_ANCHOR_MIDDLE ? y + (h - total) / 2 : anchor == SLIDE_ANCHOR_BOTTOM ? y + h - total : y;
    gdouble cy = top;
    for (guint i = 0; i < paragraphs->len; i++) {
        const SlideParagraph* p = g_ptr_array_index(paragraphs, i);
        if (i > 0) {
            cy += p->space_before * scale;
        }
        gint lw = 0, lh = 0;
        pango_layout_get_pixel_size(layouts[i], &lw, &lh);
        gdouble lx = x + offsets[i];
        if (!wrap) {
            /* Unwrapped text is aligned in the box by hand */
            gdouble room = w - offsets[i] - lw;
            lx += p->align == SLIDE_ALIGN_CENTER ? room / 2 : p->align == SLIDE_ALIGN_RIGHT ? room : 0;
        }
        cairo_move_to(cr, lx, cy);
        pango_cairo_show_layout(cr, layouts[i]);
        cy += lh + p->space_after * scale;
        g_object_unref(layouts[i]);
    }
    g_free(layouts);
    g_free(offsets);
}

static void draw_table(cairo_t* cr, const SlideDeck* deck, const SlideTable* t, gdouble x, gdouble y, gdouble scale) {
    gdouble* xs = g_new0(gdouble, t->cols + 1);
    gdouble* ys = g_new0(gdouble, t->rows + 1);
    xs[0] = x;
    for (guint c = 0; c < t->cols; c++) {
        xs[c + 1] = xs[c] + t->col_widths[c] * scale;
    }
    ys[0] = y;
    for (guint r = 0; r < t->rows; r++) {
        /* A row grows to fit its text like in PowerPoint */
        gdouble height = t->row_heights[r] * scale;
        for (guint c = 0; c < t->cols; c++) {
            const SlideCell* cell = &t->cells[r * t->cols + c];
            if (cell->merged || cell->row_span > 1) {
                continue;
            }
            guint end = MIN(c + MAX(cell->col_span, 1), t->cols);
            gdouble w = xs[end] - xs[c] - 14.4 * scale;
            gdouble need = 7.2 * scale;
            for (guint i = 0; i < cell->paragraphs->len; i++) {
                gdouble off = 0;
                PangoLayout* l = paragraph_layout(cr, deck, g_ptr_array_index(cell->paragraphs, i), w, scale, 1.0,
                                                  TRUE, &off);
                gint lh = 0;
                pango_layout_get_pixel_size(l, NULL, &lh);
                need += lh;
                g_object_unref(l);
            }
            height = MAX(height, need);
        }
        ys[r + 1] = ys[r] + height;
    }
    for (guint r = 0; r < t->rows; r++) {
        for (guint c = 0; c < t->cols; c++) {
            const SlideCell* cell = &t->cells[r * t->cols + c];
            if (cell->merged) {
                continue;
            }
            guint ce = MIN(c + MAX(cell->col_span, 1), t->cols), re = MIN(r + MAX(cell->row_span, 1), t->rows);
            gdouble cx = xs[c], cy = ys[r], cw = xs[ce] - xs[c], ch = ys[re] - ys[r];
            if (cell->fill != SLIDE_NO_COLOR) {
                set_color(cr, cell->fill);
                cairo_rectangle(cr, cx, cy, cw, ch);
                cairo_fill(cr);
            }
            if (t->border != SLIDE_NO_COLOR) {
                set_color(cr, t->border);
                cairo_set_line_width(cr, MAX(scale * 0.75, 1.0));
                cairo_rectangle(cr, cx, cy, cw, ch);
                cairo_stroke(cr);
            }
            draw_paragraphs(cr, deck, cell->paragraphs, cx + 7.2 * scale, cy + 3.6 * scale, cw - 14.4 * scale,
                            ch - 7.2 * scale, cell->anchor, scale, 1.0, TRUE);
        }
    }
    g_free(xs);
    g_free(ys);
}

static void draw_shape(cairo_t* cr, const SlideDeck* deck, const SlideShape* s, gdouble scale) {
    gdouble x = s->x * scale, y = s->y * scale, w = s->w * scale, h = s->h * scale;
    cairo_save(cr);
    cairo_translate(cr, x + w / 2, y + h / 2);
    if (s->rotation != 0) {
        cairo_rotate(cr, s->rotation * G_PI / 180.0);
    }
    cairo_translate(cr, -w / 2, -h / 2);

    /* Geometry: flipped in its box, the text is not */
    cairo_save(cr);
    if (s->flip_h || s->flip_v) {
        cairo_translate(cr, s->flip_h ? w : 0, s->flip_v ? h : 0);
        cairo_scale(cr, s->flip_h ? -1 : 1, s->flip_v ? -1 : 1);
    }
    if (s->picture != NULL) {
        draw_picture(cr, s->picture, 0, 0, w, h);
    }
    if (s->fill != SLIDE_NO_COLOR && s->geometry != SLIDE_GEOM_LINE) {
        geometry_path(cr, s, w, h);
        if (s->fill_end != SLIDE_NO_COLOR) {
            set_gradient(cr, s->fill, s->fill_end, s->gradient_angle, 0, 0, w, h);
        } else {
            set_color(cr, s->fill);
        }
        cairo_fill(cr);
    }
    if (s->line != SLIDE_NO_COLOR) {
        geometry_path(cr, s, w, h);
        set_color(cr, s->line);
        cairo_set_line_width(cr, MAX(s->line_width * scale, 1.0));
        cairo_stroke(cr);
    }
    cairo_restore(cr);

    if (s->table != NULL) {
        draw_table(cr, deck, s->table, 0, 0, scale);
    }
    if (s->paragraphs->len > 0) {
        gdouble il = s->inset_left * scale, it = s->inset_top * scale;
        gdouble iw = w - il - s->inset_right * scale, ih = h - it - s->inset_bottom * scale;
        draw_paragraphs(cr, deck, s->paragraphs, il, it, MAX(iw, 1.0), ih, s->anchor, scale, s->font_scale,
                        !s->no_wrap);
    }
    cairo_restore(cr);
}

static cairo_status_t write_png(void* closure, const unsigned char* data, unsigned int length) {
    g_byte_array_append(closure, data, length);
    return CAIRO_STATUS_SUCCESS;
}

GBytes* slide_render_png(const SlideDeck* deck, const Slide* slide, gint width) {
    width = CLAMP(width, 16, 4096);
    gdouble scale = width / deck->width;
    gint height = MAX((gint)(deck->height * scale + 0.5), 1);
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_t* cr = cairo_create(surface);
    if (slide->background_end != SLIDE_NO_COLOR) {
        set_gradient(cr, slide->background, slide->background_end, 90, 0, 0, width, height);
    } else {
        set_color(cr, slide->background);
    }
    cairo_paint(cr);
    if (slide->background_picture != NULL) {
        draw_picture(cr, slide->background_picture, 0, 0, width, height);
    }
    for (guint i = 0; i < slide->shapes->len; i++) {
        draw_shape(cr, deck, g_ptr_array_index(slide->shapes, i), scale);
    }
    cairo_destroy(cr);
    GByteArray* png = g_byte_array_new();
    cairo_status_t status = cairo_surface_write_to_png_stream(surface, write_png, png);
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        g_byte_array_free(png, TRUE);
        return NULL;
    }
    return g_byte_array_free_to_bytes(png);
}

/* ---- Preview ---- */

PreviewDoc* slide_deck_preview(const SlideDeck* deck, const gchar* title, const gchar* subtitle, guint max_drawn) {
    PreviewDoc* doc = preview_doc_new();
    doc->paged = TRUE;
    preview_doc_title(doc, title, subtitle);
    guint n = deck->slides->len;
    for (guint i = 0; i < n; i++) {
        const Slide* s = g_ptr_array_index(deck->slides, i);
        gchar* label = s->title != NULL && s->title[0] != '\0' ? g_strdup_printf(_("Slide %1$u · %2$s"), i + 1, s->title)
                                                               : g_strdup_printf(_("Slide %u"), i + 1);
        for (gchar* c = label; *c != '\0'; c++) {
            if (*c == '\n') {
                *c = ' ';
            }
        }
        if (i == max_drawn) {
            gchar* note = g_strdup_printf(_("The first %1$u of %2$u slides are drawn; the others are listed."), max_drawn, n);
            preview_doc_note(doc, note);
            g_free(note);
            preview_doc_append_len(doc, "\n", 1);
        }
        preview_doc_append(doc, i < max_drawn ? PREVIEW_STYLE_SECTION : PREVIEW_STYLE_DIM, label);
        preview_doc_append_len(doc, "\n", 1);
        g_free(label);
        if (i >= max_drawn) {
            continue;
        }
        GBytes* png = slide_render_png(deck, s, RENDER_WIDTH);
        if (png != NULL) {
            preview_doc_add_image(doc, png, RENDER_WIDTH);
            preview_doc_append_len(doc, "\n", 1);
            g_bytes_unref(png);
        }
        if (s->notes != NULL && s->notes[0] != '\0') {
            gint start = preview_doc_offset(doc);
            preview_doc_append(doc, PREVIEW_STYLE_DIM, s->notes);
            preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_ITALIC);
            preview_doc_append_len(doc, "\n", 1);
        }
        preview_doc_append_len(doc, "\n", 1);
    }
    if (n == 0) {
        preview_doc_note(doc, _("(no slides)"));
    }
    return doc;
}
