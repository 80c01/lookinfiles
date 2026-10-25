#include "html.h"
#include <ctype.h>
#include <string.h>

void ensure_html_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    #define CREATE_TAG(name, ...) \
        if (!gtk_text_tag_table_lookup(tt, name)) { \
            gtk_text_buffer_create_tag(buffer, name, __VA_ARGS__, NULL); \
        }

    CREATE_TAG("html_title",
               "scale", 1.5,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#61afef",
               "pixels-above-lines", 8,
               "pixels-below-lines", 6);

    CREATE_TAG("html_h1",
               "scale", 1.8,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4fc1ff",
               "pixels-above-lines", 14,
               "pixels-below-lines", 6);

    CREATE_TAG("html_h2",
               "scale", 1.45,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("html_h3",
               "scale", 1.25,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#dcdcaa",
               "pixels-above-lines", 10,
               "pixels-below-lines", 3);

    CREATE_TAG("html_h4",
               "scale", 1.1,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#ce9178",
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("html_h5",
               "scale", 1.0,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#c586c0",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("html_h6",
               "scale", 0.9,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#808080",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("html_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("html_underline",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("html_strike",
               "strikethrough", TRUE,
               "foreground", "#808080");

    CREATE_TAG("html_code",
               "family", "monospace",
               "background", "#2d2d2d",
               "foreground", "#ce9178");

    CREATE_TAG("html_pre",
               "family", "monospace",
               "background", "#252526",
               "foreground", "#d4d4d4",
               "left-margin", 24,
               "right-margin", 24,
               "wrap-mode", GTK_WRAP_NONE,
               "pixels-above-lines", 3,
               "pixels-below-lines", 3);

    CREATE_TAG("html_quote",
               "style", PANGO_STYLE_ITALIC,
               "foreground", "#b0b0b0",
               "left-margin", 24,
               "pixels-above-lines", 2,
               "pixels-below-lines", 2);

    CREATE_TAG("html_quote_bar",
               "foreground", "#569cd6",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_link",
               "foreground", "#3794ff",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("html_mark",
               "background", "#e5bf00",
               "foreground", "#1e1e1e");

    CREATE_TAG("html_small",
               "scale", 0.85);

    CREATE_TAG("html_sub",
               "scale", 0.8);

    CREATE_TAG("html_sup",
               "scale", 0.8);

    CREATE_TAG("html_bullet",
               "foreground", "#569cd6",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_num",
               "foreground", "#4ec9b0",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_hr",
               "foreground", "#3e3e42",
               "pixels-above-lines", 8,
               "pixels-below-lines", 8);

    CREATE_TAG("html_table",
               "family", "monospace",
               "foreground", "#cccccc",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_table_header",
               "family", "monospace",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "#4ec9b0",
               "background", "#252526",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_table_border",
               "family", "monospace",
               "foreground", "#555555",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_tag",
               "scale", 0.85,
               "foreground", "#7f848e");

    CREATE_TAG("html_preview_header",
               "family", "monospace",
               "scale", 0.85,
               "foreground", "#808080",
               "background", "#121314",
               "left-margin", 16,
               "pixels-above-lines", 6,
               "pixels-below-lines", 4);

    #undef CREATE_TAG
}

/* Renderer context */
typedef struct {
    GtkTextBuffer* buffer;
    GtkTextIter iter;
    const gchar* doc_dir;

    /* Parser state */
    gboolean in_pre;
    gint quote_depth;
    gboolean line_needs_quote_bar;

    /* Lists: depth and ordered flags */
    struct {
        gboolean is_ordered;
        gint count;
    } list_stack[16];
    gint list_depth;

    /* Active styling tags */
    GPtrArray* active_tags;

    /* Link tracking */
    gchar* current_link_url;

    /* Spacing state */
    gboolean last_was_ws;
    gint trailing_newlines;
} HtmlRenderContext;

static void apply_tag_mark(GtkTextBuffer* buffer, const gchar* tag_name,
                           GtkTextMark* start_mark, GtkTextIter* end_iter) {
    GtkTextIter start_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);
    gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start_iter, end_iter);
}

static void ensure_newlines(HtmlRenderContext* ctx, gint count) {
    while (ctx->trailing_newlines < count) {
        gtk_text_buffer_insert(ctx->buffer, &ctx->iter, "\n", 1);
        ctx->trailing_newlines++;
        ctx->last_was_ws = TRUE;
        ctx->line_needs_quote_bar = TRUE;
    }
}

static void push_tag(HtmlRenderContext* ctx, const gchar* tag_name) {
    g_ptr_array_add(ctx->active_tags, (gpointer)tag_name);
}

static void pop_tag(HtmlRenderContext* ctx, const gchar* tag_name) {
    for (gint i = (gint)ctx->active_tags->len - 1; i >= 0; i--) {
        if (g_strcmp0(g_ptr_array_index(ctx->active_tags, i), tag_name) == 0) {
            g_ptr_array_remove_index(ctx->active_tags, (guint)i);
            break;
        }
    }
}

static void insert_tag_text(HtmlRenderContext* ctx, const gchar* text, const gchar* tag_name) {
    if (text == NULL || *text == '\0') return;
    GtkTextMark* m = gtk_text_buffer_create_mark(ctx->buffer, NULL, &ctx->iter, TRUE);
    gtk_text_buffer_insert(ctx->buffer, &ctx->iter, text, -1);
    apply_tag_mark(ctx->buffer, tag_name, m, &ctx->iter);
    gtk_text_buffer_delete_mark(ctx->buffer, m);

    gint tlen = (gint)strlen(text);
    const gchar* end = text + tlen;
    while (end > text && *(end - 1) == '\n') {
        ctx->trailing_newlines++;
        ctx->line_needs_quote_bar = TRUE;
        end--;
    }
    if (end > text) {
        ctx->trailing_newlines = 0;
    }
}

static void insert_styled_text(HtmlRenderContext* ctx, const gchar* text, gssize len) {
    if (text == NULL || len == 0 || *text == '\0') return;
    if (len < 0) len = strlen(text);

    if (ctx->line_needs_quote_bar && ctx->quote_depth > 0) {
        GtkTextMark* qm = gtk_text_buffer_create_mark(ctx->buffer, NULL, &ctx->iter, TRUE);
        gtk_text_buffer_insert(ctx->buffer, &ctx->iter, "  │ ", -1);
        apply_tag_mark(ctx->buffer, "html_quote_bar", qm, &ctx->iter);
        gtk_text_buffer_delete_mark(ctx->buffer, qm);
        ctx->line_needs_quote_bar = FALSE;
    }

    GtkTextMark* sm = gtk_text_buffer_create_mark(ctx->buffer, NULL, &ctx->iter, TRUE);
    gtk_text_buffer_insert(ctx->buffer, &ctx->iter, text, len);
    for (guint i = 0; i < ctx->active_tags->len; i++) {
        const gchar* tag_name = g_ptr_array_index(ctx->active_tags, i);
        apply_tag_mark(ctx->buffer, tag_name, sm, &ctx->iter);
    }
    gtk_text_buffer_delete_mark(ctx->buffer, sm);

    /* Update trailing newlines */
    const gchar* end = text + len;
    while (end > text && *(end - 1) == '\n') {
        ctx->trailing_newlines++;
        ctx->line_needs_quote_bar = TRUE;
        end--;
    }
    if (end > text) {
        ctx->trailing_newlines = 0;
    }
}

static const gchar* decode_named_entity(const gchar* name, gint len) {
    #define MATCH(s, val) if (len == (gint)strlen(s) && g_ascii_strncasecmp(name, s, len) == 0) return val;
    MATCH("amp", "&");
    MATCH("lt", "<");
    MATCH("gt", ">");
    MATCH("quot", "\"");
    MATCH("apos", "'");
    MATCH("nbsp", " ");
    MATCH("ensp", " ");
    MATCH("emsp", "  ");
    MATCH("thinsp", " ");
    MATCH("mdash", "—");
    MATCH("ndash", "–");
    MATCH("hellip", "…");
    MATCH("bull", "•");
    MATCH("middot", "·");
    MATCH("copy", "©");
    MATCH("reg", "®");
    MATCH("trade", "™");
    MATCH("euro", "€");
    MATCH("pound", "£");
    MATCH("yen", "¥");
    MATCH("cent", "¢");
    MATCH("deg", "°");
    MATCH("plusmn", "±");
    MATCH("times", "×");
    MATCH("divide", "÷");
    MATCH("lsquo", "‘");
    MATCH("rsquo", "’");
    MATCH("ldquo", "“");
    MATCH("rdquo", "”");
    MATCH("laquo", "«");
    MATCH("raquo", "»");
    MATCH("check", "✓");
    MATCH("cross", "✗");
    MATCH("star", "★");
    MATCH("heart", "♥");
    #undef MATCH
    return NULL;
}

static void append_entity(GString* out, const gchar** ptr) {
    const gchar* p = *ptr; /* points to '&' */
    const gchar* semi = strchr(p, ';');
    if (semi == NULL || semi - p > 12) {
        g_string_append_c(out, '&');
        (*ptr)++;
        return;
    }

    gint elen = (gint)(semi - p - 1);
    const gchar* ename = p + 1;

    if (ename[0] == '#') {
        gunichar c = 0;
        if (ename[1] == 'x' || ename[1] == 'X') {
            c = (gunichar)g_ascii_strtoull(ename + 2, NULL, 16);
        } else {
            c = (gunichar)g_ascii_strtoull(ename + 1, NULL, 10);
        }
        if (c > 0 && g_unichar_validate(c)) {
            gchar buf[7] = {0};
            gint ulen = g_unichar_to_utf8(c, buf);
            buf[ulen] = '\0';
            g_string_append(out, buf);
            *ptr = semi + 1;
            return;
        }
    } else {
        const gchar* decoded = decode_named_entity(ename, elen);
        if (decoded != NULL) {
            g_string_append(out, decoded);
            *ptr = semi + 1;
            return;
        }
    }

    /* Fallback */
    g_string_append_len(out, p, semi - p + 1);
    *ptr = semi + 1;
}

static gchar* html_get_attr(const gchar* tag_str, const gchar* attr_name) {
    if (tag_str == NULL || attr_name == NULL) return NULL;
    gsize alen = strlen(attr_name);
    const gchar* p = tag_str;
    while (*p != '\0' && *p != '>') {
        if ((p == tag_str || isspace((unsigned char)*(p - 1))) &&
            g_ascii_strncasecmp(p, attr_name, alen) == 0) {
            const gchar* eq = p + alen;
            while (*eq == ' ' || *eq == '\t') eq++;
            if (*eq == '=') {
                eq++;
                while (*eq == ' ' || *eq == '\t') eq++;
                if (*eq == '"' || *eq == '\'') {
                    gchar q = *eq;
                    eq++;
                    const gchar* qend = strchr(eq, q);
                    if (qend != NULL) {
                        return g_strndup(eq, qend - eq);
                    }
                } else {
                    const gchar* vend = eq;
                    while (*vend != '\0' && !isspace((unsigned char)*vend) && *vend != '>' && *vend != '/') vend++;
                    return g_strndup(eq, vend - eq);
                }
            }
        }
        p++;
    }
    return NULL;
}

static const gchar* strstr_case(const gchar* haystack, const gchar* needle, const gchar* limit) {
    if (haystack == NULL || needle == NULL) return NULL;
    gsize nlen = strlen(needle);
    const gchar* h = haystack;
    while (*h != '\0' && (limit == NULL || h + nlen <= limit)) {
        if (g_ascii_strncasecmp(h, needle, nlen) == 0) return h;
        h++;
    }
    return NULL;
}

static gchar* strip_tags_and_entities(const gchar* src) {
    if (src == NULL) return g_strdup("");
    GString* res = g_string_new(NULL);
    const gchar* p = src;
    gboolean last_space = TRUE;
    while (*p != '\0') {
        if (*p == '<') {
            const gchar* close = strchr(p, '>');
            if (close != NULL) {
                p = close + 1;
            } else {
                p++;
            }
            continue;
        }
        if (*p == '&') {
            append_entity(res, &p);
            last_space = FALSE;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            if (!last_space) {
                g_string_append_c(res, ' ');
                last_space = TRUE;
            }
            p++;
            continue;
        }
        g_string_append_c(res, *p);
        last_space = FALSE;
        p++;
    }
    if (res->len > 0 && res->str[res->len - 1] == ' ') {
        g_string_truncate(res, res->len - 1);
    }
    return g_string_free(res, FALSE);
}

static const gchar* render_html_table(HtmlRenderContext* ctx, const gchar* p) {
    const gchar* table_end = strstr_case(p, "</table", NULL);
    const gchar* search_limit = (table_end != NULL) ? table_end + 8 : p + strlen(p);

    GPtrArray* rows = g_ptr_array_new();
    GArray* is_header_row = g_array_new(FALSE, FALSE, sizeof(gboolean));

    const gchar* cur = p;
    while (cur < search_limit) {
        const gchar* tr_start = strstr_case(cur, "<tr", search_limit);
        if (tr_start == NULL || tr_start >= search_limit) break;

        const gchar* tr_close_tag = strchr(tr_start, '>');
        if (tr_close_tag == NULL || tr_close_tag >= search_limit) break;

        const gchar* tr_end = strstr_case(tr_close_tag, "</tr", search_limit);
        if (tr_end == NULL || tr_end > search_limit) {
            const gchar* next_tr = strstr_case(tr_close_tag + 1, "<tr", search_limit);
            tr_end = (next_tr != NULL) ? next_tr : search_limit;
        }

        GPtrArray* row_cells = g_ptr_array_new_with_free_func(g_free);
        gboolean row_has_th = FALSE;

        const gchar* cell_cur = tr_close_tag + 1;
        while (cell_cur < tr_end) {
            const gchar* th_start = strstr_case(cell_cur, "<th", tr_end);
            const gchar* td_start = strstr_case(cell_cur, "<td", tr_end);

            const gchar* cell_start = NULL;
            gboolean is_th = FALSE;
            if (th_start != NULL && td_start != NULL) {
                if (th_start < td_start) { cell_start = th_start; is_th = TRUE; }
                else { cell_start = td_start; is_th = FALSE; }
            } else if (th_start != NULL) {
                cell_start = th_start; is_th = TRUE;
            } else if (td_start != NULL) {
                cell_start = td_start; is_th = FALSE;
            } else {
                break;
            }

            if (is_th) row_has_th = TRUE;

            const gchar* cell_close_tag = strchr(cell_start, '>');
            if (cell_close_tag == NULL || cell_close_tag >= tr_end) break;

            const gchar* cell_end = NULL;
            if (is_th) cell_end = strstr_case(cell_close_tag, "</th", tr_end);
            else cell_end = strstr_case(cell_close_tag, "</td", tr_end);

            if (cell_end == NULL || cell_end > tr_end) {
                const gchar* next_cell = strstr_case(cell_close_tag + 1, "<t", tr_end);
                cell_end = (next_cell != NULL) ? next_cell : tr_end;
            }

            gchar* cell_raw = g_strndup(cell_close_tag + 1, cell_end - (cell_close_tag + 1));
            gchar* clean = strip_tags_and_entities(cell_raw);
            g_free(cell_raw);
            g_ptr_array_add(row_cells, clean);

            cell_cur = cell_end;
            if (*cell_cur == '<') {
                const gchar* gt = strchr(cell_cur, '>');
                if (gt != NULL && gt < tr_end) cell_cur = gt + 1;
                else cell_cur++;
            }
        }

        if (row_cells->len > 0) {
            g_ptr_array_add(rows, row_cells);
            g_array_append_val(is_header_row, row_has_th);
        } else {
            g_ptr_array_unref(row_cells);
        }

        cur = tr_end;
        if (*cur == '<') {
            const gchar* gt = strchr(cur, '>');
            if (gt != NULL && gt < search_limit) cur = gt + 1;
            else cur++;
        }
    }

    if (rows->len > 0) {
        guint num_rows = rows->len;
        guint max_cols = 0;
        for (guint r = 0; r < num_rows; r++) {
            GPtrArray* rc = g_ptr_array_index(rows, r);
            if (rc->len > max_cols) max_cols = rc->len;
        }

        if (max_cols > 0) {
            gint* col_widths = g_new0(gint, max_cols);
            for (guint c = 0; c < max_cols; c++) col_widths[c] = 3;

            for (guint r = 0; r < num_rows; r++) {
                GPtrArray* rc = g_ptr_array_index(rows, r);
                for (guint c = 0; c < rc->len && c < max_cols; c++) {
                    const gchar* text = g_ptr_array_index(rc, c);
                    gint clen = (gint)g_utf8_strlen(text, -1);
                    if (clen > 40) clen = 40;
                    if (clen > col_widths[c]) col_widths[c] = clen;
                }
            }

            ensure_newlines(ctx, 2);

            GString* border_top = g_string_new("  ┌");
            GString* border_mid = g_string_new("  ├");
            GString* border_bot = g_string_new("  └");
            for (guint c = 0; c < max_cols; c++) {
                for (gint w = 0; w < col_widths[c] + 2; w++) {
                    g_string_append(border_top, "─");
                    g_string_append(border_mid, "─");
                    g_string_append(border_bot, "─");
                }
                if (c + 1 < max_cols) {
                    g_string_append(border_top, "┬");
                    g_string_append(border_mid, "┼");
                    g_string_append(border_bot, "┴");
                } else {
                    g_string_append(border_top, "┐\n");
                    g_string_append(border_mid, "┤\n");
                    g_string_append(border_bot, "┘\n");
                }
            }

            insert_tag_text(ctx, border_top->str, "html_table_border");

            for (guint r = 0; r < num_rows; r++) {
                GPtrArray* rc = g_ptr_array_index(rows, r);
                gboolean is_hdr = g_array_index(is_header_row, gboolean, r) || (r == 0 && num_rows > 1);
                GString* row_str = g_string_new("  │");
                for (guint c = 0; c < max_cols; c++) {
                    const gchar* cell_text = (c < rc->len) ? g_ptr_array_index(rc, c) : "";
                    gchar* display_text = NULL;
                    gint clen = (gint)g_utf8_strlen(cell_text, -1);
                    if (clen > col_widths[c]) {
                        display_text = g_strndup(cell_text, col_widths[c] - 1);
                        gchar* tmp = g_strconcat(display_text, "…", NULL);
                        g_free(display_text);
                        display_text = tmp;
                        clen = col_widths[c];
                    } else {
                        display_text = g_strdup(cell_text);
                    }
                    gint pad = col_widths[c] - clen;
                    g_string_append_printf(row_str, " %s", display_text);
                    for (gint p_pad = 0; p_pad < pad; p_pad++) g_string_append_c(row_str, ' ');
                    g_string_append(row_str, " │");
                    g_free(display_text);
                }
                g_string_append_c(row_str, '\n');

                insert_tag_text(ctx, row_str->str, is_hdr ? "html_table_header" : "html_table");
                g_string_free(row_str, TRUE);

                if (is_hdr && r + 1 < num_rows) {
                    insert_tag_text(ctx, border_mid->str, "html_table_border");
                }
            }

            insert_tag_text(ctx, border_bot->str, "html_table_border");
            ensure_newlines(ctx, 2);

            g_string_free(border_top, TRUE);
            g_string_free(border_mid, TRUE);
            g_string_free(border_bot, TRUE);
            g_free(col_widths);
        }
    }

    for (guint r = 0; r < rows->len; r++) {
        g_ptr_array_unref(g_ptr_array_index(rows, r));
    }
    g_ptr_array_unref(rows);
    g_array_unref(is_header_row);

    return search_limit;
}

static void handle_html_tag(HtmlRenderContext* ctx, const gchar* tag, const gchar* full_tag, gboolean is_closing) {
    if (is_closing) {
        if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0) {
            pop_tag(ctx, "html_bold");
        } else if (strcmp(tag, "i") == 0 || strcmp(tag, "em") == 0 ||
                   strcmp(tag, "cite") == 0 || strcmp(tag, "var") == 0 ||
                   strcmp(tag, "dfn") == 0) {
            pop_tag(ctx, "html_italic");
        } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
            pop_tag(ctx, "html_underline");
        } else if (strcmp(tag, "s") == 0 || strcmp(tag, "del") == 0 || strcmp(tag, "strike") == 0) {
            pop_tag(ctx, "html_strike");
        } else if (strcmp(tag, "code") == 0 || strcmp(tag, "kbd") == 0 ||
                   strcmp(tag, "samp") == 0 || strcmp(tag, "tt") == 0) {
            pop_tag(ctx, "html_code");
        } else if (strcmp(tag, "mark") == 0) {
            pop_tag(ctx, "html_mark");
        } else if (strcmp(tag, "small") == 0) {
            pop_tag(ctx, "html_small");
        } else if (strcmp(tag, "sub") == 0) {
            pop_tag(ctx, "html_sub");
        } else if (strcmp(tag, "sup") == 0) {
            pop_tag(ctx, "html_sup");
        } else if (strcmp(tag, "a") == 0) {
            pop_tag(ctx, "html_link");
            if (ctx->current_link_url != NULL && ctx->current_link_url[0] != '\0') {
                gchar* link_info = g_strdup_printf(" ↗ [%s]", ctx->current_link_url);
                insert_tag_text(ctx, link_info, "html_tag");
                g_free(link_info);
                g_free(ctx->current_link_url);
                ctx->current_link_url = NULL;
            }
        } else if (strcmp(tag, "h1") == 0) {
            pop_tag(ctx, "html_h1");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "h2") == 0) {
            pop_tag(ctx, "html_h2");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "h3") == 0) {
            pop_tag(ctx, "html_h3");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "h4") == 0) {
            pop_tag(ctx, "html_h4");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "h5") == 0) {
            pop_tag(ctx, "html_h5");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "h6") == 0) {
            pop_tag(ctx, "html_h6");
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "p") == 0) {
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "blockquote") == 0) {
            pop_tag(ctx, "html_quote");
            if (ctx->quote_depth > 0) ctx->quote_depth--;
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "pre") == 0) {
            pop_tag(ctx, "html_pre");
            ctx->in_pre = FALSE;
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
            if (ctx->list_depth > 0) ctx->list_depth--;
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "li") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "div") == 0 || strcmp(tag, "section") == 0 ||
                   strcmp(tag, "article") == 0 || strcmp(tag, "header") == 0 ||
                   strcmp(tag, "footer") == 0 || strcmp(tag, "nav") == 0 ||
                   strcmp(tag, "aside") == 0 || strcmp(tag, "main") == 0) {
            ensure_newlines(ctx, 1);
        }
    } else {
        if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0) {
            push_tag(ctx, "html_bold");
        } else if (strcmp(tag, "i") == 0 || strcmp(tag, "em") == 0 ||
                   strcmp(tag, "cite") == 0 || strcmp(tag, "var") == 0 ||
                   strcmp(tag, "dfn") == 0) {
            push_tag(ctx, "html_italic");
        } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
            push_tag(ctx, "html_underline");
        } else if (strcmp(tag, "s") == 0 || strcmp(tag, "del") == 0 || strcmp(tag, "strike") == 0) {
            push_tag(ctx, "html_strike");
        } else if (strcmp(tag, "code") == 0 || strcmp(tag, "kbd") == 0 ||
                   strcmp(tag, "samp") == 0 || strcmp(tag, "tt") == 0) {
            push_tag(ctx, "html_code");
        } else if (strcmp(tag, "mark") == 0) {
            push_tag(ctx, "html_mark");
        } else if (strcmp(tag, "small") == 0) {
            push_tag(ctx, "html_small");
        } else if (strcmp(tag, "sub") == 0) {
            push_tag(ctx, "html_sub");
        } else if (strcmp(tag, "sup") == 0) {
            push_tag(ctx, "html_sup");
        } else if (strcmp(tag, "a") == 0) {
            g_free(ctx->current_link_url);
            ctx->current_link_url = html_get_attr(full_tag, "href");
            push_tag(ctx, "html_link");
        } else if (strcmp(tag, "h1") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h1");
        } else if (strcmp(tag, "h2") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h2");
        } else if (strcmp(tag, "h3") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h3");
        } else if (strcmp(tag, "h4") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h4");
        } else if (strcmp(tag, "h5") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h5");
        } else if (strcmp(tag, "h6") == 0) {
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h6");
        } else if (strcmp(tag, "p") == 0) {
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "br") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "hr") == 0) {
            ensure_newlines(ctx, 1);
            insert_tag_text(ctx, "────────────────────────────────────────────────────────────────────────\n", "html_hr");
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "blockquote") == 0) {
            ensure_newlines(ctx, 2);
            ctx->quote_depth++;
            ctx->line_needs_quote_bar = TRUE;
            push_tag(ctx, "html_quote");
        } else if (strcmp(tag, "pre") == 0) {
            ensure_newlines(ctx, 2);
            ctx->in_pre = TRUE;
            push_tag(ctx, "html_pre");
        } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
            ensure_newlines(ctx, 1);
            if (ctx->list_depth < 16) {
                ctx->list_stack[ctx->list_depth].is_ordered = (strcmp(tag, "ol") == 0);
                ctx->list_stack[ctx->list_depth].count = 0;
                ctx->list_depth++;
            }
        } else if (strcmp(tag, "li") == 0) {
            ensure_newlines(ctx, 1);
            gint depth = (ctx->list_depth > 0) ? ctx->list_depth - 1 : 0;
            gint indent_spaces = depth * 3 + 2;
            for (gint s = 0; s < indent_spaces; s++) {
                gtk_text_buffer_insert(ctx->buffer, &ctx->iter, " ", 1);
            }
            if (ctx->list_depth > 0 && ctx->list_stack[depth].is_ordered) {
                ctx->list_stack[depth].count++;
                gchar* num_str = g_strdup_printf("%d. ", ctx->list_stack[depth].count);
                insert_tag_text(ctx, num_str, "html_num");
                g_free(num_str);
            } else {
                const gchar* bullet = (depth % 3 == 0) ? "• " : (depth % 3 == 1) ? "◦ " : "▪ ";
                insert_tag_text(ctx, bullet, "html_bullet");
            }
            ctx->trailing_newlines = 0;
            ctx->last_was_ws = TRUE;
        } else if (strcmp(tag, "img") == 0) {
            gchar* src = html_get_attr(full_tag, "src");
            gchar* alt = html_get_attr(full_tag, "alt");
            if (src != NULL && src[0] != '\0') {
                gchar* img_path = NULL;
                if (g_path_is_absolute(src)) {
                    img_path = g_strdup(src);
                } else if (ctx->doc_dir != NULL) {
                    img_path = g_build_filename(ctx->doc_dir, src, NULL);
                }

                gboolean loaded = FALSE;
                if (img_path != NULL && g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                    GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(img_path, 480, -1, TRUE, NULL);
                    if (pix != NULL) {
                        ensure_newlines(ctx, 1);
                        gtk_text_buffer_insert_pixbuf(ctx->buffer, &ctx->iter, pix);
                        g_object_unref(pix);
                        ensure_newlines(ctx, 1);
                        loaded = TRUE;
                    }
                }
                g_free(img_path);

                if (!loaded) {
                    gchar* placeholder = g_strdup_printf("🖼 [%s]", (alt && alt[0]) ? alt : src);
                    insert_tag_text(ctx, placeholder, "html_tag");
                    g_free(placeholder);
                }
            }
            g_free(src);
            g_free(alt);
        } else if (strcmp(tag, "div") == 0 || strcmp(tag, "section") == 0 ||
                   strcmp(tag, "article") == 0 || strcmp(tag, "header") == 0 ||
                   strcmp(tag, "footer") == 0 || strcmp(tag, "nav") == 0 ||
                   strcmp(tag, "aside") == 0 || strcmp(tag, "main") == 0) {
            ensure_newlines(ctx, 1);
        }
    }
}

void render_html_to_buffer(GtkTextBuffer* buffer,
                           const gchar* html_text,
                           const gchar* doc_path,
                           gboolean is_preview,
                           const gchar* file_name,
                           gint64 total_size) {
    gtk_text_buffer_set_text(buffer, "", -1);

    HtmlRenderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.buffer = buffer;
    gtk_text_buffer_get_start_iter(buffer, &ctx.iter);
    ctx.active_tags = g_ptr_array_new();
    ctx.trailing_newlines = 2;
    ctx.last_was_ws = TRUE;

    gchar* doc_dir = (doc_path != NULL) ? g_path_get_dirname(doc_path) : NULL;
    ctx.doc_dir = doc_dir;

    if (is_preview) {
        gchar* hdr = g_strdup_printf("[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                                     "Showing first %d KB. Large file, display truncated.\n\n",
                                     file_name, total_size, (int)(PREVIEW_BYTES / 1024));
        insert_tag_text(&ctx, hdr, "html_preview_header");
        g_free(hdr);
        insert_tag_text(&ctx, "────────────────────────────────────────────────────────────────────────\n\n", "html_hr");
        ctx.trailing_newlines = 2;
    }

    /* Extract and display document title if present */
    const gchar* t_open = strstr_case(html_text, "<title", NULL);
    if (t_open != NULL) {
        const gchar* t_gt = strchr(t_open, '>');
        if (t_gt != NULL) {
            const gchar* t_close = strstr_case(t_gt, "</title", NULL);
            if (t_close != NULL && t_close > t_gt) {
                gchar* raw_title = g_strndup(t_gt + 1, t_close - (t_gt + 1));
                gchar* clean_title = strip_tags_and_entities(raw_title);
                g_free(raw_title);
                if (clean_title != NULL && clean_title[0] != '\0') {
                    insert_tag_text(&ctx, "🌐 ", "html_title");
                    insert_tag_text(&ctx, clean_title, "html_title");
                    ensure_newlines(&ctx, 1);
                    insert_tag_text(&ctx, "────────────────────────────────────────────────────────────────────────\n\n", "html_hr");
                    ctx.trailing_newlines = 2;
                }
                g_free(clean_title);
            }
        }
    }

    const gchar* p = html_text;
    while (*p != '\0') {
        /* Comments: <!-- ... --> */
        if (p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const gchar* cend = strstr(p + 4, "-->");
            if (cend != NULL) {
                p = cend + 3;
            } else {
                break;
            }
            continue;
        }

        /* Declarations: <! ... > */
        if (p[0] == '<' && p[1] == '!') {
            const gchar* dend = strchr(p + 2, '>');
            if (dend != NULL) {
                p = dend + 1;
            } else {
                break;
            }
            continue;
        }

        /* Processing instructions: <? ... ?> */
        if (p[0] == '<' && p[1] == '?') {
            const gchar* pend = strstr(p + 2, "?>");
            if (pend != NULL) {
                p = pend + 2;
            } else {
                break;
            }
            continue;
        }

        /* Skip scripts, styles, svg, noscript */
        if (p[0] == '<' && (g_ascii_strncasecmp(p + 1, "script", 6) == 0 ||
                            g_ascii_strncasecmp(p + 1, "style", 5) == 0 ||
                            g_ascii_strncasecmp(p + 1, "svg", 3) == 0 ||
                            g_ascii_strncasecmp(p + 1, "noscript", 8) == 0)) {
            const gchar* end_tag = NULL;
            if (g_ascii_strncasecmp(p + 1, "script", 6) == 0) end_tag = "</script";
            else if (g_ascii_strncasecmp(p + 1, "style", 5) == 0) end_tag = "</style";
            else if (g_ascii_strncasecmp(p + 1, "svg", 3) == 0) end_tag = "</svg";
            else end_tag = "</noscript";

            const gchar* close = strstr_case(p, end_tag, NULL);
            if (close != NULL) {
                const gchar* gt = strchr(close, '>');
                if (gt != NULL) {
                    p = gt + 1;
                    continue;
                }
            }
            break;
        }

        /* Skip <head> block contents (title extracted at top) */
        if (p[0] == '<' && g_ascii_strncasecmp(p + 1, "head", 4) == 0 &&
            (p[5] == '>' || isspace((unsigned char)p[5]))) {
            const gchar* close = strstr_case(p, "</head", NULL);
            if (close != NULL) {
                const gchar* gt = strchr(close, '>');
                if (gt != NULL) {
                    p = gt + 1;
                    continue;
                }
            }
        }

        /* Table */
        if (p[0] == '<' && g_ascii_strncasecmp(p + 1, "table", 5) == 0 &&
            (p[6] == '>' || isspace((unsigned char)p[6]))) {
            p = render_html_table(&ctx, p);
            continue;
        }

        /* HTML tag */
        if (*p == '<') {
            const gchar* tag_start = p;
            const gchar* tag_end = strchr(p, '>');
            if (tag_end == NULL) {
                p++;
                continue;
            }

            gboolean is_closing = (p[1] == '/');
            const gchar* name_start = is_closing ? p + 2 : p + 1;
            while (*name_start == ' ' || *name_start == '\t') name_start++;

            const gchar* name_end = name_start;
            while (*name_end != '\0' && !isspace((unsigned char)*name_end) &&
                   *name_end != '>' && *name_end != '/') {
                name_end++;
            }

            gchar* tag_name = g_ascii_strdown(name_start, name_end - name_start);
            gchar* full_tag = g_strndup(tag_start, tag_end - tag_start + 1);

            handle_html_tag(&ctx, tag_name, full_tag, is_closing);

            g_free(tag_name);
            g_free(full_tag);

            p = tag_end + 1;
            continue;
        }

        /* Preformatted text */
        if (ctx.in_pre) {
            if (*p == '&') {
                GString* ent_str = g_string_new(NULL);
                append_entity(ent_str, &p);
                insert_styled_text(&ctx, ent_str->str, ent_str->len);
                g_string_free(ent_str, TRUE);
            } else {
                const gchar* text_end = p;
                while (*text_end != '\0' && *text_end != '<' && *text_end != '&') {
                    text_end++;
                }
                insert_styled_text(&ctx, p, text_end - p);
                p = text_end;
            }
            continue;
        }

        /* Normal text: whitespace collapsing & entity decoding */
        if (isspace((unsigned char)*p)) {
            if (!ctx.last_was_ws && ctx.trailing_newlines == 0) {
                insert_styled_text(&ctx, " ", 1);
                ctx.last_was_ws = TRUE;
            }
            p++;
            continue;
        }

        if (*p == '&') {
            GString* ent_str = g_string_new(NULL);
            append_entity(ent_str, &p);
            insert_styled_text(&ctx, ent_str->str, ent_str->len);
            ctx.last_was_ws = (ent_str->len > 0 && ent_str->str[ent_str->len - 1] == ' ');
            g_string_free(ent_str, TRUE);
            continue;
        }

        /* Regular text run */
        const gchar* text_end = p;
        while (*text_end != '\0' && *text_end != '<' && *text_end != '&' &&
               !isspace((unsigned char)*text_end)) {
            text_end++;
        }
        insert_styled_text(&ctx, p, text_end - p);
        ctx.last_was_ws = FALSE;
        p = text_end;
    }

    ensure_newlines(&ctx, 1);

    g_ptr_array_free(ctx.active_tags, TRUE);
    g_free(ctx.current_link_url);
    g_free(doc_dir);
}
