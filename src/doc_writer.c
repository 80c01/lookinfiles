/* Word processing documents written as preview documents (see doc_writer.h) */
#include "doc_writer.h"

#include <string.h>

#define PX_PER_PT (4.0 / 3.0)     /* 96 dpi */
#define MAX_INDENT_PX 400
#define MAX_SPACE_PX 48
#define MAX_IMAGE_PX 1600
#define MAX_CELL_WIDTH 40          /* characters of a column before its text wraps */
#define MAX_TABLE_WIDTH 110        /* characters of a whole table line */
#define MIN_SHRUNK_WIDTH 8
#define MAX_COLUMNS 32
#define MAX_TABLE_ROWS 500
#define LABEL_GAP "\xE2\x80\x82"   /* en space after a list label */

void doc_run_init(DocRun* run) {
    memset(run, 0, sizeof *run);
    run->color = DOC_COLOR_NONE;
    run->highlight = DOC_COLOR_NONE;
}

void doc_para_init(DocPara* para) {
    memset(para, 0, sizeof *para);
    para->align = PREVIEW_ALIGN_LEFT;
}

/* ---- Paragraph contents ---- */

typedef struct {
    gchar* text;       /* NULL for a picture */
    DocRun run;        /* run.font is owned (copy) */
    GBytes* image;
    gint image_width;  /* pixels, 0: its own */
    gchar* image_name;
} Seg;

typedef struct {
    GPtrArray* segs;   /* Seg* */
    PreviewAlign align;
} Para;

static void seg_free(gpointer data) {
    Seg* s = data;
    g_free(s->text);
    g_free((gchar*)s->run.font);
    if (s->image != NULL) {
        g_bytes_unref(s->image);
    }
    g_free(s->image_name);
    g_free(s);
}

static Para* para_new(void) {
    Para* p = g_new0(Para, 1);
    p->segs = g_ptr_array_new_with_free_func(seg_free);
    return p;
}

static void para_free(gpointer data) {
    Para* p = data;
    if (p != NULL) {
        g_ptr_array_free(p->segs, TRUE);
        g_free(p);
    }
}

static gboolean run_equal(const DocRun* a, const DocRun* b) {
    return g_strcmp0(a->font, b->font) == 0 && a->size == b->size && a->bold == b->bold && a->italic == b->italic &&
           a->underline == b->underline && a->strike == b->strike && a->caps == b->caps && a->link == b->link &&
           a->vert == b->vert && a->color == b->color && a->highlight == b->highlight;
}

static void para_add_text(Para* p, const gchar* text, gsize len, const DocRun* run) {
    Seg* last = p->segs->len > 0 ? g_ptr_array_index(p->segs, p->segs->len - 1) : NULL;
    if (last != NULL && last->text != NULL && run_equal(&last->run, run)) {
        gchar* piece = g_strndup(text, len);
        gchar* joined = g_strconcat(last->text, piece, NULL);
        g_free(piece);
        g_free(last->text);
        last->text = joined;
        return;
    }
    Seg* s = g_new0(Seg, 1);
    s->text = g_strndup(text, len);
    s->run = *run;
    s->run.font = g_strdup(run->font);
    g_ptr_array_add(p->segs, s);
}

static gboolean para_is_empty(const Para* p) {
    return p == NULL || p->segs->len == 0;
}

/* ---- Tables ---- */

typedef struct {
    GPtrArray* paras;  /* Para* */
    guint span;
} Cell;

typedef struct {
    GPtrArray* cells;  /* Cell* */
    gboolean header;
} Row;

typedef struct {
    GPtrArray* rows;   /* Row* */
} Table;

static void cell_free(gpointer data) {
    Cell* c = data;
    g_ptr_array_free(c->paras, TRUE);
    g_free(c);
}

static void row_free(gpointer data) {
    Row* r = data;
    g_ptr_array_free(r->cells, TRUE);
    g_free(r);
}

/* ---- Writer ---- */

struct DocWriter {
    PreviewDoc* doc;
    gchar* default_font;
    gdouble default_size;
    Para* cur;           /* paragraph being written */
    gsize length;        /* characters written */
    gint table_depth;
    Table* table;        /* outermost table */
    Row* row;
    Cell* cell;
};

DocWriter* doc_writer_new(const gchar* default_font, gdouble default_size) {
    DocWriter* w = g_new0(DocWriter, 1);
    w->doc = preview_doc_new();
    w->default_font = g_strdup(default_font);
    w->default_size = default_size > 0 ? default_size : 11.0;
    w->cur = para_new();
    return w;
}

static void table_free(Table* t) {
    if (t != NULL) {
        g_ptr_array_free(t->rows, TRUE);
        g_free(t);
    }
}

void doc_writer_free(DocWriter* w) {
    if (w == NULL) {
        return;
    }
    if (w->doc != NULL) {
        preview_doc_free(w->doc);
    }
    para_free(w->cur);
    if (w->cell != NULL) {
        cell_free(w->cell);
    }
    if (w->row != NULL) {
        row_free(w->row);
    }
    table_free(w->table);
    g_free(w->default_font);
    g_free(w);
}

gsize doc_writer_length(const DocWriter* w) {
    return w->length;
}

void doc_writer_text(DocWriter* w, const gchar* text, gssize len, const DocRun* run) {
    if (text == NULL) {
        return;
    }
    gsize n = len < 0 ? strlen(text) : (gsize)len;
    gchar* valid = g_utf8_make_valid(text, (gssize)n);
    GString* clean = g_string_sized_new(strlen(valid));
    for (const gchar* p = valid; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == '\t' || c >= 0x20) {
            g_string_append_unichar(clean, c);
        }
    }
    g_free(valid);
    if (clean->len > 0) {
        DocRun r = *run;
        if (r.caps) {
            gchar* up = g_utf8_strup(clean->str, (gssize)clean->len);
            g_string_assign(clean, up);
            g_free(up);
        }
        para_add_text(w->cur, clean->str, clean->len, &r);
        w->length += g_utf8_strlen(clean->str, (gssize)clean->len);
    }
    g_string_free(clean, TRUE);
}

void doc_writer_image(DocWriter* w, GBytes* data, gdouble width_pt, const gchar* name) {
    Seg* s = g_new0(Seg, 1);
    doc_run_init(&s->run);
    s->image = data != NULL ? g_bytes_ref(data) : NULL;
    s->image_width = width_pt > 0 ? (gint)MIN(width_pt * PX_PER_PT, MAX_IMAGE_PX) : 0;
    s->image_name = g_strdup(name != NULL ? name : "picture");
    g_ptr_array_add(w->cur->segs, s);
    w->length++;
}

/* Spans of a run over [start, end); tables keep the grid font and size */
static void add_run_spans(DocWriter* w, gint start, gint end, const DocRun* r, gboolean in_table) {
    PreviewDoc* doc = w->doc;
    if (end <= start) {
        return;
    }
    if (r->bold) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_BOLD);
    if (r->italic) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_ITALIC);
    if (r->underline || r->link) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_UNDERLINE);
    if (r->strike) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_STRIKE);
    if (r->highlight != DOC_COLOR_NONE) {
        gchar* c = preview_color_format(r->highlight);
        preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_HIGHLIGHT, c);
        g_free(c);
    } else if (r->link) {
        preview_doc_add_span(doc, start, end, PREVIEW_STYLE_LINK);
    } else if (r->color != DOC_COLOR_NONE) {
        gchar* c = preview_color_format(r->color);
        preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_COLOR, c);
        g_free(c);
    }
    if (in_table) {
        return;
    }
    if (r->vert > 0) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_SUPERSCRIPT);
    if (r->vert < 0) preview_doc_add_span(doc, start, end, PREVIEW_STYLE_SUBSCRIPT);
    const gchar* font = r->font != NULL ? r->font : w->default_font;
    if (font != NULL && font[0] != '\0') {
        preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_FAMILY, font);
    }
    gdouble size = r->size > 0 ? r->size : w->default_size;
    gdouble scale = CLAMP(size / w->default_size, 0.5, 4.0);
    if (r->vert == 0 && (scale < 0.99 || scale > 1.01)) {
        gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(buf, sizeof buf, "%.3f", scale);
        preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_SCALE, buf);
    }
}

static gint px(gdouble pt, gint lo, gint hi) {
    return (gint)CLAMP(pt * PX_PER_PT + (pt >= 0 ? 0.5 : -0.5), lo, hi);
}

/* Paragraph with its label first */
static void prepend_label(Para* p, const gchar* label, const DocRun* label_run) {
    if (label == NULL || label[0] == '\0') {
        return;
    }
    DocRun plain;
    doc_run_init(&plain);
    const DocRun* r = label_run != NULL ? label_run : &plain;
    Seg* s = g_new0(Seg, 1);
    s->text = g_strconcat(label, LABEL_GAP, NULL);
    s->run = *r;
    s->run.font = g_strdup(r->font);
    s->run.link = FALSE;
    g_ptr_array_insert(p->segs, 0, s);
}

/* Write the segments of a paragraph (outside tables) */
static void emit_segments(DocWriter* w, const Para* p) {
    for (guint i = 0; i < p->segs->len; i++) {
        const Seg* s = g_ptr_array_index(p->segs, i);
        gint start = preview_doc_offset(w->doc);
        if (s->text != NULL) {
            preview_doc_append_len(w->doc, s->text, -1);
            add_run_spans(w, start, preview_doc_offset(w->doc), &s->run, FALSE);
        } else if (s->image != NULL) {
            preview_doc_add_image(w->doc, s->image, s->image_width);
        } else {
            preview_doc_append(w->doc, PREVIEW_STYLE_DIM, "[");
            preview_doc_append(w->doc, PREVIEW_STYLE_DIM, s->image_name);
            preview_doc_append(w->doc, PREVIEW_STYLE_DIM, "]");
        }
    }
}

void doc_writer_end_paragraph(DocWriter* w, const DocPara* para, const gchar* label, const DocRun* label_run) {
    DocPara plain;
    doc_para_init(&plain);
    const DocPara* pp = para != NULL ? para : &plain;
    Para* p = w->cur;
    w->cur = para_new();
    prepend_label(p, label, label_run);
    p->align = pp->align;
    if (w->table_depth > 0) {
        if (w->cell != NULL && !para_is_empty(p)) {
            g_ptr_array_add(w->cell->paras, p);
        } else {
            para_free(p);
        }
        return;
    }
    gint start = preview_doc_offset(w->doc);
    emit_segments(w, p);
    preview_doc_append_len(w->doc, "\n", 1);
    gint left = px(pp->left, 0, MAX_INDENT_PX);
    PreviewParagraph layout = { pp->align, left, px(pp->right, 0, MAX_INDENT_PX),
                                px(pp->first, -left, MAX_INDENT_PX), px(pp->before, 0, MAX_SPACE_PX),
                                px(pp->after, 0, MAX_SPACE_PX) };
    preview_doc_add_paragraph_span(w->doc, start, preview_doc_offset(w->doc), &layout);
    para_free(p);
}

/* Centred short rule */
static void emit_rule(PreviewDoc* doc) {
    gint start = preview_doc_offset(doc);
    preview_doc_append(doc, PREVIEW_STYLE_GRID, "────────────────────────");
    preview_doc_append_len(doc, "\n", 1);
    PreviewParagraph layout = { PREVIEW_ALIGN_CENTER, 0, 0, 0, 8, 8 };
    preview_doc_add_paragraph_span(doc, start, preview_doc_offset(doc), &layout);
}

void doc_writer_page_break(DocWriter* w) {
    if (w->table_depth > 0) {
        return;
    }
    if (!para_is_empty(w->cur)) {
        doc_writer_end_paragraph(w, NULL, NULL, NULL);
    }
    if (preview_doc_offset(w->doc) > 0) {
        emit_rule(w->doc);
    }
}

/* ---- Table layout ---- */

static gint char_width(gunichar c) {
    if (g_unichar_iszerowidth(c)) {
        return 0;
    }
    return g_unichar_iswide(c) ? 2 : 1;
}

/* A paragraph as characters, each with the index of its segment */
typedef struct {
    GArray* chars;  /* gunichar */
    GArray* segs;   /* guint */
} Flat;

static Flat flatten(const Para* p) {
    Flat f = { g_array_new(FALSE, FALSE, sizeof(gunichar)), g_array_new(FALSE, FALSE, sizeof(guint)) };
    for (guint i = 0; i < p->segs->len; i++) {
        const Seg* s = g_ptr_array_index(p->segs, i);
        gchar* image = NULL;
        const gchar* text = s->text;
        if (text == NULL) {
            image = g_strdup_printf("[%s]", s->image_name);
            text = image;
        }
        for (const gchar* c = text; *c != '\0'; c = g_utf8_next_char(c)) {
            gunichar u = g_utf8_get_char(c);
            if (u == '\t' || u == 0x2002) {
                u = ' ';
            }
            g_array_append_val(f.chars, u);
            g_array_append_val(f.segs, i);
        }
        g_free(image);
    }
    return f;
}

static void flat_clear(Flat* f) {
    g_array_free(f->chars, TRUE);
    g_array_free(f->segs, TRUE);
}

static gint flat_width(const Flat* f, guint a, guint b) {
    gint width = 0;
    for (guint i = a; i < b; i++) {
        width += char_width(g_array_index(f->chars, gunichar, i));
    }
    return width;
}

/* Lines of at most width columns: [start, end) pairs, broken at spaces */
static GArray* wrap(const Flat* f, gint width) {
    GArray* lines = g_array_new(FALSE, FALSE, sizeof(guint) * 2);
    guint n = f->chars->len, start = 0;
    while (start < n) {
        while (start < n && g_array_index(f->chars, gunichar, start) == ' ') {
            start++;
        }
        if (start >= n) {
            break;
        }
        guint i = start, last_space = 0;
        gint used = 0;
        while (i < n) {
            gunichar c = g_array_index(f->chars, gunichar, i);
            gint cw = char_width(c);
            if (used + cw > width && i > start) {
                break;
            }
            if (c == ' ') {
                last_space = i;
            }
            used += cw;
            i++;
        }
        guint end = i;
        if (i < n && last_space > start) {
            end = last_space;
        }
        guint pair[2] = { start, end };
        g_array_append_vals(lines, pair, 1);
        start = end;
    }
    return lines;
}

static guint row_columns(const Row* r) {
    guint n = 0;
    for (guint i = 0; i < r->cells->len; i++) {
        n += ((Cell*)g_ptr_array_index(r->cells, i))->span;
    }
    return n;
}

static gint cell_natural_width(const Cell* c) {
    gint width = 1;
    for (guint i = 0; i < c->paras->len; i++) {
        Flat f = flatten(g_ptr_array_index(c->paras, i));
        width = MAX(width, flat_width(&f, 0, f.chars->len));
        flat_clear(&f);
    }
    return width;
}

/* Column widths: the natural width of the cells, capped, then the widest
 * columns shrunk until the table fits */
static gint* column_widths(const Table* t, guint ncols) {
    gint* widths = g_new(gint, ncols);
    for (guint c = 0; c < ncols; c++) {
        widths[c] = 1;
    }
    for (gint pass = 0; pass < 2; pass++) {
        for (guint r = 0; r < t->rows->len; r++) {
            const Row* row = g_ptr_array_index(t->rows, r);
            guint col = 0;
            for (guint i = 0; i < row->cells->len && col < ncols; i++) {
                const Cell* cell = g_ptr_array_index(row->cells, i);
                guint span = MIN(cell->span, ncols - col);
                if ((pass == 0) == (span == 1)) {
                    gint need = MIN(cell_natural_width(cell), MAX_CELL_WIDTH * (gint)span);
                    gint have = -3;
                    for (guint k = col; k < col + span; k++) {
                        have += widths[k] + 3;
                    }
                    if (need > have) {
                        widths[col + span - 1] += need - have;
                    }
                }
                col += span;
            }
        }
    }
    gint total = 1;
    for (guint c = 0; c < ncols; c++) {
        total += widths[c] + 3;
    }
    while (total > MAX_TABLE_WIDTH) {
        guint widest = 0;
        for (guint c = 1; c < ncols; c++) {
            if (widths[c] > widths[widest]) {
                widest = c;
            }
        }
        if (widths[widest] <= MIN_SHRUNK_WIDTH) {
            break;
        }
        widths[widest]--;
        total--;
    }
    return widths;
}

static void append_repeat(GString* s, const gchar* text, gint n) {
    for (gint i = 0; i < n; i++) {
        g_string_append(s, text);
    }
}

static void emit_border(PreviewDoc* doc, const gint* widths, guint ncols, const gchar* left, const gchar* mid,
                        const gchar* right) {
    GString* line = g_string_new(left);
    for (guint c = 0; c < ncols; c++) {
        append_repeat(line, "─", widths[c] + 2);
        g_string_append(line, c + 1 < ncols ? mid : right);
    }
    g_string_append_c(line, '\n');
    preview_doc_append(doc, PREVIEW_STYLE_GRID, line->str);
    g_string_free(line, TRUE);
}

/* One cell's lines: wrapped paragraphs */
typedef struct {
    const Para* para;
    Flat flat;
    guint start, end;
} CellLine;

static GArray* cell_lines(const Cell* cell, gint width) {
    GArray* out = g_array_new(FALSE, FALSE, sizeof(CellLine));
    for (guint i = 0; i < cell->paras->len; i++) {
        const Para* p = g_ptr_array_index(cell->paras, i);
        Flat f = flatten(p);
        GArray* lines = wrap(&f, width);
        for (guint k = 0; k < lines->len; k++) {
            const guint* pair = &g_array_index(lines, guint, 2 * k);
            CellLine cl = { p, f, pair[0], pair[1] };
            if (k > 0) {
                /* Each line keeps its own copy of the characters */
                cl.flat.chars = g_array_copy(f.chars);
                cl.flat.segs = g_array_copy(f.segs);
            }
            g_array_append_val(out, cl);
        }
        if (lines->len == 0) {
            flat_clear(&f);
        }
        g_array_free(lines, TRUE);
    }
    return out;
}

static void cell_lines_free(GArray* lines) {
    for (guint i = 0; i < lines->len; i++) {
        flat_clear(&g_array_index(lines, CellLine, i).flat);
    }
    g_array_free(lines, TRUE);
}

static void emit_cell_line(DocWriter* w, const CellLine* cl, gint width, gboolean header) {
    PreviewDoc* doc = w->doc;
    gint used = cl != NULL ? flat_width(&cl->flat, cl->start, cl->end) : 0;
    gint pad = MAX(width - used, 0), before = 0;
    if (cl != NULL && cl->para->align == PREVIEW_ALIGN_CENTER) {
        before = pad / 2;
    } else if (cl != NULL && cl->para->align == PREVIEW_ALIGN_RIGHT) {
        before = pad;
    }
    GString* spaces = g_string_new(NULL);
    append_repeat(spaces, " ", before + 1);
    preview_doc_append_len(doc, spaces->str, (gssize)spaces->len);
    if (cl != NULL) {
        guint i = cl->start;
        while (i < cl->end) {
            guint seg = g_array_index(cl->flat.segs, guint, i), j = i;
            GString* text = g_string_new(NULL);
            while (j < cl->end && g_array_index(cl->flat.segs, guint, j) == seg) {
                g_string_append_unichar(text, g_array_index(cl->flat.chars, gunichar, j));
                j++;
            }
            gint start = preview_doc_offset(doc);
            preview_doc_append_len(doc, text->str, (gssize)text->len);
            const Seg* s = g_ptr_array_index(cl->para->segs, seg);
            add_run_spans(w, start, preview_doc_offset(doc), &s->run, TRUE);
            if (header) {
                preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_BOLD);
            }
            g_string_free(text, TRUE);
            i = j;
        }
    }
    g_string_truncate(spaces, 0);
    append_repeat(spaces, " ", pad - before + 1);
    preview_doc_append_len(doc, spaces->str, (gssize)spaces->len);
    g_string_free(spaces, TRUE);
    preview_doc_append(doc, PREVIEW_STYLE_GRID, "│");
}

static void emit_table(DocWriter* w, Table* t) {
    guint ncols = 0;
    for (guint r = 0; r < t->rows->len; r++) {
        ncols = MAX(ncols, row_columns(g_ptr_array_index(t->rows, r)));
    }
    ncols = MIN(ncols, MAX_COLUMNS);
    if (ncols == 0) {
        return;
    }
    PreviewDoc* doc = w->doc;
    gint* widths = column_widths(t, ncols);
    gint table_start = preview_doc_offset(doc);
    emit_border(doc, widths, ncols, "┌", "┬", "┐");
    for (guint r = 0; r < t->rows->len; r++) {
        const Row* row = g_ptr_array_index(t->rows, r);
        /* Cells of the row with their width; missing cells are empty */
        GPtrArray* lines = g_ptr_array_new_with_free_func((GDestroyNotify)cell_lines_free);
        GArray* cell_widths = g_array_new(FALSE, FALSE, sizeof(gint));
        guint col = 0, height = 1;
        for (guint i = 0; i < row->cells->len && col < ncols; i++) {
            const Cell* cell = g_ptr_array_index(row->cells, i);
            guint span = MIN(cell->span, ncols - col);
            gint width = -3;
            for (guint k = col; k < col + span; k++) {
                width += widths[k] + 3;
            }
            GArray* cl = cell_lines(cell, width);
            height = MAX(height, cl->len);
            g_ptr_array_add(lines, cl);
            g_array_append_val(cell_widths, width);
            col += span;
        }
        for (; col < ncols; col++) {
            g_ptr_array_add(lines, g_array_new(FALSE, FALSE, sizeof(CellLine)));
            g_array_append_val(cell_widths, widths[col]);
        }
        for (guint k = 0; k < height; k++) {
            preview_doc_append(doc, PREVIEW_STYLE_GRID, "│");
            for (guint i = 0; i < lines->len; i++) {
                GArray* cl = g_ptr_array_index(lines, i);
                emit_cell_line(w, k < cl->len ? &g_array_index(cl, CellLine, k) : NULL,
                               g_array_index(cell_widths, gint, i), row->header);
            }
            preview_doc_append_len(doc, "\n", 1);
        }
        g_ptr_array_free(lines, TRUE);
        g_array_free(cell_widths, TRUE);
        if (r + 1 < t->rows->len) {
            emit_border(doc, widths, ncols, "├", "┼", "┤");
        }
    }
    emit_border(doc, widths, ncols, "└", "┴", "┘");
    gint end = preview_doc_offset(doc);
    preview_doc_add_span(doc, table_start, end, PREVIEW_STYLE_TABLE);
    PreviewParagraph layout = { PREVIEW_ALIGN_LEFT, 0, 0, 0, 0, 0 };
    preview_doc_add_paragraph_span(doc, table_start, end, &layout);
    g_free(widths);
}

void doc_writer_table_start(DocWriter* w) {
    if (w->table_depth++ > 0) {
        return;
    }
    if (!para_is_empty(w->cur)) {
        doc_writer_end_paragraph(w, NULL, NULL, NULL);
    }
    w->table = g_new0(Table, 1);
    w->table->rows = g_ptr_array_new_with_free_func(row_free);
}

void doc_writer_row_start(DocWriter* w, gboolean header) {
    if (w->table_depth != 1) {
        return;
    }
    doc_writer_row_end(w);
    w->row = g_new0(Row, 1);
    w->row->cells = g_ptr_array_new_with_free_func(cell_free);
    w->row->header = header;
}

void doc_writer_cell_start(DocWriter* w, guint span) {
    if (w->table_depth != 1) {
        return;
    }
    if (w->row == NULL) {
        doc_writer_row_start(w, FALSE);
    }
    doc_writer_cell_end(w);
    w->cell = g_new0(Cell, 1);
    w->cell->paras = g_ptr_array_new_with_free_func(para_free);
    w->cell->span = CLAMP(span, 1, MAX_COLUMNS);
}

void doc_writer_cell_end(DocWriter* w) {
    if (w->table_depth != 1 || w->cell == NULL) {
        return;
    }
    if (!para_is_empty(w->cur)) {
        doc_writer_end_paragraph(w, NULL, NULL, NULL);
    }
    if (w->row != NULL) {
        g_ptr_array_add(w->row->cells, w->cell);
    } else {
        cell_free(w->cell);
    }
    w->cell = NULL;
}

void doc_writer_row_end(DocWriter* w) {
    if (w->table_depth != 1 || w->row == NULL) {
        return;
    }
    doc_writer_cell_end(w);
    if (w->row->cells->len > 0 && w->table->rows->len < MAX_TABLE_ROWS) {
        g_ptr_array_add(w->table->rows, w->row);
    } else {
        row_free(w->row);
    }
    w->row = NULL;
}

void doc_writer_table_end(DocWriter* w) {
    if (w->table_depth == 0) {
        return;
    }
    if (w->table_depth > 1) {
        w->table_depth--;
        return;
    }
    doc_writer_row_end(w);
    w->table_depth = 0;
    para_free(w->cur);
    w->cur = para_new();
    emit_table(w, w->table);
    table_free(w->table);
    w->table = NULL;
}

PreviewDoc* doc_writer_finish(DocWriter* w) {
    while (w->table_depth > 0) {
        doc_writer_table_end(w);
    }
    if (!para_is_empty(w->cur)) {
        doc_writer_end_paragraph(w, NULL, NULL, NULL);
    }
    PreviewDoc* doc = w->doc;
    w->doc = NULL;
    doc_writer_free(w);
    return doc;
}

/* Header or footer: dimmed */
static void append_dimmed(PreviewDoc* doc, const PreviewDoc* part) {
    gint start = preview_doc_offset(doc);
    preview_doc_append_doc(doc, part);
    preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_DIM);
}

static gboolean has_text(const PreviewDoc* doc) {
    if (doc == NULL) {
        return FALSE;
    }
    for (const gchar* p = doc->text->str; *p != '\0'; p++) {
        if (!g_ascii_isspace(*p)) {
            return TRUE;
        }
    }
    return FALSE;
}

PreviewDoc* doc_writer_page(const gchar* title, const gchar* subtitle, const PreviewDoc* header,
                            const PreviewDoc* body, const PreviewDoc* footer, const gchar* note) {
    PreviewDoc* doc = preview_doc_new();
    preview_doc_title(doc, title, subtitle);
    if (has_text(header)) {
        append_dimmed(doc, header);
        emit_rule(doc);
    }
    if (body != NULL) {
        preview_doc_append_doc(doc, body);
    }
    if (has_text(footer)) {
        emit_rule(doc);
        append_dimmed(doc, footer);
    }
    if (note != NULL) {
        emit_rule(doc);
        preview_doc_note(doc, note);
    }
    return doc;
}
