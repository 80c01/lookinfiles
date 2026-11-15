/* Text and tables of OpenDocument drawings (see odp_text.h) */
#include "office/odp_text.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_SIZE 18.0
#define DEFAULT_LABEL_WIDTH 17.0  /* 0.6 cm */
#define MAX_CELLS 4096
#define MAX_COLUMNS 64
#define MAX_REPEAT 256

/* ---- Runs ---- */

typedef struct {
    const OdpTextContext* ctx;
    SlideParagraph* para;
    gboolean last_space;    /* collapse spaces, none at the start */
} Inline;

static gboolean is_dark(guint32 rgb) {
    gdouble lum = 0.299 * ((rgb >> 16) & 0xFF) + 0.587 * ((rgb >> 8) & 0xFF) + 0.114 * (rgb & 0xFF);
    return lum < 128;
}

/* Run properties from a cascade */
static void style_run(const OdpTextContext* ctx, const OdpCascade* c, SlideRun* r) {
    gboolean is_family = FALSE;
    const gchar* font = odp_cascade_get_either(c, "style:font-name", "fo:font-family", &is_family);
    g_free(r->font);
    r->font = is_family ? odp_first_family(font) : g_strdup(odp_styles_font(ctx->styles, font));
    r->size = odp_cascade_font_size(c, ctx->default_size > 0 ? ctx->default_size : DEFAULT_SIZE);
    const gchar* weight = odp_cascade_get(c, "fo:font-weight");
    r->bold = weight != NULL && (strcmp(weight, "bold") == 0 || atoi(weight) >= 600);
    const gchar* style = odp_cascade_get(c, "fo:font-style");
    r->italic = style != NULL && (strcmp(style, "italic") == 0 || strcmp(style, "oblique") == 0);
    const gchar* under = odp_cascade_get(c, "style:text-underline-style");
    r->underline = under != NULL && strcmp(under, "none") != 0;
    const gchar* strike = odp_cascade_get(c, "style:text-line-through-style");
    r->strike = strike != NULL && strcmp(strike, "none") != 0;
    const gchar* pos = odp_cascade_get(c, "style:text-position");
    r->baseline = 0;
    if (pos != NULL) {
        if (g_str_has_prefix(pos, "super")) r->baseline = 1;
        else if (g_str_has_prefix(pos, "sub")) r->baseline = -1;
        else if (g_ascii_strtod(pos, NULL) > 0) r->baseline = 1;
        else if (g_ascii_strtod(pos, NULL) < 0) r->baseline = -1;
    }
    gboolean automatic = FALSE;
    const gchar* color = odp_cascade_get_either(c, "fo:color", "style:use-window-font-color", &automatic);
    if (color == NULL || (automatic && strcmp(color, "true") == 0)) {
        r->color = is_dark(ctx->background) ? 0xFFFFFF : 0x000000;
    } else {
        r->color = automatic ? 0x000000 : odp_color(color, 0x000000);
    }
}

static void add_text(Inline* in, const OdpCascade* c, const gchar* text, gboolean collapse) {
    GString* s = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p++) {
        gboolean space = *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r';
        if (collapse && space) {
            if (!in->last_space) {
                g_string_append_c(s, ' ');
            }
            in->last_space = TRUE;
        } else {
            g_string_append_c(s, *p);
            in->last_space = FALSE;
        }
    }
    if (s->len > 0) {
        SlideRun* r = slide_paragraph_add_run(in->para, s->str, (gssize)s->len);
        style_run(in->ctx, c, r);
        const gchar* transform = odp_cascade_get(c, "fo:text-transform");
        if (g_strcmp0(transform, "uppercase") == 0 || g_strcmp0(transform, "lowercase") == 0) {
            gchar* t = transform[0] == 'u' ? g_utf8_strup(r->text, -1) : g_utf8_strdown(r->text, -1);
            g_free(r->text);
            r->text = t;
        }
    }
    g_string_free(s, TRUE);
}

static void add_inline(Inline* in, const OdpNode* node, const OdpCascade* c) {
    for (guint i = 0; i < node->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(node->children, i);
        if (n->name == NULL) {
            add_text(in, c, n->text, TRUE);
        } else if (odp_node_is(n, "text:span")) {
            OdpCascade span = { { 0 }, 0 };
            odp_cascade_add(&span, in->ctx->styles, "text", odp_node_attr(n, "text:style-name"));
            odp_cascade_append(&span, c);
            add_inline(in, n, &span);
        } else if (odp_node_is(n, "text:s")) {
            const gchar* count = odp_node_attr(n, "text:c");
            gint k = CLAMP(count != NULL ? atoi(count) : 1, 1, 100);
            gchar* spaces = g_strnfill((gsize)k, ' ');
            add_text(in, c, spaces, FALSE);
            g_free(spaces);
        } else if (odp_node_is(n, "text:tab")) {
            add_text(in, c, "\t", FALSE);
        } else if (odp_node_is(n, "text:line-break")) {
            add_text(in, c, "\n", FALSE);
            in->last_space = TRUE;
        } else if (odp_node_is(n, "text:page-number")) {
            gchar* num = g_strdup_printf("%u", in->ctx->page_number);
            add_text(in, c, num, FALSE);
            g_free(num);
        } else if (odp_node_is(n, "presentation:footer")) {
            add_text(in, c, in->ctx->footer != NULL ? in->ctx->footer : "", TRUE);
        } else if (odp_node_is(n, "presentation:date-time")) {
            add_text(in, c, in->ctx->date_time != NULL ? in->ctx->date_time : "", TRUE);
        } else if (odp_node_is(n, "presentation:header")) {
            add_text(in, c, in->ctx->header != NULL ? in->ctx->header : "", TRUE);
        } else if (odp_node_is(n, "text:note") || odp_node_is(n, "office:annotation") ||
                   odp_node_is(n, "office:annotation-end") || odp_node_is(n, "text:bookmark-ref")) {
            continue;
        } else {
            add_inline(in, n, c);   /* text:a, fields, bookmarks... */
        }
    }
}

/* ---- Paragraphs and lists ---- */

typedef struct {
    const OdpList* list;
    gint level;             /* 1-based, 0: not in a list */
    gboolean labelled;      /* first paragraph of a list item */
    gint counters[ODP_LIST_LEVELS];
} ListState;

static gchar* to_roman(gint n, gboolean upper) {
    static const gint VALUES[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const gchar* const DIGITS[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    GString* s = g_string_new(NULL);
    n = CLAMP(n, 1, 3999);
    for (gsize i = 0; i < G_N_ELEMENTS(VALUES); i++) {
        while (n >= VALUES[i]) {
            g_string_append(s, DIGITS[i]);
            n -= VALUES[i];
        }
    }
    if (upper) {
        gchar* u = g_ascii_strup(s->str, -1);
        g_string_free(s, TRUE);
        return u;
    }
    return g_string_free(s, FALSE);
}

static gchar* number_label(const OdpListLevel* l, gint n) {
    const gchar* f = l->num_format != NULL ? l->num_format : "1";
    gchar* num;
    if (f[0] == 'a' || f[0] == 'A') {
        GString* s = g_string_new(NULL);
        for (gint k = n; k > 0; k = (k - 1) / 26) {
            g_string_prepend_c(s, (gchar)((f[0] == 'a' ? 'a' : 'A') + (k - 1) % 26));
        }
        num = g_string_free(s, FALSE);
    } else if (f[0] == 'i' || f[0] == 'I') {
        num = to_roman(n, f[0] == 'I');
    } else if (f[0] == '\0') {
        num = g_strdup("");
    } else {
        num = g_strdup_printf("%d", n);
    }
    gchar* label = g_strconcat(l->num_prefix != NULL ? l->num_prefix : "", num,
                               l->num_suffix != NULL ? l->num_suffix : "", NULL);
    g_free(num);
    return label;
}

static SlideAlign parse_align(const gchar* a) {
    if (a == NULL) return SLIDE_ALIGN_LEFT;
    if (strcmp(a, "center") == 0) return SLIDE_ALIGN_CENTER;
    if (strcmp(a, "end") == 0 || strcmp(a, "right") == 0) return SLIDE_ALIGN_RIGHT;
    if (strcmp(a, "justify") == 0) return SLIDE_ALIGN_JUSTIFY;
    return SLIDE_ALIGN_LEFT;
}

static gboolean symbol_font(const gchar* font) {
    return font != NULL && (g_ascii_strcasecmp(font, "StarSymbol") == 0 || g_ascii_strcasecmp(font, "OpenSymbol") == 0);
}

static void add_paragraph(const OdpTextContext* ctx, const OdpNode* node, ListState* ls, GPtrArray* out) {
    OdpCascade pc = { { 0 }, 0 };
    odp_cascade_add(&pc, ctx->styles, "paragraph", odp_node_attr(node, "text:style-name"));
    if (ctx->outline_prefix != NULL && ls->level > 1) {
        gchar* level_style = g_strdup_printf("%s%d", ctx->outline_prefix, MIN(ls->level, 9));
        odp_cascade_add(&pc, ctx->styles, "presentation", level_style);
        g_free(level_style);
    }
    odp_cascade_append(&pc, &ctx->base);

    SlideParagraph* p = slide_paragraph_new();
    p->size = odp_cascade_font_size(&pc, ctx->default_size > 0 ? ctx->default_size : DEFAULT_SIZE);
    p->align = parse_align(odp_cascade_get(&pc, "fo:text-align"));
    p->space_before = odp_length(odp_cascade_get(&pc, "fo:margin-top"), 0);
    p->space_after = odp_length(odp_cascade_get(&pc, "fo:margin-bottom"), 0);
    gdouble line = odp_percent(odp_cascade_get(&pc, "fo:line-height"));
    p->line_spacing = line > 0 ? line : 0;
    gdouble margin = odp_length(odp_cascade_get(&pc, "fo:margin-left"), 0);
    p->left = margin;
    p->indent = odp_length(odp_cascade_get(&pc, "fo:text-indent"), 0);

    if (ls->level > 0 && ls->labelled) {
        gint lv = MIN(ls->level, ODP_LIST_LEVELS) - 1;
        const OdpListLevel* l = ls->list != NULL && ls->list->levels[lv].set ? &ls->list->levels[lv] : NULL;
        if (l == NULL || l->bullet != NULL || l->numbered) {
            if (l != NULL && l->numbered) {
                if (ls->counters[lv] == 0) {
                    ls->counters[lv] = l->start > 0 ? l->start : 1;
                } else {
                    ls->counters[lv]++;
                }
                p->bullet = number_label(l, ls->counters[lv]);
            } else {
                p->bullet = g_strdup(l != NULL ? l->bullet : "•");
            }
            if (l != NULL && l->label_alignment) {
                p->left = l->margin_left;
                p->indent = l->text_indent;
            } else {
                gdouble width = l != NULL ? l->min_label_width : DEFAULT_LABEL_WIDTH;
                p->left = margin + (l != NULL ? l->space_before : 0) + width;
                p->indent = -width;
            }
            if (l != NULL) {
                p->bullet_scale = l->size_percent;
                p->bullet_color = l->color != 0xFFFFFFFFu ? l->color : SLIDE_NO_COLOR;
                p->bullet_font = symbol_font(l->font) ? NULL : g_strdup(l->font);
                /* Numbers take the text's size */
                if (l->numbered) p->bullet_scale = 0;
            }
        }
        /* Deeper levels restart their numbering */
        for (gint k = lv + 1; k < ODP_LIST_LEVELS; k++) {
            ls->counters[k] = 0;
        }
    }

    Inline in = { ctx, p, TRUE };
    add_inline(&in, node, &pc);
    /* Trailing space of the paragraph */
    if (p->runs->len > 0) {
        SlideRun* last = g_ptr_array_index(p->runs, p->runs->len - 1);
        g_strchomp(last->text);
    }
    g_ptr_array_add(out, p);
}

static void add_block(const OdpTextContext* ctx, const OdpNode* container, ListState* ls, GPtrArray* out) {
    gboolean first = TRUE;
    for (guint i = 0; i < container->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(container->children, i);
        if (odp_node_is(n, "text:p") || odp_node_is(n, "text:h")) {
            ListState para = *ls;
            para.labelled = ls->labelled && first;
            add_paragraph(ctx, n, &para, out);
            memcpy(ls->counters, para.counters, sizeof ls->counters);
            first = FALSE;
        } else if (odp_node_is(n, "text:list")) {
            ListState inner = *ls;
            const OdpList* explicit = odp_styles_list(ctx->styles, odp_node_attr(n, "text:style-name"));
            if (explicit != NULL) {
                inner.list = explicit;
            }
            inner.level = ls->level + 1;
            /* A new list numbers from its start unless it continues */
            if (g_strcmp0(odp_node_attr(n, "text:continue-numbering"), "true") != 0 &&
                odp_node_attr(n, "text:continue-list") == NULL) {
                for (gint k = MIN(inner.level, ODP_LIST_LEVELS) - 1; k < ODP_LIST_LEVELS; k++) {
                    inner.counters[k] = 0;
                }
            }
            for (guint k = 0; k < n->children->len; k++) {
                const OdpNode* item = g_ptr_array_index(n->children, k);
                gboolean header = odp_node_is(item, "text:list-header");
                if (!header && !odp_node_is(item, "text:list-item")) {
                    continue;
                }
                const gchar* start = odp_node_attr(item, "text:start-value");
                if (start != NULL) {
                    inner.counters[MIN(inner.level, ODP_LIST_LEVELS) - 1] = atoi(start) - 1;
                }
                inner.labelled = !header;
                add_block(ctx, item, &inner, out);
            }
            memcpy(ls->counters, inner.counters, sizeof ls->counters);
            first = FALSE;
        }
    }
}

void odp_text_paragraphs(const OdpTextContext* ctx, const OdpNode* container, GPtrArray* out) {
    if (container == NULL) {
        return;
    }
    ListState ls;
    memset(&ls, 0, sizeof ls);
    ls.list = ctx->list;
    add_block(ctx, container, &ls, out);
}

/* ---- Tables ---- */

static guint repeat_of(const OdpNode* n, const gchar* attr) {
    const gchar* v = odp_node_attr(n, attr);
    gint r = v != NULL ? atoi(v) : 1;
    return (guint)CLAMP(r, 1, MAX_REPEAT);
}

/* Rows or columns, looking inside their groups */
static void collect(const OdpNode* node, const gchar* name, GPtrArray* out) {
    for (guint i = 0; i < node->children->len; i++) {
        const OdpNode* n = g_ptr_array_index(node->children, i);
        if (odp_node_is(n, name)) {
            g_ptr_array_add(out, (gpointer)n);
        } else if (n->name != NULL && g_str_has_prefix(n->name, "table:table-") &&
                   (g_str_has_suffix(n->name, "-rows") || g_str_has_suffix(n->name, "-columns"))) {
            collect(n, name, out);
        }
    }
}

/* Colour of a border (width, style, then the colour), SLIDE_NO_COLOR for none */
static guint32 border_color(const gchar* border) {
    if (border == NULL || strstr(border, "none") != NULL) {
        return SLIDE_NO_COLOR;
    }
    const gchar* hash = strchr(border, '#');
    if (hash == NULL || strlen(hash) < 7) {
        return 0x000000;
    }
    gchar* c = g_strndup(hash, 7);
    guint32 rgb = odp_color(c, 0x000000);
    g_free(c);
    return rgb;
}

/* Part of the table template for a cell (as LibreOffice picks it) */
static const gchar* template_part(const OdpNode* table, guint r, guint c, guint rows, guint cols) {
    gboolean first_row = g_strcmp0(odp_node_attr(table, "table:use-first-row-styles"), "true") == 0;
    gboolean last_row = g_strcmp0(odp_node_attr(table, "table:use-last-row-styles"), "true") == 0;
    gboolean first_col = g_strcmp0(odp_node_attr(table, "table:use-first-column-styles"), "true") == 0;
    gboolean last_col = g_strcmp0(odp_node_attr(table, "table:use-last-column-styles"), "true") == 0;
    gboolean band_rows = g_strcmp0(odp_node_attr(table, "table:use-banding-rows-styles"), "true") == 0;
    gboolean band_cols = g_strcmp0(odp_node_attr(table, "table:use-banding-columns-styles"), "true") == 0;
    if (first_row && r == 0) return "first-row";
    if (last_row && r + 1 == rows) return "last-row";
    if (first_col && c == 0) return "first-column";
    if (last_col && c + 1 == cols) return "last-column";
    if (band_rows && ((r + (first_row ? 1 : 0)) & 1) == 1) return "odd-rows";
    if (band_cols && ((c + (first_col ? 1 : 0)) & 1) == 1) return "odd-columns";
    return "body";
}

static guint32 cell_fill(const OdpCascade* c) {
    const gchar* fill = odp_cascade_get(c, "draw:fill");
    if (fill != NULL) {
        return strcmp(fill, "solid") == 0 ? odp_color(odp_cascade_get(c, "draw:fill-color"), SLIDE_NO_COLOR)
                                          : SLIDE_NO_COLOR;
    }
    return odp_color(odp_cascade_get(c, "fo:background-color"), SLIDE_NO_COLOR);
}

SlideTable* odp_text_table(const OdpTextContext* ctx, const OdpNode* table, gdouble width, gdouble height) {
    GPtrArray* column_nodes = g_ptr_array_new();
    GPtrArray* row_nodes = g_ptr_array_new();
    collect(table, "table:table-column", column_nodes);
    collect(table, "table:table-row", row_nodes);

    /* Columns as listed (repeated ones expanded) */
    GArray* col_widths = g_array_new(FALSE, TRUE, sizeof(gdouble));
    GPtrArray* col_styles = g_ptr_array_new();
    for (guint i = 0; i < column_nodes->len && col_widths->len < MAX_COLUMNS; i++) {
        const OdpNode* n = g_ptr_array_index(column_nodes, i);
        OdpCascade cc = { { 0 }, 0 };
        odp_cascade_add(&cc, ctx->styles, "table-column", odp_node_attr(n, "table:style-name"));
        gdouble w = odp_length(odp_cascade_get(&cc, "style:column-width"), 0);
        for (guint k = repeat_of(n, "table:number-columns-repeated"); k > 0 && col_widths->len < MAX_COLUMNS; k--) {
            g_array_append_val(col_widths, w);
            g_ptr_array_add(col_styles, (gpointer)odp_node_attr(n, "table:default-cell-style-name"));
        }
    }
    /* Rows (repeated ones expanded) and the widest row */
    GPtrArray* rows = g_ptr_array_new();
    guint cols = col_widths->len;
    for (guint i = 0; i < row_nodes->len; i++) {
        const OdpNode* n = g_ptr_array_index(row_nodes, i);
        guint count = 0;
        for (guint k = 0; k < n->children->len; k++) {
            const OdpNode* cell = g_ptr_array_index(n->children, k);
            if (odp_node_is(cell, "table:table-cell") || odp_node_is(cell, "table:covered-table-cell")) {
                count += repeat_of(cell, "table:number-columns-repeated");
            }
        }
        cols = MAX(cols, MIN(count, MAX_COLUMNS));
        for (guint k = repeat_of(n, "table:number-rows-repeated"); k > 0; k--) {
            g_ptr_array_add(rows, (gpointer)n);
        }
    }
    if (cols == 0 || rows->len == 0) {
        g_ptr_array_free(column_nodes, TRUE);
        g_ptr_array_free(row_nodes, TRUE);
        g_ptr_array_free(rows, TRUE);
        g_ptr_array_free(col_styles, TRUE);
        g_array_free(col_widths, TRUE);
        return NULL;
    }
    guint nrows = MIN(rows->len, MAX_CELLS / cols);
    SlideTable* t = slide_table_new(nrows, cols);
    gdouble total = 0;
    for (guint c = 0; c < cols; c++) {
        t->col_widths[c] = c < col_widths->len ? g_array_index(col_widths, gdouble, c) : 0;
        total += t->col_widths[c];
    }
    if (total <= 0) {
        for (guint c = 0; c < cols; c++) {
            t->col_widths[c] = width / cols;
        }
    }
    const gchar* template_name = odp_node_attr(table, "table:template-name");
    t->border = SLIDE_NO_COLOR;
    gboolean border_seen = FALSE;
    for (guint r = 0; r < nrows; r++) {
        const OdpNode* row = g_ptr_array_index(rows, r);
        OdpCascade rc = { { 0 }, 0 };
        odp_cascade_add(&rc, ctx->styles, "table-row", odp_node_attr(row, "table:style-name"));
        gdouble h = odp_length(odp_cascade_get(&rc, "style:row-height"), 0);
        if (h <= 0) {
            h = odp_length(odp_cascade_get(&rc, "style:min-row-height"), 0);
        }
        t->row_heights[r] = h > 0 ? h : height / nrows;
        const gchar* row_default = odp_node_attr(row, "table:default-cell-style-name");
        guint c = 0;
        for (guint k = 0; k < row->children->len && c < cols; k++) {
            const OdpNode* node = g_ptr_array_index(row->children, k);
            gboolean covered = odp_node_is(node, "table:covered-table-cell");
            if (!covered && !odp_node_is(node, "table:table-cell")) {
                continue;
            }
            for (guint rep = repeat_of(node, "table:number-columns-repeated"); rep > 0 && c < cols; rep--, c++) {
                SlideCell* cell = slide_table_cell(t, r, c);
                if (covered) {
                    cell->merged = TRUE;
                    continue;
                }
                const gchar* spans = odp_node_attr(node, "table:number-columns-spanned");
                const gchar* rspans = odp_node_attr(node, "table:number-rows-spanned");
                cell->col_span = (guint)CLAMP(spans != NULL ? atoi(spans) : 1, 1, (gint)cols);
                cell->row_span = (guint)CLAMP(rspans != NULL ? atoi(rspans) : 1, 1, (gint)nrows);

                const gchar* style = odp_node_attr(node, "table:style-name");
                if (style == NULL) style = row_default;
                if (style == NULL && c < col_styles->len) style = g_ptr_array_index(col_styles, c);
                OdpCascade cc = { { 0 }, 0 };
                odp_cascade_add(&cc, ctx->styles, "table-cell", style);
                const gchar* part = odp_styles_template_part(ctx->styles, template_name,
                                                             template_part(table, r, c, nrows, cols));
                if (part == NULL) part = odp_styles_template_part(ctx->styles, template_name, "body");
                odp_cascade_add(&cc, ctx->styles, "table-cell", part);
                cell->fill = cell_fill(&cc);
                const gchar* valign = odp_cascade_get(&cc, "draw:textarea-vertical-align");
                if (valign == NULL) valign = odp_cascade_get(&cc, "style:vertical-align");
                cell->anchor = g_strcmp0(valign, "middle") == 0   ? SLIDE_ANCHOR_MIDDLE
                               : g_strcmp0(valign, "bottom") == 0 ? SLIDE_ANCHOR_BOTTOM
                                                                  : SLIDE_ANCHOR_TOP;
                if (!border_seen) {
                    const gchar* border = odp_cascade_get(&cc, "fo:border");
                    if (border == NULL) border = odp_cascade_get(&cc, "fo:border-bottom");
                    if (border != NULL) {
                        t->border = border_color(border);
                        border_seen = TRUE;
                    }
                }
                OdpTextContext sub = *ctx;
                sub.base.n = 0;
                odp_cascade_append(&sub.base, &cc);
                odp_cascade_append(&sub.base, &ctx->base);
                sub.list = NULL;
                sub.outline_prefix = NULL;
                if (cell->fill != SLIDE_NO_COLOR) {
                    sub.background = cell->fill;
                }
                odp_text_paragraphs(&sub, node, cell->paragraphs);
            }
        }
    }
    /* Cells covered by a span */
    for (guint r = 0; r < nrows; r++) {
        for (guint c = 0; c < cols; c++) {
            const SlideCell* cell = slide_table_cell(t, r, c);
            if (cell->merged) {
                continue;
            }
            for (guint dr = 0; dr < cell->row_span; dr++) {
                for (guint dc = 0; dc < cell->col_span; dc++) {
                    SlideCell* o = slide_table_cell(t, r + dr, c + dc);
                    if (o != NULL && (dr > 0 || dc > 0)) {
                        o->merged = TRUE;
                    }
                }
            }
        }
    }
    g_ptr_array_free(column_nodes, TRUE);
    g_ptr_array_free(row_nodes, TRUE);
    g_ptr_array_free(rows, TRUE);
    g_ptr_array_free(col_styles, TRUE);
    g_array_free(col_widths, TRUE);
    return t;
}
