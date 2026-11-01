/* Markdown tables (see markdown_table.h) */
#include "markdown_table.h"
#include "fonts.h"
#include "palette.h"

#include <string.h>

/* Apply the tag named tag_name from start_mark to end_iter */
static void apply_tag_mark(GtkTextBuffer* buffer, const gchar* tag_name,
                           GtkTextMark* start_mark, GtkTextIter* end_iter) {
    GtkTextIter start_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);
    gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start_iter, end_iter);
}

static gchar* clean_table_cell_text(const gchar* text) {
    GString* clean = g_string_new(NULL);
    const gchar* p = text;
    while (*p != '\0') {
        if (*p == '`' || *p == '*' || *p == '_' || *p == '~') {
            p++;
            continue;
        }
        if (*p == '[' && strchr(p, ']')) {
            const gchar* end = strchr(p, ']');
            p++;
            while (p < end) {
                g_string_append_c(clean, *p);
                p++;
            }
            if (*p == ']' && p[1] == '(') {
                const gchar* uend = strchr(p + 2, ')');
                if (uend) {
                    p = uend + 1;
                    continue;
                }
            }
            continue;
        }
        g_string_append_c(clean, *p);
        p++;
    }
    return g_string_free(clean, FALSE);
}

GPtrArray* markdown_table_split_row(const gchar* row_str) {
    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
    const gchar* p = row_str;
    while (*p == ' ') p++;
    if (*p == '|') p++; /* skip leading border pipe */

    GString* cur = g_string_new(NULL);
    while (*p != '\0') {
        if (*p == '\\' && p[1] == '|') {
            g_string_append_c(cur, '|');
            p += 2;
            continue;
        }
        if (*p == '|') {
            const gchar* rest = p + 1;
            while (*rest == ' ') rest++;
            if (*rest == '\0') {
                break;
            }
            g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
            cur = g_string_new(NULL);
            p++;
            continue;
        }
        g_string_append_c(cur, *p);
        p++;
    }
    if (cur->len > 0) {
        g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
    } else {
        g_string_free(cur, TRUE);
    }
    return cells;
}

/* Pango markup of a table cell's inline Markdown (code, bold, italic,
 * strikethrough, links), escaped */
static void cell_markup_append(GString* out, const gchar* text, gsize len) {
    gsize i = 0;
    while (i < len) {
        const gchar* p = text + i;
        const gchar* rest_end = text + len;
        if (*p == '`') {
            const gchar* e = memchr(p + 1, '`', rest_end - p - 1);
            if (e != NULL) {
                gchar* esc = g_markup_escape_text(p + 1, e - p - 1);
                gchar* span = g_markup_printf_escaped("<span font_family=\"%s\" size=\"%d%%\" foreground=\"%s\" "
                                                      "background=\"%s\">",
                                                      fonts_family(FONT_CODE), (gint)(FONT_SCALE_CODE * 100),
                                                      palette_color(PAL_SYN_ESCAPE), palette_color(PAL_BG_CODE_INLINE));
                g_string_append_printf(out, "%s%s</span>", span, esc);
                g_free(span);
                g_free(esc);
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if ((p[0] == '*' || p[0] == '_' || p[0] == '~') && i + 1 < len && p[1] == p[0]) {
            gchar marker[3] = { p[0], p[0], 0 };
            const gchar* e = g_strstr_len(p + 2, rest_end - p - 2, marker);
            if (e != NULL) {
                const gchar* tag = (p[0] == '~') ? "s" : "b";
                g_string_append_printf(out, "<%s>", tag);
                cell_markup_append(out, p + 2, e - p - 2);
                g_string_append_printf(out, "</%s>", tag);
                i = (gsize)(e - text) + 2;
                continue;
            }
        }
        if (p[0] == '*' || p[0] == '_') {
            const gchar* e = memchr(p + 1, p[0], rest_end - p - 1);
            if (e != NULL && e > p + 1) {
                g_string_append(out, "<i>");
                cell_markup_append(out, p + 1, e - p - 1);
                g_string_append(out, "</i>");
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if (p[0] == '[') {
            const gchar* close = memchr(p, ']', rest_end - p);
            if (close != NULL && close + 1 < rest_end && close[1] == '(') {
                const gchar* paren = memchr(close, ')', rest_end - close);
                if (paren != NULL) {
                    g_string_append_printf(out, "<span foreground=\"%s\">", palette_color(PAL_LINK));
                    cell_markup_append(out, p + 1, close - p - 1);
                    g_string_append(out, "</span>");
                    i = (gsize)(paren - text) + 1;
                    continue;
                }
            }
        }
        gint n = g_utf8_skip[*(const guchar*)p];
        gchar* esc = g_markup_escape_text(p, n);
        g_string_append(out, esc);
        g_free(esc);
        i += (gsize)n;
    }
}

static gchar* cell_markup(const gchar* text) {
    GString* out = g_string_new(NULL);
    cell_markup_append(out, text, strlen(text));
    return g_string_free(out, FALSE);
}

/* Column alignment from the separator row: ":--" left, "--:" right, ":-:" center */
static gfloat cell_align(const gchar* sep_cell) {
    gsize n = strlen(sep_cell);
    gboolean left = (n > 0 && sep_cell[0] == ':');
    gboolean right = (n > 0 && sep_cell[n - 1] == ':');
    return (left && right) ? 0.5f : right ? 1.0f : 0.0f;
}

/* Table as a grid widget in the text (VS Code: no vertical borders, a line
 * under the header, faint lines between rows; CSS in theme.c) */
static void insert_table_grid(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* iter,
                              GPtrArray* rows, GPtrArray* aligns, guint cols) {
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "md-table");
    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* cells = g_ptr_array_index(rows, r);
        for (guint c = 0; c < cols; c++) {
            const gchar* text = (c < cells->len) ? g_ptr_array_index(cells, c) : "";
            gchar* markup = cell_markup(text);
            GtkWidget* label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), markup);
            g_free(markup);
            gfloat align = (c < aligns->len) ? cell_align(g_ptr_array_index(aligns, c)) : 0.0f;
            gtk_label_set_xalign(GTK_LABEL(label), align);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
            /* The text view gives anchored widgets their minimum size: ask for
             * the cell's length (up to 30 chars) so short cells never wrap */
            gchar* visible = clean_table_cell_text(text);
            gtk_label_set_width_chars(GTK_LABEL(label), MIN((gint)g_utf8_strlen(visible, -1) + 1, 30)); /* +1: bold header */
            g_free(visible);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_widget_set_focusable(label, FALSE);
            gtk_widget_set_valign(label, GTK_ALIGN_FILL);
            gtk_widget_add_css_class(label, r == 0 ? "md-th" : "md-td");
            if (r > 1) {
                gtk_widget_add_css_class(label, "md-td-sep");
            }
            gtk_grid_attach(GTK_GRID(grid), label, (gint)c, (gint)r, 1, 1);
        }
    }
    GtkTextChildAnchor* anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
    gtk_text_view_add_child_at_anchor(view, grid, anchor);
    gtk_text_buffer_insert(buffer, iter, "\n", 1);
}

/* Without a view (no widgets): a text table with box-drawing borders */
static void insert_text_table(GtkTextBuffer* buffer, GtkTextIter* iter, GPtrArray* rows, guint cols) {
    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* rc = g_ptr_array_index(rows, r);
        for (guint c = 0; c < rc->len; c++) {
            gchar* cleaned = clean_table_cell_text(g_ptr_array_index(rc, c));
            g_free(g_ptr_array_index(rc, c));
            g_ptr_array_index(rc, c) = cleaned;
        }
    }
    gint* col_widths = g_new0(gint, cols);
    for (guint c = 0; c < cols; c++) col_widths[c] = 3;

    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* row_cells = g_ptr_array_index(rows, r);
        for (guint c = 0; c < row_cells->len && c < cols; c++) {
            const gchar* cell_text = g_ptr_array_index(row_cells, c);
            gint clen = (gint)g_utf8_strlen(cell_text, -1);
            if (clen > col_widths[c]) col_widths[c] = clen;
        }
    }

    GString* border_top = g_string_new("┌");
    GString* border_mid = g_string_new("├");
    GString* border_bot = g_string_new("└");
    for (guint c = 0; c < cols; c++) {
        for (gint w = 0; w < col_widths[c] + 2; w++) {
            g_string_append(border_top, "─");
            g_string_append(border_mid, "─");
            g_string_append(border_bot, "─");
        }
        if (c + 1 < cols) {
            g_string_append(border_top, "┬");
            g_string_append(border_mid, "┼");
            g_string_append(border_bot, "┴");
        } else {
            g_string_append(border_top, "┐\n");
            g_string_append(border_mid, "┤\n");
            g_string_append(border_bot, "┘\n");
        }
    }

    GtkTextMark* tb_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    gtk_text_buffer_insert(buffer, iter, border_top->str, -1);
    apply_tag_mark(buffer, "md_table_border", tb_mark, iter);
    gtk_text_buffer_delete_mark(buffer, tb_mark);

    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* row_cells = g_ptr_array_index(rows, r);
        GString* row_str = g_string_new("│");
        for (guint c = 0; c < cols; c++) {
            const gchar* cell_text = (c < row_cells->len) ? g_ptr_array_index(row_cells, c) : "";
            gint clen = (gint)g_utf8_strlen(cell_text, -1);
            gint pad = col_widths[c] - clen;
            g_string_append_printf(row_str, " %s", cell_text);
            for (gint p = 0; p < pad; p++) g_string_append_c(row_str, ' ');
            g_string_append(row_str, " │");
        }
        g_string_append_c(row_str, '\n');

        GtkTextMark* rm = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
        gtk_text_buffer_insert(buffer, iter, row_str->str, -1);
        apply_tag_mark(buffer, (r == 0) ? "md_table_header" : "md_table", rm, iter);
        gtk_text_buffer_delete_mark(buffer, rm);
        g_string_free(row_str, TRUE);

        if (r == 0) {
            GtkTextMark* mb_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
            gtk_text_buffer_insert(buffer, iter, border_mid->str, -1);
            apply_tag_mark(buffer, "md_table_border", mb_mark, iter);
            gtk_text_buffer_delete_mark(buffer, mb_mark);
        }
    }

    GtkTextMark* bb_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    gtk_text_buffer_insert(buffer, iter, border_bot->str, -1);
    apply_tag_mark(buffer, "md_table_border", bb_mark, iter);
    gtk_text_buffer_delete_mark(buffer, bb_mark);

    g_string_free(border_top, TRUE);
    g_string_free(border_mid, TRUE);
    g_string_free(border_bot, TRUE);
    g_free(col_widths);
}

void markdown_table_insert(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* iter,
                           GPtrArray* rows, GPtrArray* aligns, guint cols) {
    if (cols == 0) {
        return;
    }
    if (view != NULL) {
        insert_table_grid(buffer, view, iter, rows, aligns, cols);
    } else {
        insert_text_table(buffer, iter, rows, cols);
    }
}
