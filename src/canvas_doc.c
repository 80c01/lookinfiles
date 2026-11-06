#include "canvas_doc.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TEXT_WIDTH 300

/* Style names of the "s" attribute of <span>, in CanvasStyle bit order */
static const gchar* const STYLE_NAMES[] = {
    "b", "i", "u", "s", "code", "h",
    "h2", "mark",
    "serif", "mono",
    "sm", "lg",
    "c-muted", "c-accent", "c-danger", "c-success", "c-warning"
};

static const gchar* const SHAPE_NAMES[] = {
    "rect", "round_rect", "ellipse", "diamond", "line", "arrow", "freehand"
};

static const gchar* const FILL_NAMES[] = {
    "none", "solid", "sketch"
};

/* ------------------------------------------------------------------------- */
/* Blocks                                                                    */
/* ------------------------------------------------------------------------- */

static CanvasBlock* block_new(CanvasBlockKind kind, gint x, gint y, gint w, gint h) {
    CanvasBlock* b = g_new0(CanvasBlock, 1);
    b->kind = kind;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    if (kind == CANVAS_BLOCK_TEXT) {
        b->text = g_strdup("");
        b->runs = g_array_new(FALSE, FALSE, sizeof(CanvasRun));
    } else if (kind == CANVAS_BLOCK_IMAGE) {
        b->crop_w = 1.0;
        b->crop_h = 1.0;
    } else if (kind == CANVAS_BLOCK_SHAPE) {
        b->stroke_color = g_strdup("fg");
        b->stroke_width = 2;
        b->rough = TRUE;
        b->points = g_array_new(FALSE, FALSE, sizeof(CanvasPoint));
    } else if (kind == CANVAS_BLOCK_TABLE) {
        b->cells = g_ptr_array_new_with_free_func(g_free);
        b->col_widths = g_array_new(FALSE, TRUE, sizeof(gint));
    }
    return b;
}

static void block_free(gpointer data) {
    CanvasBlock* b = data;
    if (b == NULL) return;
    g_free(b->text);
    if (b->runs != NULL) g_array_unref(b->runs);
    g_free(b->src);
    g_free(b->stroke_color);
    if (b->points != NULL) g_array_unref(b->points);
    if (b->cells != NULL) g_ptr_array_unref(b->cells);
    if (b->col_widths != NULL) g_array_unref(b->col_widths);
    g_free(b);
}

CanvasDoc* canvas_doc_new(void) {
    CanvasDoc* doc = g_new0(CanvasDoc, 1);
    doc->blocks = g_ptr_array_new_with_free_func(block_free);
    return doc;
}

void canvas_doc_free(CanvasDoc* doc) {
    if (doc == NULL) return;
    g_ptr_array_unref(doc->blocks);
    g_free(doc);
}

CanvasBlock* canvas_doc_add_text(CanvasDoc* doc, gint x, gint y, gint w, const gchar* text) {
    CanvasBlock* b = block_new(CANVAS_BLOCK_TEXT, x, y, w > 0 ? w : DEFAULT_TEXT_WIDTH, 0);
    if (text != NULL) {
        g_free(b->text);
        b->text = g_utf8_make_valid(text, -1);
    }
    g_ptr_array_add(doc->blocks, b);
    return b;
}

CanvasBlock* canvas_doc_add_image(CanvasDoc* doc, gint x, gint y, gint w, gint h, const gchar* src) {
    CanvasBlock* b = block_new(CANVAS_BLOCK_IMAGE, x, y, w, h);
    b->src = g_strdup(src != NULL ? src : "");
    g_ptr_array_add(doc->blocks, b);
    return b;
}

CanvasBlock* canvas_doc_add_shape(CanvasDoc* doc, gint x, gint y, gint w, gint h, CanvasShapeType shape_type) {
    CanvasBlock* b = block_new(CANVAS_BLOCK_SHAPE, x, y, MAX(w, 16), MAX(h, 16));
    b->shape_type = shape_type;
    g_ptr_array_add(doc->blocks, b);
    return b;
}

CanvasBlock* canvas_doc_add_table(CanvasDoc* doc, gint x, gint y, gint rows, gint cols) {
    rows = CLAMP(rows, 1, 50);
    cols = CLAMP(cols, 1, 20);
    CanvasBlock* b = block_new(CANVAS_BLOCK_TABLE, x, y, cols * 120, rows * 32);
    b->table_rows = rows;
    b->table_cols = cols;
    for (gint i = 0; i < rows * cols; i++) {
        g_ptr_array_add(b->cells, g_strdup(""));
    }
    for (gint c = 0; c < cols; c++) {
        gint width = 120;
        g_array_append_val(b->col_widths, width);
    }
    g_ptr_array_add(doc->blocks, b);
    return b;
}

const gchar* canvas_table_get_cell(const CanvasBlock* b, gint row, gint col) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->cells == NULL) return "";
    if (row < 0 || row >= b->table_rows || col < 0 || col >= b->table_cols) return "";
    guint idx = (guint)(row * b->table_cols + col);
    if (idx >= b->cells->len) return "";
    const gchar* txt = g_ptr_array_index(b->cells, idx);
    return txt != NULL ? txt : "";
}

void canvas_table_set_cell(CanvasBlock* b, gint row, gint col, const gchar* text) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->cells == NULL) return;
    if (row < 0 || row >= b->table_rows || col < 0 || col >= b->table_cols) return;
    guint idx = (guint)(row * b->table_cols + col);
    if (idx >= b->cells->len) return;
    gchar* valid = g_utf8_make_valid(text != NULL ? text : "", -1);
    g_ptr_array_index(b->cells, idx) = valid;
}

gint canvas_table_get_col_width(const CanvasBlock* b, gint col) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->col_widths == NULL) return 120;
    if (col < 0 || (guint)col >= b->col_widths->len) return 120;
    return g_array_index(b->col_widths, gint, col);
}

void canvas_table_set_col_width(CanvasBlock* b, gint col, gint width) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->col_widths == NULL) return;
    if (col < 0 || (guint)col >= b->col_widths->len) return;
    g_array_index(b->col_widths, gint, col) = MAX(width, 40);
    gint total = 0;
    for (guint c = 0; c < b->col_widths->len; c++) total += g_array_index(b->col_widths, gint, c);
    b->w = total;
}

void canvas_table_insert_row(CanvasBlock* b, gint at_row) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->cells == NULL) return;
    at_row = CLAMP(at_row, 0, b->table_rows);
    gint insert_idx = at_row * b->table_cols;
    for (gint c = 0; c < b->table_cols; c++) {
        g_ptr_array_insert(b->cells, insert_idx + c, g_strdup(""));
    }
    b->table_rows++;
    b->h = b->table_rows * 32;
}

void canvas_table_insert_col(CanvasBlock* b, gint at_col) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->cells == NULL) return;
    at_col = CLAMP(at_col, 0, b->table_cols);
    for (gint r = b->table_rows - 1; r >= 0; r--) {
        gint insert_idx = r * b->table_cols + at_col;
        g_ptr_array_insert(b->cells, insert_idx, g_strdup(""));
    }
    b->table_cols++;
    gint default_w = 120;
    g_array_insert_val(b->col_widths, at_col, default_w);
    b->w += default_w;
}

void canvas_table_delete_row(CanvasBlock* b, gint row) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->table_rows <= 1) return;
    if (row < 0 || row >= b->table_rows) return;
    gint start_idx = row * b->table_cols;
    for (gint c = 0; c < b->table_cols; c++) {
        g_ptr_array_remove_index(b->cells, start_idx);
    }
    b->table_rows--;
    b->h = b->table_rows * 32;
}

void canvas_table_delete_col(CanvasBlock* b, gint col) {
    if (b == NULL || b->kind != CANVAS_BLOCK_TABLE || b->table_cols <= 1) return;
    if (col < 0 || col >= b->table_cols) return;
    for (gint r = b->table_rows - 1; r >= 0; r--) {
        gint remove_idx = r * b->table_cols + col;
        g_ptr_array_remove_index(b->cells, remove_idx);
    }
    gint w = g_array_index(b->col_widths, gint, col);
    g_array_remove_index(b->col_widths, col);
    b->table_cols--;
    b->w = MAX(b->w - w, 40);
}

gboolean canvas_doc_remove(CanvasDoc* doc, CanvasBlock* block) {
    return g_ptr_array_remove(doc->blocks, block);
}

void canvas_doc_raise(CanvasDoc* doc, CanvasBlock* block) {
    guint i;
    if (!g_ptr_array_find(doc->blocks, block, &i)) return;
    g_ptr_array_steal_index(doc->blocks, i);
    g_ptr_array_add(doc->blocks, block);
}

void canvas_block_add_run(CanvasBlock* block, gint start, gint end, guint styles) {
    if (block == NULL || block->kind != CANVAS_BLOCK_TEXT) return;
    gint len = (gint)g_utf8_strlen(block->text, -1);
    start = CLAMP(start, 0, len);
    end = CLAMP(end, 0, len);
    if (start >= end || styles == 0) return;

    /* Styles of each character, then runs of equal styles again */
    guint* flags = g_new0(guint, len);
    for (guint i = 0; i < block->runs->len; i++) {
        CanvasRun* r = &g_array_index(block->runs, CanvasRun, i);
        for (gint c = MAX(r->start, 0); c < MIN(r->end, len); c++) flags[c] |= r->styles;
    }
    for (gint c = start; c < end; c++) flags[c] |= styles;

    g_array_set_size(block->runs, 0);
    gint c = 0;
    while (c < len) {
        gint s = c;
        while (c < len && flags[c] == flags[s]) c++;
        if (flags[s] != 0) {
            CanvasRun r = {s, c, flags[s]};
            g_array_append_val(block->runs, r);
        }
    }
    g_free(flags);
}

/* ------------------------------------------------------------------------- */
/* page.xml                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    CanvasDoc* doc;
    gboolean in_canvas;
    CanvasBlock* text_block;  /* <text> being read */
    CanvasBlock* table_block; /* <table> being read */
    gint cell_row;
    gint cell_col;
    GString* text;
    gint n_chars;
    GArray* spans; /* open <span>s: CanvasRun with end unused */
} ParseState;

static const gchar* attr(const gchar** names, const gchar** values, const gchar* name) {
    for (gint i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], name) == 0) return values[i];
    }
    return NULL;
}

static gint attr_int(const gchar** names, const gchar** values, const gchar* name) {
    const gchar* v = attr(names, values, name);
    return (v != NULL) ? (gint)g_ascii_strtoll(v, NULL, 10) : 0;
}

static guint parse_styles(const gchar* s) {
    guint styles = 0;
    if (s == NULL) return 0;
    gchar** words = g_strsplit_set(s, " ,", -1);
    for (gint i = 0; words[i] != NULL; i++) {
        for (guint k = 0; k < G_N_ELEMENTS(STYLE_NAMES); k++) {
            if (strcmp(words[i], STYLE_NAMES[k]) == 0) styles |= 1u << k;
        }
    }
    g_strfreev(words);
    return styles;
}

static void on_start(GMarkupParseContext* ctx, const gchar* element, const gchar** names,
                     const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    ParseState* st = data;
    if (!st->in_canvas) {
        if (strcmp(element, "canvas") != 0) {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "not a canvas page");
            return;
        }
        st->in_canvas = TRUE;
        return;
    }
    if (st->table_block != NULL) {
        if (strcmp(element, "cell") == 0) {
            st->cell_row = attr_int(names, values, "row");
            st->cell_col = attr_int(names, values, "col");
            g_string_truncate(st->text, 0);
        }
        return;
    }
    if (st->text_block != NULL) {
        if (strcmp(element, "span") == 0) {
            CanvasRun r = {st->n_chars, 0, parse_styles(attr(names, values, "s"))};
            g_array_append_val(st->spans, r);
        }
        return;
    }
    gint x = attr_int(names, values, "x");
    gint y = attr_int(names, values, "y");
    gint w = attr_int(names, values, "w");
    gint h = attr_int(names, values, "h");
    if (strcmp(element, "text") == 0) {
        st->text_block = canvas_doc_add_text(st->doc, x, y, w, NULL);
        st->text_block->h = MAX(h, 0);
        g_string_truncate(st->text, 0);
        st->n_chars = 0;
    } else if (strcmp(element, "image") == 0) {
        const gchar* src = attr(names, values, "src");
        if (src != NULL && src[0] != '\0') {
            CanvasBlock* b = canvas_doc_add_image(st->doc, x, y, w, h, src);
            const gchar* crop = attr(names, values, "crop");
            if (crop != NULL) {
                gchar** parts = g_strsplit(crop, ",", 4);
                if (parts[0] && parts[1] && parts[2] && parts[3]) {
                    b->crop_x = g_ascii_strtod(parts[0], NULL);
                    b->crop_y = g_ascii_strtod(parts[1], NULL);
                    b->crop_w = g_ascii_strtod(parts[2], NULL);
                    b->crop_h = g_ascii_strtod(parts[3], NULL);
                }
                g_strfreev(parts);
            }
        }
    } else if (strcmp(element, "shape") == 0) {
        const gchar* type_str = attr(names, values, "type");
        CanvasShapeType stype = CANVAS_SHAPE_RECT;
        for (guint k = 0; k < G_N_ELEMENTS(SHAPE_NAMES); k++) {
            if (g_strcmp0(type_str, SHAPE_NAMES[k]) == 0) {
                stype = (CanvasShapeType)k;
                break;
            }
        }
        CanvasBlock* b = canvas_doc_add_shape(st->doc, x, y, w, h, stype);
        const gchar* stroke = attr(names, values, "stroke");
        if (stroke != NULL) {
            g_free(b->stroke_color);
            b->stroke_color = g_strdup(stroke);
        }
        const gchar* fill = attr(names, values, "fill");
        if (fill != NULL) {
            for (guint k = 0; k < G_N_ELEMENTS(FILL_NAMES); k++) {
                if (strcmp(fill, FILL_NAMES[k]) == 0) {
                    b->fill_style = (CanvasFillStyle)k;
                    break;
                }
            }
        }
        b->stroke_width = MAX(attr_int(names, values, "stroke_width"), 1);
        b->rough = (attr_int(names, values, "rough") != 0);
        const gchar* pts = attr(names, values, "points");
        if (pts != NULL && pts[0] != '\0') {
            gchar** pairs = g_strsplit(pts, " ", -1);
            for (gint i = 0; pairs[i] != NULL; i++) {
                gchar* comma = strchr(pairs[i], ',');
                if (comma != NULL) {
                    *comma = '\0';
                    CanvasPoint pt = {g_ascii_strtod(pairs[i], NULL), g_ascii_strtod(comma + 1, NULL)};
                    g_array_append_val(b->points, pt);
                }
            }
            g_strfreev(pairs);
        }
    } else if (strcmp(element, "table") == 0) {
        gint rows = MAX(attr_int(names, values, "rows"), 1);
        gint cols = MAX(attr_int(names, values, "cols"), 1);
        st->table_block = canvas_doc_add_table(st->doc, x, y, rows, cols);
        if (w > 0) st->table_block->w = w;
        if (h > 0) st->table_block->h = h;
        const gchar* cw = attr(names, values, "col_widths");
        if (cw != NULL) {
            gchar** widths = g_strsplit(cw, ",", -1);
            for (gint i = 0; widths[i] != NULL && i < cols; i++) {
                gint colw = (gint)g_ascii_strtoll(widths[i], NULL, 10);
                if (colw > 0) canvas_table_set_col_width(st->table_block, i, colw);
            }
            g_strfreev(widths);
        }
    }
}

static void on_end(GMarkupParseContext* ctx, const gchar* element, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ParseState* st = data;
    if (st->table_block != NULL) {
        if (strcmp(element, "cell") == 0) {
            canvas_table_set_cell(st->table_block, st->cell_row, st->cell_col, st->text->str);
        } else if (strcmp(element, "table") == 0) {
            st->table_block = NULL;
        }
        return;
    }
    if (st->text_block == NULL) return;
    if (strcmp(element, "span") == 0 && st->spans->len > 0) {
        CanvasRun r = g_array_index(st->spans, CanvasRun, st->spans->len - 1);
        g_array_set_size(st->spans, st->spans->len - 1);
        /* The runs refer to the text read so far */
        g_free(st->text_block->text);
        st->text_block->text = g_strdup(st->text->str);
        canvas_block_add_run(st->text_block, r.start, st->n_chars, r.styles);
    } else if (strcmp(element, "text") == 0) {
        g_free(st->text_block->text);
        st->text_block->text = g_strdup(st->text->str);
        st->text_block = NULL;
        g_array_set_size(st->spans, 0);
    }
}

static void on_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ParseState* st = data;
    if (st->text_block == NULL && st->table_block == NULL) return;
    gchar* valid = g_utf8_make_valid(text, (gssize)len);
    g_string_append(st->text, valid);
    st->n_chars += (gint)g_utf8_strlen(valid, -1);
    g_free(valid);
}

CanvasDoc* canvas_doc_parse(const gchar* xml, gssize length, GError** error) {
    if (xml == NULL) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, "empty canvas page");
        return NULL;
    }
    ParseState st = {0};
    st.doc = canvas_doc_new();
    st.text = g_string_new(NULL);
    st.spans = g_array_new(FALSE, FALSE, sizeof(CanvasRun));
    GMarkupParser parser = {on_start, on_end, on_text, NULL, NULL};
    GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, &st, NULL);
    gboolean ok = g_markup_parse_context_parse(ctx, xml, length, error) &&
                  g_markup_parse_context_end_parse(ctx, error);
    g_markup_parse_context_free(ctx);
    g_string_free(st.text, TRUE);
    g_array_unref(st.spans);
    if (ok && !st.in_canvas) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, "empty canvas page");
        ok = FALSE;
    }
    if (!ok) {
        canvas_doc_free(st.doc);
        return NULL;
    }
    return st.doc;
}

static void append_escaped(GString* out, const gchar* start, const gchar* end) {
    gchar* esc = g_markup_escape_text(start, end - start);
    g_string_append(out, esc);
    g_free(esc);
}

static void append_styles(GString* out, guint styles) {
    gboolean first = TRUE;
    for (guint k = 0; k < G_N_ELEMENTS(STYLE_NAMES); k++) {
        if ((styles & (1u << k)) == 0) continue;
        if (!first) g_string_append_c(out, ' ');
        g_string_append(out, STYLE_NAMES[k]);
        first = FALSE;
    }
}

static void append_text_block(GString* out, const CanvasBlock* b) {
    g_string_append_printf(out, "  <text x=\"%d\" y=\"%d\" w=\"%d\"", b->x, b->y, b->w);
    if (b->h > 0) g_string_append_printf(out, " h=\"%d\"", b->h);
    g_string_append_c(out, '>');
    const gchar* text = b->text;
    gint len = (gint)g_utf8_strlen(text, -1);
    gint pos = 0;
    for (guint i = 0; i < b->runs->len; i++) {
        const CanvasRun* r = &g_array_index(b->runs, CanvasRun, i);
        gint start = CLAMP(r->start, pos, len);
        gint end = CLAMP(r->end, start, len);
        if (start == end) continue;
        append_escaped(out, g_utf8_offset_to_pointer(text, pos), g_utf8_offset_to_pointer(text, start));
        g_string_append(out, "<span s=\"");
        append_styles(out, r->styles);
        g_string_append(out, "\">");
        append_escaped(out, g_utf8_offset_to_pointer(text, start), g_utf8_offset_to_pointer(text, end));
        g_string_append(out, "</span>");
        pos = end;
    }
    append_escaped(out, g_utf8_offset_to_pointer(text, pos), text + strlen(text));
    g_string_append(out, "</text>\n");
}

gchar* canvas_doc_to_xml(const CanvasDoc* doc) {
    GString* out = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<canvas version=\"1\">\n");
    for (guint i = 0; doc != NULL && i < doc->blocks->len; i++) {
        const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
        if (b->kind == CANVAS_BLOCK_TEXT) {
            append_text_block(out, b);
        } else if (b->kind == CANVAS_BLOCK_IMAGE) {
            if (b->crop_w > 0 && b->crop_h > 0 &&
                (b->crop_x > 0.001 || b->crop_y > 0.001 || b->crop_w < 0.999 || b->crop_h < 0.999)) {
                gchar* line = g_markup_printf_escaped(
                    "  <image x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" src=\"%s\" crop=\"%.4f,%.4f,%.4f,%.4f\"/>\n",
                    b->x, b->y, b->w, b->h, b->src, b->crop_x, b->crop_y, b->crop_w, b->crop_h);
                g_string_append(out, line);
                g_free(line);
            } else {
                gchar* line = g_markup_printf_escaped("  <image x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" src=\"%s\"/>\n",
                                                      b->x, b->y, b->w, b->h, b->src);
                g_string_append(out, line);
                g_free(line);
            }
        } else if (b->kind == CANVAS_BLOCK_SHAPE) {
            GString* pts_str = g_string_new(NULL);
            if (b->points != NULL) {
                for (guint p = 0; p < b->points->len; p++) {
                    const CanvasPoint* pt = &g_array_index(b->points, CanvasPoint, p);
                    if (p > 0) g_string_append_c(pts_str, ' ');
                    g_string_append_printf(pts_str, "%.1f,%.1f", pt->x, pt->y);
                }
            }
            gchar* line = g_markup_printf_escaped(
                "  <shape type=\"%s\" x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" stroke=\"%s\" fill=\"%s\" stroke_width=\"%d\" rough=\"%d\" points=\"%s\"/>\n",
                SHAPE_NAMES[b->shape_type], b->x, b->y, b->w, b->h,
                b->stroke_color != NULL ? b->stroke_color : "fg",
                FILL_NAMES[b->fill_style], b->stroke_width, b->rough ? 1 : 0, pts_str->str);
            g_string_append(out, line);
            g_free(line);
            g_string_free(pts_str, TRUE);
        } else if (b->kind == CANVAS_BLOCK_TABLE) {
            GString* cw_str = g_string_new(NULL);
            if (b->col_widths != NULL) {
                for (guint c = 0; c < b->col_widths->len; c++) {
                    if (c > 0) g_string_append_c(cw_str, ',');
                    g_string_append_printf(cw_str, "%d", g_array_index(b->col_widths, gint, c));
                }
            }
            g_string_append_printf(out, "  <table x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" rows=\"%d\" cols=\"%d\" col_widths=\"%s\">\n",
                                   b->x, b->y, b->w, b->h, b->table_rows, b->table_cols, cw_str->str);
            g_string_free(cw_str, TRUE);
            for (gint r = 0; r < b->table_rows; r++) {
                for (gint c = 0; c < b->table_cols; c++) {
                    const gchar* cell_text = canvas_table_get_cell(b, r, c);
                    gchar* esc = g_markup_escape_text(cell_text, -1);
                    g_string_append_printf(out, "    <cell row=\"%d\" col=\"%d\">%s</cell>\n", r, c, esc);
                    g_free(esc);
                }
            }
            g_string_append(out, "  </table>\n");
        }
    }
    g_string_append(out, "</canvas>\n");
    return g_string_free(out, FALSE);
}

CanvasDoc* canvas_doc_load(const gchar* dir, GError** error) {
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "no canvas note at %s", dir);
        return NULL;
    }
    gchar* path = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    gchar* xml = NULL;
    gsize len = 0;
    CanvasDoc* doc = NULL;
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        doc = canvas_doc_new();
    } else if (g_file_get_contents(path, &xml, &len, error)) {
        doc = canvas_doc_parse(xml, (gssize)len, error);
    }
    g_free(xml);
    g_free(path);
    return doc;
}

gboolean canvas_doc_save(const CanvasDoc* doc, const gchar* dir, GError** error) {
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "cannot create %s", dir);
        return FALSE;
    }
    gchar* path = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    gchar* xml = canvas_doc_to_xml(doc);
    gboolean ok = g_file_set_contents(path, xml, -1, error);
    g_free(xml);
    g_free(path);
    return ok;
}

#define STARTER_X 40
#define STARTER_Y 32
#define STARTER_WIDTH 480

gboolean canvas_doc_create(const gchar* dir, const gchar* title, GError** error) {
    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    if (title != NULL && title[0] != '\0') {
        CanvasBlock* b = canvas_doc_add_text(doc, STARTER_X, STARTER_Y, STARTER_WIDTH, title);
        canvas_block_add_run(b, 0, (gint)g_utf8_strlen(b->text, -1), CANVAS_STYLE_HEADING);
    }
    return canvas_doc_save(doc, dir, error);
}

gboolean canvas_doc_delete(const gchar* dir) {
    gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    GDir* d = g_dir_open(media, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* full = g_build_filename(media, name, NULL);
            if (g_file_test(full, G_FILE_TEST_IS_REGULAR)) g_remove(full);
            g_free(full);
        }
        g_dir_close(d);
    }
    g_rmdir(media);
    g_free(media);
    gchar* page = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    g_remove(page);
    g_free(page);
    g_rmdir(dir);
    return !g_file_test(dir, G_FILE_TEST_EXISTS);
}

/* ------------------------------------------------------------------------- */
/* Media                                                                     */
/* ------------------------------------------------------------------------- */

guint canvas_doc_prune_media(const CanvasDoc* doc, const gchar* dir) {
    gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    GDir* d = g_dir_open(media, 0, NULL);
    guint removed = 0;
    if (d != NULL) {
        GHashTable* used = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint i = 0; i < doc->blocks->len; i++) {
            const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
            if (b->kind == CANVAS_BLOCK_IMAGE) g_hash_table_add(used, b->src);
        }
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* rel = g_strconcat(CANVAS_MEDIA_DIR "/", name, NULL);
            if (!g_hash_table_contains(used, rel)) {
                gchar* full = g_build_filename(media, name, NULL);
                if (g_file_test(full, G_FILE_TEST_IS_REGULAR) && g_remove(full) == 0) removed++;
                g_free(full);
            }
            g_free(rel);
        }
        g_hash_table_unref(used);
        g_dir_close(d);
    }
    g_free(media);
    return removed;
}

gchar* canvas_doc_new_media_path(const gchar* dir, const gchar* ext) {
    gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    GDir* d = g_dir_open(media, 0, NULL);
    guint64 max = 0;
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* end = NULL;
            guint64 n = g_ascii_strtoull(name, &end, 10);
            if (end != name && n > max) max = n;
        }
        g_dir_close(d);
    }
    g_free(media);
    const gchar* e = (ext != NULL && ext[0] != '\0') ? ext : "png";
    if (e[0] == '.') e++;
    return g_strdup_printf(CANVAS_MEDIA_DIR "/%04" G_GUINT64_FORMAT ".%s", max + 1, e);
}

/* ------------------------------------------------------------------------- */
/* Markdown export                                                           */
/* ------------------------------------------------------------------------- */

static gint compare_position(gconstpointer a, gconstpointer b) {
    const CanvasBlock* x = *(const CanvasBlock* const*)a;
    const CanvasBlock* y = *(const CanvasBlock* const*)b;
    if (x->y != y->y) return (x->y < y->y) ? -1 : 1;
    if (x->x != y->x) return (x->x < y->x) ? -1 : 1;
    return 0;
}

/* One line of a styled segment, wrapped in the Markdown markers of its styles */
static void append_md_line(GString* out, const gchar* start, const gchar* end, guint styles, gboolean line_start) {
    if (start == end) return;
    if (line_start) {
        if (styles & CANVAS_STYLE_HEADING) g_string_append(out, "# ");
        else if (styles & CANVAS_STYLE_SUBHEADING) g_string_append(out, "## ");
    }
    const gchar* open = (styles & CANVAS_STYLE_CODE) ? "`" : "";
    GString* marks = g_string_new(NULL);
    if (!(styles & CANVAS_STYLE_CODE)) {
        if (styles & CANVAS_STYLE_BOLD) g_string_append(marks, "**");
        if (styles & CANVAS_STYLE_ITALIC) g_string_append(marks, "*");
        if (styles & CANVAS_STYLE_STRIKE) g_string_append(marks, "~~");
        if (styles & CANVAS_STYLE_HIGHLIGHT) g_string_append(marks, "==");
    }
    g_string_append(out, open);
    g_string_append(out, marks->str);
    g_string_append_len(out, start, end - start);
    g_string_append(out, g_strreverse(marks->str));
    g_string_append(out, open);
    g_string_free(marks, TRUE);
}

static void append_md_segment(GString* out, const gchar* start, const gchar* end, guint styles) {
    const gchar* p = start;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', end - p);
        const gchar* line_end = (nl != NULL) ? nl : end;
        gboolean line_start = (out->len == 0 || out->str[out->len - 1] == '\n');
        append_md_line(out, p, line_end, styles, line_start);
        if (nl == NULL) break;
        g_string_append_c(out, '\n');
        p = nl + 1;
    }
}

static void append_md_text(GString* out, const CanvasBlock* b) {
    const gchar* text = b->text;
    gint len = (gint)g_utf8_strlen(text, -1);
    gint pos = 0;
    for (guint i = 0; i <= b->runs->len; i++) {
        const CanvasRun* r = (i < b->runs->len) ? &g_array_index(b->runs, CanvasRun, i) : NULL;
        gint start = (r != NULL) ? CLAMP(r->start, pos, len) : len;
        gint end = (r != NULL) ? CLAMP(r->end, start, len) : len;
        append_md_segment(out, g_utf8_offset_to_pointer(text, pos), g_utf8_offset_to_pointer(text, start), 0);
        if (r != NULL) {
            append_md_segment(out, g_utf8_offset_to_pointer(text, start), g_utf8_offset_to_pointer(text, end),
                              r->styles);
        }
        pos = end;
    }
}

gchar* canvas_doc_export_markdown(const gchar* dir, GError** error) {
    g_autoptr(CanvasDoc) doc = canvas_doc_load(dir, error);
    if (doc == NULL) return NULL;
    gchar* base = g_path_get_basename(dir);
    gchar* parent = g_path_get_dirname(dir);
    gchar* prefix = g_strconcat(base, "/", NULL);
    if (g_str_has_suffix(base, CANVAS_EXT)) base[strlen(base) - strlen(CANVAS_EXT)] = '\0';

    gchar* path = NULL;
    for (gint n = 1; path == NULL || g_file_test(path, G_FILE_TEST_EXISTS); n++) {
        g_free(path);
        gchar* name = (n == 1) ? g_strconcat(base, ".md", NULL) : g_strdup_printf("%s (%d).md", base, n);
        path = g_build_filename(parent, name, NULL);
        g_free(name);
    }
    gchar* md = canvas_doc_to_markdown(doc, prefix);
    if (!g_file_set_contents(path, md, -1, error)) g_clear_pointer(&path, g_free);
    g_free(md);
    g_free(prefix);
    g_free(parent);
    g_free(base);
    return path;
}

gchar* canvas_doc_to_markdown(const CanvasDoc* doc, const gchar* media_prefix) {
    GPtrArray* sorted = g_ptr_array_new();
    for (guint i = 0; i < doc->blocks->len; i++) g_ptr_array_add(sorted, g_ptr_array_index(doc->blocks, i));
    g_ptr_array_sort(sorted, compare_position);

    GString* out = g_string_new(NULL);
    for (guint i = 0; i < sorted->len; i++) {
        const CanvasBlock* b = g_ptr_array_index(sorted, i);
        if (b->kind == CANVAS_BLOCK_TEXT && b->text[0] == '\0') continue;
        if (out->len > 0) g_string_append(out, "\n\n");
        if (b->kind == CANVAS_BLOCK_TEXT) {
            append_md_text(out, b);
        } else if (b->kind == CANVAS_BLOCK_TABLE) {
            for (gint r = 0; r < b->table_rows; r++) {
                g_string_append(out, "|");
                for (gint c = 0; c < b->table_cols; c++) {
                    g_string_append_printf(out, " %s |", canvas_table_get_cell(b, r, c));
                }
                g_string_append_c(out, '\n');
                if (r == 0) {
                    g_string_append(out, "|");
                    for (gint c = 0; c < b->table_cols; c++) {
                        g_string_append(out, " --- |");
                    }
                    g_string_append_c(out, '\n');
                }
            }
        } else if (b->kind == CANVAS_BLOCK_IMAGE) {
            gchar* link = g_strconcat(media_prefix != NULL ? media_prefix : "", b->src, NULL);
            /* A link with spaces goes between angle brackets */
            g_string_append_printf(out, strchr(link, ' ') != NULL ? "![](<%s>)" : "![](%s)", link);
            g_free(link);
        }
    }
    if (out->len > 0) g_string_append_c(out, '\n');
    g_ptr_array_unref(sorted);
    return g_string_free(out, FALSE);
}
