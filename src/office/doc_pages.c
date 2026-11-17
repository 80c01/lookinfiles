/* Word processing documents drawn page by page (see doc_pages.h) */
#include "office/doc_pages.h"
#include "core/i18n.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#define PX_PER_PT 1.6              /* pixels of a page per point at zoom 1 (A4: 952 px wide) */
#define PIXEL_BUDGET (48u << 20)   /* pixels of all the pages drawn */
#define DEFAULT_FAMILY "serif"
#define TAB_STEP_PT 36.0           /* default tab stops: 1.27 cm */
#define CELL_PAD_X_PT 5.4          /* cell margins of Word: 0.19 cm */
#define CELL_PAD_Y_PT 1.5
#define RULE_PT 0.5                /* table borders */
#define STORY_GAP_PT 6.0           /* between the header (footer) and the body */
#define MAX_COLUMNS 64
#define MIN_COLUMN_PT 18.0

#define INK 0x000000u              /* automatic text colour on paper */
#define LINK_COLOR 0x0563C1u       /* Word's hyperlink colour */
#define MISSING_COLOR 0x808080u    /* name of a picture that cannot be shown */

/* ---- Pages ---- */

typedef struct {
    PangoContext* ctx;
    gdouble scale;              /* pixels per point */
    gint page_w, page_h;        /* pixels */
    gdouble left, width;        /* body column, pixels */
    gdouble top, bottom;        /* body, pixels from the top edge */
    gdouble header_y;           /* top of the header */
    gdouble footer_y;           /* bottom of the footer */
    gdouble header_h, footer_h;
    const DocModel* header;
    const DocModel* footer;
    const gchar* default_font;
    gdouble default_size;
    guint max_drawn;
    GPtrArray* surfaces;        /* cairo_surface_t*: the pages drawn */
    cairo_t* cr;                /* the page being drawn, NULL when not drawn */
    guint n_pages;
    gdouble y;                  /* top of the next line */
    gboolean page_empty;        /* nothing placed on the page yet */
} Pager;

static void set_color(cairo_t* cr, guint32 rgb) {
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0);
}

/* ---- Pictures ---- */

typedef struct {
    gdouble width_px;           /* wanted width, 0: its own (at 96 dpi) */
    gdouble max_px;             /* widest */
    gdouble scale;
} WantSize;

static void on_size_prepared(GdkPixbufLoader* loader, gint w, gint h, gpointer data) {
    const WantSize* want = data;
    gdouble nw = want->width_px > 0 ? want->width_px : w * 0.75 * want->scale;
    nw = MIN(nw, want->max_px);
    gdouble nh = w > 0 ? nw * h / w : h;
    gint iw = MAX((gint)lround(nw), 1), ih = MAX((gint)lround(nh), 1);
    if (iw != w || ih != h) {
        gdk_pixbuf_loader_set_size(loader, iw, ih);
    }
}

/* An encoded picture decoded at the size it is drawn (premultiplied ARGB),
 * or NULL. New reference. */
static cairo_surface_t* picture_surface(GBytes* data, gdouble width_px, gdouble max_px, gdouble scale) {
    WantSize want = { width_px, MAX(max_px, 1), scale };
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    g_signal_connect(loader, "size-prepared", G_CALLBACK(on_size_prepared), &want);
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

/* Pango draws the pictures of the text (shape attributes) here */
static void draw_shape(cairo_t* cr, PangoAttrShape* attr, gboolean do_path, gpointer data) {
    (void)data;
    cairo_surface_t* s = attr->data;
    if (do_path || s == NULL) {
        return;
    }
    gdouble x = 0, y = 0;
    cairo_get_current_point(cr, &x, &y);
    gdouble w = attr->logical_rect.width / (gdouble)PANGO_SCALE;
    gdouble h = attr->logical_rect.height / (gdouble)PANGO_SCALE;
    gint sw = cairo_image_surface_get_width(s), sh = cairo_image_surface_get_height(s);
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0) {
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, x, y + attr->logical_rect.y / (gdouble)PANGO_SCALE);
    cairo_scale(cr, w / sw, h / sh);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

/* ---- Paragraphs ---- */

static void add_attr(PangoAttrList* list, PangoAttribute* attr, guint start, guint end) {
    attr->start_index = start;
    attr->end_index = end;
    pango_attr_list_insert(list, attr);
}

static PangoAttribute* color_attr(guint32 rgb, gboolean background) {
    guint16 r = (guint16)(((rgb >> 16) & 0xFF) * 257), g = (guint16)(((rgb >> 8) & 0xFF) * 257),
            b = (guint16)((rgb & 0xFF) * 257);
    return background ? pango_attr_background_new(r, g, b) : pango_attr_foreground_new(r, g, b);
}

static PangoFontDescription* font_of(const Pager* pg, const gchar* family, gdouble size_pt) {
    PangoFontDescription* fd = pango_font_description_new();
    const gchar* f = family != NULL && family[0] != '\0' ? family
                     : pg->default_font != NULL && pg->default_font[0] != '\0' ? pg->default_font
                                                                                : DEFAULT_FAMILY;
    pango_font_description_set_family(fd, f);
    pango_font_description_set_absolute_size(fd, MAX(size_pt * pg->scale, 1.0) * PANGO_SCALE);
    return fd;
}

static void add_run(const Pager* pg, GString* text, PangoAttrList* attrs, const DocSeg* s) {
    guint start = (guint)text->len;
    g_string_append(text, s->text);
    guint end = (guint)text->len;
    if (end == start) {
        return;
    }
    const DocRun* r = &s->run;
    gdouble size = r->size > 0 ? r->size : pg->default_size;
    if (r->vert != 0) {
        add_attr(attrs, pango_attr_rise_new((gint)(size * (r->vert > 0 ? 0.33 : -0.12) * pg->scale * PANGO_SCALE)),
                 start, end);
        size *= 0.65;
    }
    PangoFontDescription* fd = font_of(pg, r->font, size);
    pango_font_description_set_weight(fd, r->bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(fd, r->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    add_attr(attrs, pango_attr_font_desc_new(fd), start, end);
    pango_font_description_free(fd);
    if (r->underline || r->link) {
        add_attr(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), start, end);
    }
    if (r->strike) {
        add_attr(attrs, pango_attr_strikethrough_new(TRUE), start, end);
    }
    guint32 color = r->color != DOC_COLOR_NONE ? r->color : r->link ? LINK_COLOR : INK;
    add_attr(attrs, color_attr(color, FALSE), start, end);
    if (r->highlight != DOC_COLOR_NONE) {
        add_attr(attrs, color_attr(r->highlight, TRUE), start, end);
    }
}

static gpointer surface_copy(gconstpointer data) {
    return cairo_surface_reference((cairo_surface_t*)data);
}

static void surface_unref(gpointer data) {
    cairo_surface_destroy(data);
}

static void add_picture(const Pager* pg, GString* text, PangoAttrList* attrs, const DocSeg* s, gdouble max_px) {
    cairo_surface_t* pic = s->image != NULL ? picture_surface(s->image, s->image_width * pg->scale, max_px, pg->scale)
                                            : NULL;
    if (pic == NULL) {
        DocSeg named = *s;
        gchar* label = g_strdup_printf("[%s]", s->image_name != NULL ? s->image_name : "");
        named.text = label;
        named.run.color = MISSING_COLOR;
        add_run(pg, text, attrs, &named);
        g_free(label);
        return;
    }
    gdouble w = cairo_image_surface_get_width(pic), h = cairo_image_surface_get_height(pic);
    PangoRectangle rect = { 0, -(gint)(h * PANGO_SCALE), (gint)(w * PANGO_SCALE), (gint)(h * PANGO_SCALE) };
    guint start = (guint)text->len;
    g_string_append(text, "\xEF\xBF\xBC");
    /* The attribute keeps the reference (freed with it) */
    add_attr(attrs, pango_attr_shape_new_with_data(&rect, &rect, pic, surface_copy, surface_unref), start,
             (guint)text->len);
}

/* A paragraph laid out in a column width_px wide; *x_off: where the
 * layout's left edge is in the column */
static PangoLayout* paragraph_layout(const Pager* pg, const DocParagraph* p, gdouble width_px, gdouble* x_off,
                                     PangoWrapMode wrap, gboolean unwrapped) {
    const DocPara* pp = &p->layout;
    gdouble left = MAX(pp->left, 0) * pg->scale, right = MAX(pp->right, 0) * pg->scale;
    gdouble first = MAX(pp->first, -MAX(pp->left, 0)) * pg->scale;
    gdouble avail = MAX(width_px - left - right, 8.0);
    *x_off = first < 0 ? left + first : left;
    gdouble layout_w = first < 0 ? avail - first : avail;

    GString* text = g_string_new(NULL);
    PangoAttrList* attrs = pango_attr_list_new();
    for (guint i = 0; i < p->segs->len; i++) {
        const DocSeg* s = g_ptr_array_index(p->segs, i);
        if (s->text != NULL) {
            add_run(pg, text, attrs, s);
        } else {
            add_picture(pg, text, attrs, s, avail);
        }
    }
    PangoLayout* layout = pango_layout_new(pg->ctx);
    PangoFontDescription* fd = font_of(pg, NULL, pg->default_size);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text->str, (gint)text->len);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    g_string_free(text, TRUE);
    PangoTabArray* tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
                                                             (gint)(TAB_STEP_PT * pg->scale * PANGO_SCALE));
    pango_layout_set_tabs(layout, tabs);
    pango_tab_array_free(tabs);
    if (!unwrapped) {
        pango_layout_set_width(layout, (gint)(layout_w * PANGO_SCALE));
        pango_layout_set_wrap(layout, wrap);
        pango_layout_set_indent(layout, (gint)(first * PANGO_SCALE));
        pango_layout_set_alignment(layout, pp->align == PREVIEW_ALIGN_CENTER  ? PANGO_ALIGN_CENTER
                                           : pp->align == PREVIEW_ALIGN_RIGHT ? PANGO_ALIGN_RIGHT
                                                                              : PANGO_ALIGN_LEFT);
        pango_layout_set_justify(layout, pp->align == PREVIEW_ALIGN_JUSTIFY);
    }
    return layout;
}

/* Origin of the iterator's line and its height, in the layout */
static void line_metrics(PangoLayoutIter* it, gdouble* origin_x, gdouble* baseline, gdouble* height) {
    PangoRectangle logical, own;
    pango_layout_iter_get_line_extents(it, NULL, &logical);
    pango_layout_line_get_extents(pango_layout_iter_get_line_readonly(it), NULL, &own);
    *origin_x = (logical.x - own.x) / (gdouble)PANGO_SCALE;
    *baseline = (pango_layout_iter_get_baseline(it) - logical.y) / (gdouble)PANGO_SCALE;
    *height = logical.height / (gdouble)PANGO_SCALE;
}

static void draw_line(cairo_t* cr, PangoLayoutIter* it, gdouble x, gdouble y, gdouble baseline) {
    cairo_move_to(cr, x, y + baseline);
    pango_cairo_show_layout_line(cr, pango_layout_iter_get_line_readonly(it));
}

/* A paragraph in a box (no page breaks): its height, drawn when cr is set */
static gdouble paragraph_box(const Pager* pg, cairo_t* cr, const DocParagraph* p, gdouble x, gdouble y,
                             gdouble width, gboolean first_in_box) {
    gdouble top = y;
    if (!first_in_box) {
        y += MAX(p->layout.before, 0) * pg->scale;
    }
    gdouble x_off = 0;
    PangoLayout* layout = paragraph_layout(pg, p, width, &x_off, PANGO_WRAP_WORD_CHAR, FALSE);
    PangoLayoutIter* it = pango_layout_get_iter(layout);
    do {
        gdouble origin, baseline, h;
        line_metrics(it, &origin, &baseline, &h);
        if (cr != NULL) {
            draw_line(cr, it, x + x_off + origin, y, baseline);
        }
        y += h;
    } while (pango_layout_iter_next_line(it));
    pango_layout_iter_free(it);
    g_object_unref(layout);
    y += MAX(p->layout.after, 0) * pg->scale;
    return y - top;
}

static gdouble paragraphs_box(const Pager* pg, cairo_t* cr, const GPtrArray* paras, gdouble x, gdouble y,
                              gdouble width) {
    gdouble top = y;
    for (guint i = 0; i < paras->len; i++) {
        y += paragraph_box(pg, cr, g_ptr_array_index(paras, i), x, y, width, i == 0);
    }
    return y - top;
}

/* ---- Tables ---- */

typedef struct {
    guint ncols;
    gdouble* widths;            /* pixels, cell margins included */
} Grid;

static guint row_columns(const DocRow* r) {
    guint n = 0;
    for (guint i = 0; i < r->cells->len; i++) {
        n += MAX(((DocCell*)g_ptr_array_index(r->cells, i))->span, 1);
    }
    return n;
}

/* Width of a cell's text laid out without wrapping (natural) or at its
 * longest word (min) */
static void cell_widths(const Pager* pg, const DocCell* c, gdouble* min_w, gdouble* max_w) {
    *min_w = *max_w = 0;
    for (guint i = 0; i < c->paras->len; i++) {
        const DocParagraph* p = g_ptr_array_index(c->paras, i);
        gdouble indents = (MAX(p->layout.left, 0) + MAX(p->layout.right, 0)) * pg->scale;
        gdouble x_off = 0;
        PangoRectangle logical;
        PangoLayout* natural = paragraph_layout(pg, p, 1e6, &x_off, PANGO_WRAP_WORD, TRUE);
        pango_layout_get_extents(natural, NULL, &logical);
        *max_w = MAX(*max_w, logical.width / (gdouble)PANGO_SCALE + indents);
        g_object_unref(natural);
        PangoLayout* narrow = paragraph_layout(pg, p, 1, &x_off, PANGO_WRAP_WORD, FALSE);
        pango_layout_get_extents(narrow, NULL, &logical);
        *min_w = MAX(*min_w, logical.width / (gdouble)PANGO_SCALE + indents);
        g_object_unref(narrow);
    }
}

/* Column widths filling the body column: each column between the width of
 * its longest word and that of its text unwrapped */
static Grid table_grid(const Pager* pg, const DocTable* t, gdouble avail) {
    Grid g = { 0, NULL };
    for (guint r = 0; r < t->rows->len; r++) {
        g.ncols = MAX(g.ncols, row_columns(g_ptr_array_index(t->rows, r)));
    }
    g.ncols = MIN(g.ncols, MAX_COLUMNS);
    if (g.ncols == 0) {
        return g;
    }
    gdouble pad = 2 * CELL_PAD_X_PT * pg->scale, floor_w = MIN_COLUMN_PT * pg->scale;
    gdouble* lo = g_new0(gdouble, g.ncols);
    gdouble* hi = g_new0(gdouble, g.ncols);
    for (guint r = 0; r < t->rows->len; r++) {
        const DocRow* row = g_ptr_array_index(t->rows, r);
        guint col = 0;
        for (guint i = 0; i < row->cells->len && col < g.ncols; i++) {
            const DocCell* cell = g_ptr_array_index(row->cells, i);
            guint span = MAX(cell->span, 1);
            if (span == 1) {
                gdouble mn, mx;
                cell_widths(pg, cell, &mn, &mx);
                lo[col] = MAX(lo[col], mn + pad);
                hi[col] = MAX(hi[col], mx + pad);
            }
            col += span;
        }
    }
    gdouble sum_lo = 0, sum_hi = 0;
    for (guint c = 0; c < g.ncols; c++) {
        lo[c] = MAX(lo[c], floor_w);
        hi[c] = MAX(hi[c], lo[c]);
        sum_lo += lo[c];
        sum_hi += hi[c];
    }
    g.widths = g_new0(gdouble, g.ncols);
    for (guint c = 0; c < g.ncols; c++) {
        if (sum_hi <= avail) {
            g.widths[c] = hi[c] * avail / sum_hi; /* Word's tables fill the column */
        } else if (sum_lo <= avail) {
            g.widths[c] = lo[c] + (hi[c] - lo[c]) * (avail - sum_lo) / MAX(sum_hi - sum_lo, 1e-6);
        } else {
            g.widths[c] = lo[c] * avail / sum_lo;
        }
    }
    g_free(lo);
    g_free(hi);
    return g;
}

/* Height of a row, drawn at (x, y) when cr is set */
static gdouble row_box(const Pager* pg, cairo_t* cr, const DocRow* row, const Grid* g, gdouble x, gdouble y,
                       gdouble height) {
    gdouble pad_x = CELL_PAD_X_PT * pg->scale, pad_y = CELL_PAD_Y_PT * pg->scale;
    gdouble tallest = 0, cx = x;
    guint col = 0;
    for (guint i = 0; i < row->cells->len && col < g->ncols; i++) {
        const DocCell* cell = g_ptr_array_index(row->cells, i);
        guint span = MIN(MAX(cell->span, 1), g->ncols - col);
        gdouble w = 0;
        for (guint k = col; k < col + span; k++) {
            w += g->widths[k];
        }
        gdouble h = paragraphs_box(pg, cr, cell->paras, cx + pad_x, y + pad_y, MAX(w - 2 * pad_x, 4)) + 2 * pad_y;
        tallest = MAX(tallest, h);
        if (cr != NULL) {
            set_color(cr, INK);
            cairo_set_line_width(cr, RULE_PT * pg->scale);
            cairo_rectangle(cr, cx, y, w, height);
            cairo_stroke(cr);
        }
        cx += w;
        col += span;
    }
    for (; cr != NULL && col < g->ncols; col++) {
        /* Cells missing at the end of the row: empty */
        set_color(cr, INK);
        cairo_set_line_width(cr, RULE_PT * pg->scale);
        cairo_rectangle(cr, cx, y, g->widths[col], height);
        cairo_stroke(cr);
        cx += g->widths[col];
    }
    return MAX(tallest, pg->default_size * pg->scale);
}

static gdouble row_draw(const Pager* pg, cairo_t* cr, const DocRow* row, const Grid* g, gdouble x, gdouble y) {
    gdouble h = row_box(pg, NULL, row, g, x, y, 0);
    if (cr != NULL) {
        row_box(pg, cr, row, g, x, y, h);
    }
    return h;
}

/* A table in a box (headers, footers): its height */
static gdouble table_box(const Pager* pg, cairo_t* cr, const DocTable* t, gdouble x, gdouble y, gdouble width) {
    Grid g = table_grid(pg, t, width);
    gdouble top = y;
    for (guint r = 0; g.ncols > 0 && r < t->rows->len; r++) {
        y += row_draw(pg, cr, g_ptr_array_index(t->rows, r), &g, x, y);
    }
    g_free(g.widths);
    return y - top;
}

/* Header or footer: its height, drawn when cr is set */
static gdouble story_box(const Pager* pg, cairo_t* cr, const DocModel* story, gdouble y) {
    gdouble top = y;
    for (guint i = 0; story != NULL && i < story->blocks->len; i++) {
        const DocBlock* b = g_ptr_array_index(story->blocks, i);
        if (b->kind == DOC_BLOCK_PARAGRAPH) {
            y += paragraph_box(pg, cr, b->para, pg->left, y, pg->width, i == 0);
        } else if (b->kind == DOC_BLOCK_TABLE) {
            y += table_box(pg, cr, b->table, pg->left, y, pg->width);
        }
    }
    return y - top;
}

/* ---- Flow ---- */

static void finish_page(Pager* pg) {
    if (pg->cr != NULL) {
        cairo_destroy(pg->cr);
        pg->cr = NULL;
    }
}

static void new_page(Pager* pg) {
    finish_page(pg);
    pg->n_pages++;
    if (pg->n_pages <= pg->max_drawn) {
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pg->page_w, pg->page_h);
        g_ptr_array_add(pg->surfaces, surface);
        pg->cr = cairo_create(surface);
        cairo_set_source_rgb(pg->cr, 1, 1, 1);
        cairo_paint(pg->cr);
        if (pg->header_h > 0) {
            story_box(pg, pg->cr, pg->header, pg->header_y);
        }
        if (pg->footer_h > 0) {
            story_box(pg, pg->cr, pg->footer, pg->footer_y - pg->footer_h);
        }
    }
    pg->y = pg->top;
    pg->page_empty = TRUE;
}

/* Room for h more pixels on the page, else a new page (unless the page is
 * still empty: what does not fit on a page is cut) */
static void need(Pager* pg, gdouble h) {
    if (pg->y + h > pg->bottom + 0.5 && !pg->page_empty) {
        new_page(pg);
    }
}

static void flow_paragraph(Pager* pg, const DocParagraph* p) {
    if (!pg->page_empty) {
        pg->y += MAX(p->layout.before, 0) * pg->scale;
    }
    gdouble x_off = 0;
    PangoLayout* layout = paragraph_layout(pg, p, pg->width, &x_off, PANGO_WRAP_WORD_CHAR, FALSE);
    PangoLayoutIter* it = pango_layout_get_iter(layout);
    do {
        gdouble origin, baseline, h;
        line_metrics(it, &origin, &baseline, &h);
        need(pg, h);
        if (pg->cr != NULL) {
            draw_line(pg->cr, it, pg->left + x_off + origin, pg->y, baseline);
        }
        pg->y += h;
        pg->page_empty = FALSE;
    } while (pango_layout_iter_next_line(it));
    pango_layout_iter_free(it);
    g_object_unref(layout);
    pg->y += MAX(p->layout.after, 0) * pg->scale;
}

static void flow_table(Pager* pg, const DocTable* t) {
    Grid g = table_grid(pg, t, pg->width);
    for (guint r = 0; g.ncols > 0 && r < t->rows->len; r++) {
        const DocRow* row = g_ptr_array_index(t->rows, r);
        gdouble h = row_box(pg, NULL, row, &g, pg->left, pg->y, 0);
        need(pg, h);
        if (pg->cr != NULL) {
            row_box(pg, pg->cr, row, &g, pg->left, pg->y, h);
        }
        pg->y += h;
        pg->page_empty = FALSE;
    }
    g_free(g.widths);
}

/* Lay out the body on pages, drawing the first max_drawn */
static void pager_run(Pager* pg, const DocPageSetup* given, const DocModel* header, const DocModel* body,
                      const DocModel* footer, gdouble zoom, guint max_drawn) {
    DocPageSetup setup = *given;
    doc_page_setup_fix(&setup);
    memset(pg, 0, sizeof *pg);
    pg->ctx = pango_font_map_create_context(pango_cairo_font_map_get_default());
    cairo_font_options_t* fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options(pg->ctx, fo);
    cairo_font_options_destroy(fo);
    pango_cairo_context_set_shape_renderer(pg->ctx, draw_shape, NULL, NULL);

    pg->scale = PX_PER_PT * (zoom > 0 ? zoom : 1.0);
    pg->page_w = MAX((gint)lround(setup.width * pg->scale), 1);
    pg->page_h = MAX((gint)lround(setup.height * pg->scale), 1);
    pg->left = setup.left * pg->scale;
    pg->width = MAX((setup.width - setup.left - setup.right) * pg->scale, 16);
    pg->default_font = body->default_font;
    pg->default_size = body->default_size > 0 ? body->default_size : 11.0;
    pg->header = doc_model_has_content(header) ? header : NULL;
    pg->footer = doc_model_has_content(footer) ? footer : NULL;
    pg->header_y = setup.header * pg->scale;
    pg->footer_y = (setup.height - setup.footer) * pg->scale;
    pg->header_h = pg->header != NULL ? story_box(pg, NULL, pg->header, pg->header_y) : 0;
    pg->footer_h = pg->footer != NULL ? story_box(pg, NULL, pg->footer, 0) : 0;
    gdouble gap = STORY_GAP_PT * pg->scale;
    pg->top = MAX(setup.top * pg->scale, pg->header_h > 0 ? pg->header_y + pg->header_h + gap : 0);
    pg->bottom = MIN((setup.height - setup.bottom) * pg->scale,
                     pg->footer_h > 0 ? pg->footer_y - pg->footer_h - gap : pg->page_h);
    if (pg->bottom - pg->top < pg->page_h / 4.0) {
        /* Header and footer leave no room: the margins only */
        pg->top = setup.top * pg->scale;
        pg->bottom = (setup.height - setup.bottom) * pg->scale;
    }
    pg->max_drawn = max_drawn;
    pg->surfaces = g_ptr_array_new_with_free_func((GDestroyNotify)cairo_surface_destroy);

    new_page(pg);
    gboolean placed = FALSE;
    for (guint i = 0; i < body->blocks->len; i++) {
        const DocBlock* b = g_ptr_array_index(body->blocks, i);
        if (b->kind == DOC_BLOCK_PARAGRAPH) {
            flow_paragraph(pg, b->para);
            placed = TRUE;
        } else if (b->kind == DOC_BLOCK_TABLE) {
            flow_table(pg, b->table);
            placed = TRUE;
        } else if (placed) {
            new_page(pg);
        }
    }
    finish_page(pg);
}

static void pager_clear(Pager* pg) {
    finish_page(pg);
    g_ptr_array_free(pg->surfaces, TRUE);
    g_object_unref(pg->ctx);
}

guint doc_pages_count(const DocPageSetup* setup, const DocModel* header, const DocModel* body,
                      const DocModel* footer) {
    Pager pg;
    pager_run(&pg, setup, header, body, footer, 1.0, 0);
    guint n = pg.n_pages;
    pager_clear(&pg);
    return n;
}

PreviewDoc* doc_pages_preview(const gchar* title, const gchar* subtitle, const DocPageSetup* setup,
                              const DocModel* header, const DocModel* body, const DocModel* footer,
                              const gchar* note, gdouble zoom) {
    PreviewDoc* doc = preview_doc_new();
    doc->paged = TRUE;
    doc->image_zoom = zoom > 0 ? MIN(zoom, PREVIEW_DOC_IMAGE_ZOOM_MAX) : 1.0;
    DocPageSetup fixed = *setup;
    doc_page_setup_fix(&fixed);
    gdouble scale = PX_PER_PT * doc->image_zoom;
    gdouble page_px = MAX(fixed.width * scale * fixed.height * scale, 1.0);
    guint max_drawn = (guint)CLAMP(PIXEL_BUDGET / page_px, 1, DOC_PAGES_MAX_DRAWN);

    Pager pg;
    pager_run(&pg, &fixed, header, body, footer, doc->image_zoom, max_drawn);
    gchar* count = g_strdup_printf(ngettext("%u page", "%u pages", pg.n_pages), pg.n_pages);
    gchar* full = subtitle != NULL && subtitle[0] != '\0' ? g_strdup_printf("%s · %s", subtitle, count)
                                                          : g_strdup(count);
    preview_doc_title(doc, title != NULL ? title : "", full);
    g_free(full);
    g_free(count);
    for (guint i = 0; i < pg.surfaces->len; i++) {
        cairo_surface_t* s = g_ptr_array_index(pg.surfaces, i);
        cairo_surface_flush(s);
        gchar* label = g_strdup_printf(_("Page %1$u of %2$u"), i + 1, pg.n_pages);
        preview_doc_append(doc, PREVIEW_STYLE_DIM, label);
        preview_doc_append_len(doc, "\n", 1);
        g_free(label);
        gint w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
        gint stride = cairo_image_surface_get_stride(s);
        /* The surface's own memory, freed with the bytes: no copy */
        GBytes* pixels = g_bytes_new_with_free_func(cairo_image_surface_get_data(s), (gsize)stride * (gsize)h,
                                                    (GDestroyNotify)cairo_surface_destroy, cairo_surface_reference(s));
        preview_doc_add_pixels(doc, pixels, w, h, stride, w);
        g_bytes_unref(pixels);
        preview_doc_append_len(doc, "\n\n", 2);
    }
    if (pg.surfaces->len < pg.n_pages) {
        gchar* more = g_strdup_printf(_("The first %1$u of %2$u pages are drawn; the text view (m) shows the whole "
                                        "document."),
                                      pg.surfaces->len, pg.n_pages);
        preview_doc_note(doc, more);
        g_free(more);
    }
    if (note != NULL) {
        preview_doc_note(doc, note);
    }
    pager_clear(&pg);
    return doc;
}

PreviewDoc* doc_pages_or_text(gboolean pages, const gchar* title, const gchar* subtitle, const DocPageSetup* setup,
                              const DocModel* header, const DocModel* body, const DocModel* footer,
                              const gchar* note, gdouble zoom) {
    if (pages) {
        return doc_pages_preview(title, subtitle, setup, header, body, footer, note, zoom);
    }
    PreviewDoc* h = header != NULL ? doc_model_preview(header) : NULL;
    PreviewDoc* b = doc_model_preview(body);
    PreviewDoc* f = footer != NULL ? doc_model_preview(footer) : NULL;
    PreviewDoc* doc = doc_writer_page(title, subtitle, h, b, f, note);
    if (h != NULL) preview_doc_free(h);
    preview_doc_free(b);
    if (f != NULL) preview_doc_free(f);
    return doc;
}
