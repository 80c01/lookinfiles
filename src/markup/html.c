#include "markup/html.h"
#include "markup/html_text.h"
#include "core/i18n.h"
#include "image_utils.h"
#include "theme/icons.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* Renderer context */
typedef struct {
    GtkTextBuffer* buffer;
    GtkTextIter iter;
    GtkTextView* view;
    const gchar* doc_dir;
    const gchar* p_cur;

    /* Parser state */
    gboolean in_pre;
    gboolean in_pre_code;
    gchar* pre_lang;
    gint quote_depth;
    gboolean line_needs_quote_bar;

    /* Lists: depth, ordered flag, count, type */
    struct {
        gboolean is_ordered;
        gint count;
        gchar type; /* '1', 'a', 'A', 'i', 'I' */
    } list_stack[16];
    gint list_depth;

    /* List item (li) state */
    gboolean in_li;
    gchar current_li_tag[32];
    gint current_li_depth;
    gboolean current_li_ordered;
    gboolean li_has_text;
    gint li_paragraph_count;

    /* Active styling tags */
    GPtrArray* active_tags;

    /* Link & Abbr tracking */
    gchar* current_link_url;
    gchar* current_abbr_title;
    GString* current_link_text;
    gboolean in_link;

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

static void close_current_li(HtmlRenderContext* ctx) {
    if (ctx->in_li) {
        if (ctx->current_li_tag[0] != '\0') {
            pop_tag(ctx, ctx->current_li_tag);
            ctx->current_li_tag[0] = '\0';
        }
        ctx->in_li = FALSE;
        ctx->li_has_text = FALSE;
        ctx->li_paragraph_count = 0;
    }
}

static void insert_tag_text(HtmlRenderContext* ctx, const gchar* text, const gchar* tag_name) {
    if (text == NULL || *text == '\0') return;
    GtkTextMark* m = gtk_text_buffer_create_mark(ctx->buffer, NULL, &ctx->iter, TRUE);
    gtk_text_buffer_insert(ctx->buffer, &ctx->iter, text, -1);
    apply_tag_mark(ctx->buffer, tag_name, m, &ctx->iter);
    if (ctx->in_li && ctx->current_li_tag[0] != '\0') {
        apply_tag_mark(ctx->buffer, ctx->current_li_tag, m, &ctx->iter);
    }
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

/* Theme glyph (icons.h) at the insertion point, then a space */
static void insert_glyph(HtmlRenderContext* ctx, const gchar* glyph, const gchar* tag_name) {
    text_insert_glyph(ctx->buffer, &ctx->iter, glyph, tag_name);
    ctx->trailing_newlines = 0;
}

static void insert_styled_text(HtmlRenderContext* ctx, const gchar* text, gssize len) {
    if (text == NULL || len == 0 || *text == '\0') return;
    if (len < 0) len = strlen(text);

    if (ctx->in_link && ctx->current_link_text != NULL) {
        g_string_append_len(ctx->current_link_text, text, len);
    }

    if (ctx->in_li && len > 0) {
        for (gssize k = 0; k < len; k++) {
            if (!g_ascii_isspace(text[k])) {
                ctx->li_has_text = TRUE;
                break;
            }
        }
    }

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

typedef struct {
    gchar* text;
    gboolean is_num;
} HtmlTableCell;

static void free_table_cell(gpointer data) {
    HtmlTableCell* c = (HtmlTableCell*)data;
    if (c != NULL) {
        g_free(c->text);
        g_free(c);
    }
}

static const gchar* render_html_table(HtmlRenderContext* ctx, const gchar* p) {
    const gchar* table_end = html_text_find_case(p, "</table", NULL);
    const gchar* search_limit = (table_end != NULL) ? table_end + 8 : p + strlen(p);

    /* Extract and render table caption if present */
    const gchar* cap_start = html_text_find_case(p, "<caption", search_limit);
    if (cap_start != NULL && cap_start < search_limit) {
        const gchar* cap_gt = strchr(cap_start, '>');
        if (cap_gt != NULL && cap_gt < search_limit) {
            const gchar* cap_end = html_text_find_case(cap_gt, "</caption", search_limit);
            if (cap_end != NULL && cap_end < search_limit) {
                gchar* raw_cap = g_strndup(cap_gt + 1, cap_end - (cap_gt + 1));
                gchar* clean_cap = html_text_strip(raw_cap);
                g_free(raw_cap);
                if (clean_cap != NULL && clean_cap[0] != '\0') {
                    ensure_newlines(ctx, 2);
                    gchar* cap_str = g_strdup_printf("  %s\n", clean_cap);
                    insert_tag_text(ctx, cap_str, "html_table_caption");
                    g_free(cap_str);
                }
                g_free(clean_cap);
            }
        }
    }

    GPtrArray* rows = g_ptr_array_new();
    GArray* is_header_row = g_array_new(FALSE, FALSE, sizeof(gboolean));

    const gchar* cur = p;
    while (cur < search_limit) {
        const gchar* tr_start = html_text_find_case(cur, "<tr", search_limit);
        if (tr_start == NULL || tr_start >= search_limit) break;

        const gchar* tr_close_tag = strchr(tr_start, '>');
        if (tr_close_tag == NULL || tr_close_tag >= search_limit) break;

        const gchar* tr_end = html_text_find_case(tr_close_tag, "</tr", search_limit);
        if (tr_end == NULL || tr_end > search_limit) {
            const gchar* next_tr = html_text_find_case(tr_close_tag + 1, "<tr", search_limit);
            tr_end = (next_tr != NULL) ? next_tr : search_limit;
        }

        GPtrArray* row_cells = g_ptr_array_new_with_free_func(free_table_cell);
        gboolean row_has_th = FALSE;

        const gchar* cell_cur = tr_close_tag + 1;
        while (cell_cur < tr_end) {
            const gchar* th_start = html_text_find_case(cell_cur, "<th", tr_end);
            const gchar* td_start = html_text_find_case(cell_cur, "<td", tr_end);

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
            if (is_th) cell_end = html_text_find_case(cell_close_tag, "</th", tr_end);
            else cell_end = html_text_find_case(cell_close_tag, "</td", tr_end);

            if (cell_end == NULL || cell_end > tr_end) {
                const gchar* next_cell = html_text_find_case(cell_close_tag + 1, "<t", tr_end);
                cell_end = (next_cell != NULL) ? next_cell : tr_end;
            }

            gchar* cell_raw = g_strndup(cell_close_tag + 1, cell_end - (cell_close_tag + 1));
            gchar* clean = html_text_strip(cell_raw);
            g_free(cell_raw);

            /* Detect if cell is numeric for right alignment */
            gboolean is_num = FALSE;
            if (clean != NULL && clean[0] != '\0') {
                const gchar* s = clean;
                while (*s == ' ' || *s == '$' || *s == '+' || *s == '-' ||
                       (unsigned char)*s == 0xC2 /* non-breaking space / currency */) s++;
                if (isdigit((unsigned char)*s)) is_num = TRUE;
            }

            HtmlTableCell* tc = g_new0(HtmlTableCell, 1);
            tc->text = clean;
            tc->is_num = is_num;
            g_ptr_array_add(row_cells, tc);

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
                    HtmlTableCell* tc = g_ptr_array_index(rc, c);
                    gint clen = (gint)g_utf8_strlen(tc->text, -1);
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
                    HtmlTableCell* tc = (c < rc->len) ? g_ptr_array_index(rc, c) : NULL;
                    const gchar* cell_text = tc ? tc->text : "";
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
                    if (tc && tc->is_num && !is_hdr) {
                        g_string_append_c(row_str, ' ');
                        for (gint p_pad = 0; p_pad < pad; p_pad++) g_string_append_c(row_str, ' ');
                        g_string_append_printf(row_str, "%s │", display_text);
                    } else {
                        g_string_append_printf(row_str, " %s", display_text);
                        for (gint p_pad = 0; p_pad < pad; p_pad++) g_string_append_c(row_str, ' ');
                        g_string_append(row_str, " │");
                    }
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
                   strcmp(tag, "dfn") == 0 || strcmp(tag, "address") == 0) {
            pop_tag(ctx, "html_italic");
        } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
            pop_tag(ctx, "html_underline");
        } else if (strcmp(tag, "s") == 0 || strcmp(tag, "del") == 0 || strcmp(tag, "strike") == 0) {
            pop_tag(ctx, "html_strike");
        } else if (strcmp(tag, "code") == 0 || strcmp(tag, "samp") == 0 || strcmp(tag, "tt") == 0) {
            if (ctx->in_pre && ctx->in_pre_code) {
                ctx->in_pre_code = FALSE;
            } else {
                pop_tag(ctx, "html_code");
            }
        } else if (strcmp(tag, "kbd") == 0) {
            pop_tag(ctx, "html_kbd");
        } else if (strcmp(tag, "mark") == 0) {
            pop_tag(ctx, "html_mark");
        } else if (strcmp(tag, "small") == 0) {
            pop_tag(ctx, "html_small");
        } else if (strcmp(tag, "sub") == 0) {
            pop_tag(ctx, "html_sub");
        } else if (strcmp(tag, "sup") == 0) {
            pop_tag(ctx, "html_sup");
        } else if (strcmp(tag, "abbr") == 0) {
            pop_tag(ctx, "html_underline");
            if (ctx->current_abbr_title != NULL && ctx->current_abbr_title[0] != '\0') {
                gchar* abbr_info = g_strdup_printf(" (%s)", ctx->current_abbr_title);
                insert_tag_text(ctx, abbr_info, "html_tag");
                g_free(abbr_info);
                g_free(ctx->current_abbr_title);
                ctx->current_abbr_title = NULL;
            }
        } else if (strcmp(tag, "a") == 0) {
            pop_tag(ctx, "html_link");
            ctx->in_link = FALSE;
            if (ctx->current_link_url != NULL && ctx->current_link_url[0] != '\0') {
                const gchar* url = ctx->current_link_url;
                gchar* trimmed = ctx->current_link_text ? g_strstrip(g_strdup(ctx->current_link_text->str)) : NULL;
                gboolean is_anchor = (url[0] == '#');
                gboolean same_as_text = (trimmed != NULL && g_strcmp0(trimmed, url) == 0);

                if (!same_as_text && !is_anchor) {
                    gchar* link_info = g_strdup_printf(" ↗ [%s]", url);
                    insert_tag_text(ctx, link_info, "html_tag");
                    g_free(link_info);
                } else if (is_anchor && url[1] != '\0' && !same_as_text) {
                    gchar* link_info = g_strdup_printf(" [%s]", url);
                    insert_tag_text(ctx, link_info, "html_tag");
                    g_free(link_info);
                }
                g_free(trimmed);
                g_free(ctx->current_link_url);
                ctx->current_link_url = NULL;
            }
            if (ctx->current_link_text != NULL) {
                g_string_free(ctx->current_link_text, TRUE);
                ctx->current_link_text = NULL;
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
            if (ctx->in_li) {
                ensure_newlines(ctx, 1);
            } else {
                ensure_newlines(ctx, 2);
            }
        } else if (strcmp(tag, "blockquote") == 0) {
            pop_tag(ctx, "html_quote");
            if (ctx->quote_depth > 0) ctx->quote_depth--;
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "pre") == 0) {
            pop_tag(ctx, "html_pre");
            ctx->in_pre = FALSE;
            ctx->in_pre_code = FALSE;
            g_free(ctx->pre_lang);
            ctx->pre_lang = NULL;
            ensure_newlines(ctx, 2);
        } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
            close_current_li(ctx);
            if (ctx->list_depth > 0) ctx->list_depth--;
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "li") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "dl") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "dt") == 0) {
            pop_tag(ctx, "html_dt");
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "dd") == 0) {
            pop_tag(ctx, "html_dd");
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "details") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "summary") == 0) {
            pop_tag(ctx, "html_summary");
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "figure") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "figcaption") == 0) {
            pop_tag(ctx, "html_figcaption");
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
                   strcmp(tag, "dfn") == 0 || strcmp(tag, "address") == 0) {
            push_tag(ctx, "html_italic");
        } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
            push_tag(ctx, "html_underline");
        } else if (strcmp(tag, "s") == 0 || strcmp(tag, "del") == 0 || strcmp(tag, "strike") == 0) {
            push_tag(ctx, "html_strike");
        } else if (strcmp(tag, "code") == 0 || strcmp(tag, "samp") == 0 || strcmp(tag, "tt") == 0) {
            if (ctx->in_pre) {
                ctx->in_pre_code = TRUE;
                if (ctx->pre_lang == NULL) {
                    ctx->pre_lang = html_text_code_lang(full_tag);
                    if (ctx->pre_lang != NULL && ctx->pre_lang[0] != '\0') {
                        gchar* hdr = g_strdup_printf("  ──  %s  ──\n", ctx->pre_lang);
                        insert_tag_text(ctx, hdr, "html_code_header");
                        g_free(hdr);
                    }
                }
            } else {
                push_tag(ctx, "html_code");
            }
        } else if (strcmp(tag, "kbd") == 0) {
            push_tag(ctx, "html_kbd");
        } else if (strcmp(tag, "mark") == 0) {
            push_tag(ctx, "html_mark");
        } else if (strcmp(tag, "small") == 0) {
            push_tag(ctx, "html_small");
        } else if (strcmp(tag, "sub") == 0) {
            push_tag(ctx, "html_sub");
        } else if (strcmp(tag, "sup") == 0) {
            push_tag(ctx, "html_sup");
        } else if (strcmp(tag, "abbr") == 0) {
            g_free(ctx->current_abbr_title);
            ctx->current_abbr_title = html_text_attr(full_tag, "title");
            push_tag(ctx, "html_underline");
        } else if (strcmp(tag, "a") == 0) {
            g_free(ctx->current_link_url);
            ctx->current_link_url = html_text_attr(full_tag, "href");
            if (ctx->current_link_text != NULL) {
                g_string_free(ctx->current_link_text, TRUE);
            }
            ctx->current_link_text = g_string_new(NULL);
            ctx->in_link = TRUE;
            push_tag(ctx, "html_link");
        } else if (strcmp(tag, "h1") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h1");
        } else if (strcmp(tag, "h2") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h2");
        } else if (strcmp(tag, "h3") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h3");
        } else if (strcmp(tag, "h4") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h4");
        } else if (strcmp(tag, "h5") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h5");
        } else if (strcmp(tag, "h6") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            push_tag(ctx, "html_h6");
        } else if (strcmp(tag, "p") == 0) {
            if (ctx->in_li) {
                if (ctx->li_has_text) {
                    if (ctx->current_li_tag[0] != '\0') {
                        pop_tag(ctx, ctx->current_li_tag);
                    }
                    if (ctx->current_li_ordered) {
                        g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag),
                                   "html_li_p_ol_d%d", ctx->current_li_depth);
                    } else {
                        g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag),
                                   "html_li_p_ul_d%d", ctx->current_li_depth);
                    }
                    push_tag(ctx, ctx->current_li_tag);
                    ctx->li_paragraph_count++;
                    ctx->li_has_text = FALSE;
                    ensure_newlines(ctx, 1);
                }
            } else {
                ensure_newlines(ctx, 2);
            }
        } else if (strcmp(tag, "br") == 0) {
            gtk_text_buffer_insert(ctx->buffer, &ctx->iter, "\n", 1);
            ctx->trailing_newlines++;
            ctx->last_was_ws = TRUE;
            ctx->line_needs_quote_bar = TRUE;
            if (ctx->in_li && ctx->li_paragraph_count == 1) {
                if (ctx->current_li_tag[0] != '\0') {
                    pop_tag(ctx, ctx->current_li_tag);
                }
                if (ctx->current_li_ordered) {
                    g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag),
                               "html_li_p_ol_d%d", ctx->current_li_depth);
                } else {
                    g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag),
                               "html_li_p_ul_d%d", ctx->current_li_depth);
                }
                push_tag(ctx, ctx->current_li_tag);
                ctx->li_paragraph_count++;
                ctx->li_has_text = FALSE;
            }
        } else if (strcmp(tag, "hr") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
            insert_tag_text(ctx, "────────────────────────────────────────────────────────────────────────\n", "html_hr");
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "blockquote") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            ctx->quote_depth++;
            ctx->line_needs_quote_bar = TRUE;
            push_tag(ctx, "html_quote");
        } else if (strcmp(tag, "pre") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 2);
            ctx->in_pre = TRUE;
            ctx->in_pre_code = FALSE;
            g_free(ctx->pre_lang);
            ctx->pre_lang = html_text_code_lang(full_tag);
            if (ctx->pre_lang != NULL && ctx->pre_lang[0] != '\0') {
                gchar* hdr = g_strdup_printf("  ──  %s  ──\n", ctx->pre_lang);
                insert_tag_text(ctx, hdr, "html_code_header");
                g_free(hdr);
            }
            push_tag(ctx, "html_pre");
        } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
            if (ctx->list_depth < 16) {
                gchar* type_attr = html_text_attr(full_tag, "type");
                gchar* start_attr = html_text_attr(full_tag, "start");
                gchar ltype = (type_attr && type_attr[0]) ? type_attr[0] : '1';
                gint start_num = (start_attr && start_attr[0]) ? atoi(start_attr) : 1;
                g_free(type_attr);
                g_free(start_attr);

                ctx->list_stack[ctx->list_depth].is_ordered = (strcmp(tag, "ol") == 0);
                ctx->list_stack[ctx->list_depth].count = start_num - 1;
                ctx->list_stack[ctx->list_depth].type = ltype;
                ctx->list_depth++;
            }
        } else if (strcmp(tag, "li") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
            gint depth = (ctx->list_depth > 0) ? ctx->list_depth - 1 : 0;
            if (depth > 7) depth = 7;
            gboolean ordered = (ctx->list_depth > 0 && ctx->list_stack[depth].is_ordered);

            ctx->current_li_depth = depth;
            ctx->current_li_ordered = ordered;
            ctx->in_li = TRUE;
            ctx->li_has_text = FALSE;
            ctx->li_paragraph_count = 1;

            /* Check if this li is a task list item */
            gboolean is_task = FALSE;
            if (html_text_find_case(full_tag, "task", NULL) != NULL) {
                is_task = TRUE;
            } else if (ctx->p_cur != NULL) {
                const gchar* peek = ctx->p_cur;
                while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n') peek++;
                if (peek[0] == '<' && (peek[1] == 'i' || peek[1] == 'I') &&
                    g_ascii_strncasecmp(peek + 1, "input", 5) == 0) {
                    const gchar* gt = strchr(peek, '>');
                    if (gt != NULL) {
                        gchar* inp = g_strndup(peek, gt - peek + 1);
                        gchar* itype = html_text_attr(inp, "type");
                        if (itype != NULL && g_ascii_strcasecmp(itype, "checkbox") == 0) {
                            is_task = TRUE;
                        }
                        g_free(itype);
                        g_free(inp);
                    }
                }
            }

            if (is_task) {
                g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag), "html_li_task_d%d", depth);
                push_tag(ctx, ctx->current_li_tag);
                /* Do not insert bullet; the <input type="checkbox"> will be the bullet */
            } else if (ordered) {
                g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag), "html_li_ol_d%d", depth);
                push_tag(ctx, ctx->current_li_tag);

                ctx->list_stack[depth].count++;
                gint num = ctx->list_stack[depth].count;
                gchar ltype = ctx->list_stack[depth].type;
                gchar* num_str = NULL;
                if (ltype == 'a' && num >= 1 && num <= 26) {
                    num_str = g_strdup_printf("%c. ", 'a' + num - 1);
                } else if (ltype == 'A' && num >= 1 && num <= 26) {
                    num_str = g_strdup_printf("%c. ", 'A' + num - 1);
                } else if (ltype == 'i' || ltype == 'I') {
                    const gchar* romans[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii"};
                    const gchar* ROMANS[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
                    if (num >= 1 && num <= 12) {
                        num_str = g_strdup_printf("%s. ", (ltype == 'i') ? romans[num] : ROMANS[num]);
                    } else {
                        num_str = g_strdup_printf("%d. ", num);
                    }
                } else {
                    num_str = g_strdup_printf("%d. ", num);
                }
                insert_tag_text(ctx, num_str, "html_num");
                g_free(num_str);
            } else {
                g_snprintf(ctx->current_li_tag, sizeof(ctx->current_li_tag), "html_li_ul_d%d", depth);
                push_tag(ctx, ctx->current_li_tag);

                const gchar* bullet = (depth % 3 == 0) ? "• " : (depth % 3 == 1) ? "◦ " : "▪ ";
                insert_tag_text(ctx, bullet, "html_bullet");
            }
            ctx->trailing_newlines = 0;
            ctx->last_was_ws = TRUE;
        } else if (strcmp(tag, "dl") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "dt") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
            push_tag(ctx, "html_dt");
        } else if (strcmp(tag, "dd") == 0) {
            close_current_li(ctx);
            ensure_newlines(ctx, 1);
            push_tag(ctx, "html_dd");
        } else if (strcmp(tag, "details") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "summary") == 0) {
            ensure_newlines(ctx, 1);
            insert_tag_text(ctx, "▾ ", "html_summary");
            push_tag(ctx, "html_summary");
        } else if (strcmp(tag, "figure") == 0) {
            ensure_newlines(ctx, 1);
        } else if (strcmp(tag, "figcaption") == 0) {
            ensure_newlines(ctx, 1);
            push_tag(ctx, "html_figcaption");
        } else if (strcmp(tag, "input") == 0) {
            gchar* type = html_text_attr(full_tag, "type");
            if (type != NULL && g_ascii_strcasecmp(type, "checkbox") == 0) {
                gboolean checked = (html_text_attr(full_tag, "checked") != NULL ||
                                    html_text_find_case(full_tag, "checked", NULL) != NULL);
                if (checked) {
                    insert_tag_text(ctx, "☑ ", "html_task_done");
                } else {
                    insert_tag_text(ctx, "☐ ", "html_task_todo");
                }
            }
            g_free(type);
        } else if (strcmp(tag, "img") == 0) {
            gchar* src = html_text_attr(full_tag, "src");
            gchar* alt = html_text_attr(full_tag, "alt");
            if (src != NULL && src[0] != '\0') {
                /* Natural size or width="N" pixels, shrunk to the view
                 * width (CSS "max-width: 100%", as in VS Code) */
                gint max_width = FIT_IMAGE_MAX_WIDTH;
                gchar* width_attr = html_text_attr(full_tag, "width");
                if (width_attr != NULL) {
                    gchar* end = NULL;
                    gint64 w = g_ascii_strtoll(width_attr, &end, 10);
                    if (w > 0 && end != width_attr && *end != '%') {
                        max_width = (gint)MIN(w, FIT_IMAGE_MAX_WIDTH);
                    }
                    g_free(width_attr);
                }
                GdkPixbuf* pix = NULL;
                if (g_str_has_prefix(src, "data:image/")) {
                    pix = pixbuf_from_data_uri(src, max_width);
                } else {
                    gchar* img_path = NULL;
                    if (g_path_is_absolute(src)) {
                        img_path = g_strdup(src);
                    } else if (ctx->doc_dir != NULL) {
                        img_path = g_build_filename(ctx->doc_dir, src, NULL);
                    }
                    if (img_path != NULL && g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                        pix = pixbuf_from_file(img_path, max_width);
                    }
                    g_free(img_path);
                }

                if (pix != NULL) {
                    ensure_newlines(ctx, 1);
                    GdkTexture* tex = texture_new_for_pixbuf(pix);
                    GdkPaintable* fit = fit_image_new(tex, ctx->view);
                    gtk_text_buffer_insert_paintable(ctx->buffer, &ctx->iter, fit);
                    g_object_unref(fit);
                    g_object_unref(tex);
                    g_object_unref(pix);
                    ctx->trailing_newlines = 0; /* the image is on the line: end it */
                    ensure_newlines(ctx, 1);
                } else {
                    insert_glyph(ctx, "image", "html_tag");
                    gchar* placeholder = g_strdup_printf("[%s]", (alt && alt[0]) ? alt : src);
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
                           GtkTextView* view,
                           const gchar* html_text,
                           const gchar* doc_path,
                           gboolean is_preview,
                           const gchar* file_name,
                           gint64 total_size) {
    gtk_text_buffer_set_text(buffer, "", -1);

    HtmlRenderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.buffer = buffer;
    ctx.view = view;
    gtk_text_buffer_get_start_iter(buffer, &ctx.iter);
    ctx.active_tags = g_ptr_array_new();
    ctx.trailing_newlines = 2;
    ctx.last_was_ws = TRUE;

    gchar* doc_dir = (doc_path != NULL) ? g_path_get_dirname(doc_path) : NULL;
    ctx.doc_dir = doc_dir;

    if (is_preview) {
        gchar* total = g_format_size((guint64)MAX(total_size, 0));
        gchar* hdr = g_strdup_printf(_("[ PREVIEW ] %1$s (%2$s in all)\nShowing the first %3$d KB of this large file.\n\n"),
                                     file_name, total, (int)(PREVIEW_BYTES / 1024));
        g_free(total);
        insert_tag_text(&ctx, hdr, "html_preview_header");
        g_free(hdr);
        insert_tag_text(&ctx, "────────────────────────────────────────────────────────────────────────\n\n", "html_hr");
        ctx.trailing_newlines = 2;
    }

    /* Extract and display document title & metadata if present */
    const gchar* t_open = html_text_find_case(html_text, "<title", NULL);
    gboolean title_rendered = FALSE;
    if (t_open != NULL) {
        const gchar* t_gt = strchr(t_open, '>');
        if (t_gt != NULL) {
            const gchar* t_close = html_text_find_case(t_gt, "</title", NULL);
            if (t_close != NULL && t_close > t_gt) {
                gchar* raw_title = g_strndup(t_gt + 1, t_close - (t_gt + 1));
                gchar* clean_title = html_text_strip(raw_title);
                g_free(raw_title);
                if (clean_title != NULL && clean_title[0] != '\0') {
                    insert_glyph(&ctx, "link", "html_title");
                    insert_tag_text(&ctx, clean_title, "html_title");
                    ensure_newlines(&ctx, 1);
                    title_rendered = TRUE;
                }
                g_free(clean_title);
            }
        }
    }

    /* Metadata subtitle: author / description */
    gchar* raw_author = html_text_meta(html_text, "author");
    gchar* raw_desc = html_text_meta(html_text, "description");
    gchar* clean_author = raw_author ? html_text_strip(raw_author) : NULL;
    gchar* clean_desc = raw_desc ? html_text_strip(raw_desc) : NULL;
    g_free(raw_author);
    g_free(raw_desc);

    if (clean_desc != NULL && g_utf8_strlen(clean_desc, -1) > 120) {
        gchar* trunc = g_strndup(clean_desc, 120);
        gchar* tmp = g_strconcat(trunc, "…", NULL);
        g_free(trunc);
        g_free(clean_desc);
        clean_desc = tmp;
    }

    if ((clean_author && clean_author[0] != '\0') || (clean_desc && clean_desc[0] != '\0')) {
        gboolean author = clean_author && clean_author[0] != '\0';
        insert_tag_text(&ctx, "  ", "html_meta_sub");
        if (author) {
            insert_glyph(&ctx, "author", "html_meta_sub");
            insert_tag_text(&ctx, clean_author, "html_meta_sub");
        }
        if (clean_desc && clean_desc[0] != '\0') {
            if (author) insert_tag_text(&ctx, "  •  ", "html_meta_sub");
            insert_glyph(&ctx, "description", "html_meta_sub");
            insert_tag_text(&ctx, clean_desc, "html_meta_sub");
        }
        insert_tag_text(&ctx, "\n", "html_meta_sub");
        title_rendered = TRUE;
    }
    g_free(clean_author);
    g_free(clean_desc);

    if (title_rendered) {
        insert_tag_text(&ctx, "────────────────────────────────────────────────────────────────────────\n\n", "html_hr");
        ctx.trailing_newlines = 2;
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

            const gchar* close = html_text_find_case(p, end_tag, NULL);
            if (close != NULL) {
                const gchar* gt = strchr(close, '>');
                if (gt != NULL) {
                    p = gt + 1;
                    continue;
                }
            }
            break;
        }

        /* Skip <head> block contents (title/meta extracted at top) */
        if (p[0] == '<' && g_ascii_strncasecmp(p + 1, "head", 4) == 0 &&
            (p[5] == '>' || isspace((unsigned char)p[5]))) {
            const gchar* close = html_text_find_case(p, "</head", NULL);
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
            close_current_li(&ctx);
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

            ctx.p_cur = tag_end + 1;
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
                html_text_append_entity(ent_str, &p);
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
            html_text_append_entity(ent_str, &p);
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

    close_current_li(&ctx);
    ensure_newlines(&ctx, 1);

    g_ptr_array_free(ctx.active_tags, TRUE);
    g_free(ctx.pre_lang);
    g_free(ctx.current_link_url);
    g_free(ctx.current_abbr_title);
    if (ctx.current_link_text != NULL) {
        g_string_free(ctx.current_link_text, TRUE);
    }
    g_free(doc_dir);
}
