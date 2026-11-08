/* Org-mode tables: parse, align, navigate and edit rows and columns. */
#include "org/org_table.h"

#include <string.h>

typedef struct {
    gboolean hline;
    GPtrArray* cells; /* gchar*, trimmed */
} Row;

typedef struct {
    gchar* indent;
    GPtrArray* rows; /* Row* */
    gint ncols;
    gint start;      /* first line of the table */
} Table;

gboolean org_table_is_line(const gchar* line) {
    if (line == NULL) return FALSE;
    while (*line == ' ' || *line == '\t') line++;
    return *line == '|';
}

gboolean org_table_bounds(GPtrArray* lines, gint line, gint* start, gint* end) {
    if (!org_table_is_line(org_line(lines, line))) return FALSE;
    gint s = line, e = line + 1;
    while (s > 0 && org_table_is_line(org_line(lines, s - 1))) s--;
    while (e < (gint)lines->len && org_table_is_line(org_line(lines, e))) e++;
    if (start) *start = s;
    if (end) *end = e;
    return TRUE;
}

static void row_free(gpointer data) {
    Row* r = data;
    if (r->cells) g_ptr_array_unref(r->cells);
    g_free(r);
}

static Row* row_new_empty(gint ncols) {
    Row* r = g_new0(Row, 1);
    r->cells = g_ptr_array_new_with_free_func(g_free);
    for (gint i = 0; i < ncols; i++) g_ptr_array_add(r->cells, g_strdup(""));
    return r;
}

static Row* row_new_hline(void) {
    Row* r = g_new0(Row, 1);
    r->hline = TRUE;
    return r;
}

/* Text after the indentation and the leading '|' */
static const gchar* row_body(const gchar* line) {
    while (*line == ' ' || *line == '\t') line++;
    return *line == '|' ? line + 1 : line;
}

static Row* row_parse(const gchar* line) {
    const gchar* b = row_body(line);
    if (*b == '-') return row_new_hline();
    Row* r = g_new0(Row, 1);
    r->cells = g_ptr_array_new_with_free_func(g_free);
    gchar** parts = g_strsplit(b, "|", -1);
    gint n = (gint)g_strv_length(parts);
    /* "| a | b |": the text after the last '|' is not a cell when blank */
    gchar* last = g_strstrip(g_strdup(parts[n - 1]));
    if (last[0] == '\0' && n > 1) n--;
    g_free(last);
    for (gint i = 0; i < n; i++) {
        g_ptr_array_add(r->cells, g_strstrip(g_strdup(parts[i])));
    }
    g_strfreev(parts);
    return r;
}

static void table_free(Table* t) {
    g_ptr_array_unref(t->rows);
    g_free(t->indent);
    g_free(t);
}

static Table* table_parse(GPtrArray* lines, gint start, gint end) {
    Table* t = g_new0(Table, 1);
    const gchar* first = org_line(lines, start);
    gint ib = 0;
    while (first[ib] == ' ' || first[ib] == '\t') ib++;
    t->indent = g_strndup(first, ib);
    t->rows = g_ptr_array_new_with_free_func(row_free);
    t->start = start;
    for (gint i = start; i < end; i++) {
        Row* r = row_parse(org_line(lines, i));
        if (!r->hline) t->ncols = MAX(t->ncols, (gint)r->cells->len);
        g_ptr_array_add(t->rows, r);
    }
    if (t->ncols == 0) t->ncols = 1;
    for (guint i = 0; i < t->rows->len; i++) {
        Row* r = g_ptr_array_index(t->rows, i);
        while (!r->hline && (gint)r->cells->len < t->ncols) g_ptr_array_add(r->cells, g_strdup(""));
    }
    return t;
}

static const gchar* cell(Table* t, gint r, gint c) {
    Row* row = g_ptr_array_index(t->rows, r);
    if (row->hline || c >= (gint)row->cells->len) return "";
    return g_ptr_array_index(row->cells, c);
}

static gboolean is_number(const gchar* s) {
    if (*s == '-' || *s == '+') s++;
    gboolean digit = FALSE;
    for (; *s != '\0'; s++) {
        if (g_ascii_isdigit(*s)) digit = TRUE;
        else if (*s != '.' && *s != ',' && *s != '%' && *s != 'e' && *s != 'E') return FALSE;
    }
    return digit;
}

/* Column widths and alignment (numbers right-aligned) */
static void table_layout(Table* t, gint* widths, gboolean* right) {
    for (gint c = 0; c < t->ncols; c++) {
        gint w = 1, nonempty = 0, numbers = 0;
        for (guint r = 0; r < t->rows->len; r++) {
            const gchar* s = cell(t, r, c);
            w = MAX(w, org_utf8_width(s));
            if (s[0] != '\0') {
                nonempty++;
                if (is_number(s)) numbers++;
            }
        }
        widths[c] = w;
        right[c] = nonempty > 0 && numbers * 2 > nonempty;
    }
}

/* Column where the text of cell c of row r starts */
static gint cell_col(Table* t, const gint* widths, const gboolean* right, gint r, gint c) {
    gint col = org_utf8_width(t->indent) + 2;
    for (gint k = 0; k < c; k++) col += widths[k] + 3;
    if (right[c]) col += widths[c] - org_utf8_width(cell(t, r, c));
    return col;
}

/* Write the aligned table back over its old lines */
static void table_write(GPtrArray* lines, Table* t, gint old_end, gint* widths, gboolean* right) {
    for (gint i = old_end - 1; i >= t->start; i--) g_ptr_array_remove_index(lines, i);
    for (guint r = 0; r < t->rows->len; r++) {
        Row* row = g_ptr_array_index(t->rows, r);
        GString* out = g_string_new(t->indent);
        g_string_append_c(out, '|');
        for (gint c = 0; c < t->ncols; c++) {
            if (row->hline) {
                for (gint k = 0; k < widths[c] + 2; k++) g_string_append_c(out, '-');
                g_string_append_c(out, c == t->ncols - 1 ? '|' : '+');
                continue;
            }
            const gchar* s = cell(t, r, c);
            gint pad = widths[c] - org_utf8_width(s);
            g_string_append_c(out, ' ');
            if (right[c]) for (gint k = 0; k < pad; k++) g_string_append_c(out, ' ');
            g_string_append(out, s);
            if (!right[c]) for (gint k = 0; k < pad; k++) g_string_append_c(out, ' ');
            g_string_append(out, " |");
        }
        g_ptr_array_insert(lines, t->start + (gint)r, g_string_free(out, FALSE));
    }
}

/* Cursor cell: row index, column index and offset in the cell text */
typedef struct {
    gint row, col, offset;
} CellPos;

static CellPos cursor_cell(GPtrArray* lines, Table* t, const OrgPos* pos) {
    CellPos cp = {pos->line - t->start, 0, 0};
    const gchar* l = org_line(lines, pos->line);
    gint byte = org_col_to_byte(l, pos->col);
    const gchar* b = row_body(l);
    gint body_off = (gint)(b - l);
    if (byte < body_off) return cp;
    gint bars = 0;
    gint seg_start = body_off;
    for (gint i = body_off; i < byte && l[i] != '\0'; i++) {
        if (l[i] == '|') {
            bars++;
            seg_start = i + 1;
        }
    }
    cp.col = MIN(bars, t->ncols - 1);
    if (bars >= t->ncols) return cp;
    /* Offset inside the trimmed cell text */
    gint lead = 0;
    while (l[seg_start + lead] == ' ') lead++;
    gint off_bytes = byte - seg_start - lead;
    if (off_bytes < 0) off_bytes = 0;
    const gchar* txt = cell(t, cp.row, cp.col);
    gint tl = (gint)strlen(txt);
    if (off_bytes > tl) off_bytes = tl;
    cp.offset = org_byte_to_col(txt, off_bytes);
    return cp;
}

typedef struct {
    Table* t;
    gint start, end;
    CellPos cp;
} Ctx;

static gboolean ctx_open(GPtrArray* lines, const OrgPos* pos, Ctx* ctx) {
    if (!org_table_bounds(lines, pos->line, &ctx->start, &ctx->end)) return FALSE;
    ctx->t = table_parse(lines, ctx->start, ctx->end);
    ctx->cp = cursor_cell(lines, ctx->t, pos);
    return TRUE;
}

/* Write the table and put the cursor in cell (row, col) at offset */
static void ctx_close(GPtrArray* lines, OrgPos* pos, Ctx* ctx, gint row, gint col, gint offset) {
    Table* t = ctx->t;
    if (t->rows->len == 0) {
        for (gint i = ctx->end - 1; i >= ctx->start; i--) g_ptr_array_remove_index(lines, i);
        if (lines->len == 0) g_ptr_array_add(lines, g_strdup(""));
        pos->line = MIN(ctx->start, (gint)lines->len - 1);
        pos->col = 0;
        table_free(t);
        return;
    }
    gint* widths = g_new0(gint, t->ncols);
    gboolean* right = g_new0(gboolean, t->ncols);
    table_layout(t, widths, right);
    table_write(lines, t, ctx->end, widths, right);
    row = CLAMP(row, 0, (gint)t->rows->len - 1);
    col = CLAMP(col, 0, t->ncols - 1);
    pos->line = t->start + row;
    Row* r = g_ptr_array_index(t->rows, row);
    if (r->hline) {
        pos->col = org_utf8_width(t->indent) + 1;
    } else {
        offset = CLAMP(offset, 0, org_utf8_width(cell(t, row, col)));
        pos->col = cell_col(t, widths, right, row, col) + offset;
    }
    g_free(widths);
    g_free(right);
    table_free(t);
}

gboolean org_table_align(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, ctx.cp.offset);
    return TRUE;
}

static Row* row_at(Table* t, gint r) {
    return g_ptr_array_index(t->rows, r);
}

gboolean org_table_next_field(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    gint r = ctx.cp.row, c = ctx.cp.col + 1;
    if (row_at(t, r)->hline) c = t->ncols;
    if (c >= t->ncols) {
        c = 0;
        r++;
        while (r < (gint)t->rows->len && row_at(t, r)->hline) r++;
        if (r >= (gint)t->rows->len) {
            g_ptr_array_add(t->rows, row_new_empty(t->ncols));
            r = (gint)t->rows->len - 1;
        }
    }
    ctx_close(lines, pos, &ctx, r, c, 0);
    return TRUE;
}

gboolean org_table_prev_field(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    gint r = ctx.cp.row, c = ctx.cp.col - 1;
    if (row_at(t, r)->hline) c = -1;
    if (c < 0) {
        gint pr = r - 1;
        while (pr >= 0 && row_at(t, pr)->hline) pr--;
        if (pr < 0) {
            c = 0; /* first cell: stay */
            if (row_at(t, r)->hline) r = 0;
        } else {
            r = pr;
            c = t->ncols - 1;
        }
    }
    ctx_close(lines, pos, &ctx, r, c, 0);
    return TRUE;
}

gboolean org_table_next_row(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    gint r = ctx.cp.row + 1;
    if (r >= (gint)t->rows->len || row_at(t, r)->hline) {
        g_ptr_array_insert(t->rows, r, row_new_empty(t->ncols));
    }
    ctx_close(lines, pos, &ctx, r, ctx.cp.col, 0);
    return TRUE;
}

gboolean org_table_move_row(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    gint r = ctx.cp.row, o = r + dir;
    if (o < 0 || o >= (gint)t->rows->len) {
        if (msg) *msg = g_strdup("Cannot move row further");
        table_free(t);
        return FALSE;
    }
    gpointer tmp = t->rows->pdata[r];
    t->rows->pdata[r] = t->rows->pdata[o];
    t->rows->pdata[o] = tmp;
    ctx_close(lines, pos, &ctx, o, ctx.cp.col, ctx.cp.offset);
    return TRUE;
}

gboolean org_table_move_column(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    gint c = ctx.cp.col, o = c + dir;
    if (o < 0 || o >= t->ncols) {
        if (msg) *msg = g_strdup("Cannot move column further");
        table_free(t);
        return FALSE;
    }
    for (guint r = 0; r < t->rows->len; r++) {
        Row* row = row_at(t, r);
        if (row->hline) continue;
        gpointer tmp = row->cells->pdata[c];
        row->cells->pdata[c] = row->cells->pdata[o];
        row->cells->pdata[o] = tmp;
    }
    ctx_close(lines, pos, &ctx, ctx.cp.row, o, ctx.cp.offset);
    return TRUE;
}

gboolean org_table_insert_row(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    g_ptr_array_insert(ctx.t->rows, ctx.cp.row, row_new_empty(ctx.t->ncols));
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, 0);
    return TRUE;
}

gboolean org_table_delete_row(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    g_ptr_array_remove_index(ctx.t->rows, ctx.cp.row);
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, 0);
    return TRUE;
}

gboolean org_table_insert_column(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    for (guint r = 0; r < t->rows->len; r++) {
        Row* row = row_at(t, r);
        if (!row->hline) g_ptr_array_insert(row->cells, ctx.cp.col, g_strdup(""));
    }
    t->ncols++;
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, 0);
    return TRUE;
}

gboolean org_table_delete_column(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    Table* t = ctx.t;
    if (t->ncols <= 1) {
        /* The last column: the table goes away */
        g_ptr_array_set_size(t->rows, 0);
    } else {
        for (guint r = 0; r < t->rows->len; r++) {
            Row* row = row_at(t, r);
            if (!row->hline && ctx.cp.col < (gint)row->cells->len) {
                g_ptr_array_remove_index(row->cells, ctx.cp.col);
            }
        }
        t->ncols--;
    }
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, 0);
    return TRUE;
}

gboolean org_table_insert_hline(GPtrArray* lines, OrgPos* pos) {
    Ctx ctx;
    if (!ctx_open(lines, pos, &ctx)) return FALSE;
    g_ptr_array_insert(ctx.t->rows, ctx.cp.row + 1, row_new_hline());
    ctx_close(lines, pos, &ctx, ctx.cp.row, ctx.cp.col, ctx.cp.offset);
    return TRUE;
}
