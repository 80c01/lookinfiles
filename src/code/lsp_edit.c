/* Pure text-edit application and buffer identifier extraction (see lsp_edit.h) */
#include "code/lsp_edit.h"

#include <gio/gio.h>
#include <string.h>

typedef struct {
    gsize start;
    gsize end;
    guint order;
    const gchar* new_text;
} ResolvedEdit;

static GArray* build_line_starts(const gchar* text, gsize len) {
    GArray* starts = g_array_new(FALSE, FALSE, sizeof(gsize));
    gsize pos = 0;
    g_array_append_val(starts, pos);
    for (gsize i = 0; i < len; i++) {
        if (text[i] == '\n') {
            gsize next = i + 1;
            g_array_append_val(starts, next);
        }
    }
    return starts;
}

static gsize resolve_pos(const gchar* text, gsize text_len, const GArray* starts,
                         gint line, gint col, gboolean utf8_positions) {
    if (line < 0) return 0;
    if ((guint)line >= starts->len) return text_len;
    gsize ls = g_array_index(starts, gsize, (guint)line);
    gsize le = ((guint)line + 1 < starts->len) ? g_array_index(starts, gsize, (guint)line + 1) : text_len;
    /* Strip line ending (\n or \r\n) when computing line length */
    if (le > ls && text[le - 1] == '\n') le--;
    if (le > ls && text[le - 1] == '\r') le--;
    gint byte_col = lsp_column_to_byte(text + ls, (gssize)(le - ls), col, utf8_positions);
    return ls + (gsize)MAX(byte_col, 0);
}

static gint cmp_resolved_asc(gconstpointer a, gconstpointer b) {
    const ResolvedEdit* ea = a;
    const ResolvedEdit* eb = b;
    if (ea->start != eb->start) return ea->start < eb->start ? -1 : 1;
    if (ea->end != eb->end) return ea->end < eb->end ? -1 : 1;
    return ea->order < eb->order ? -1 : (ea->order > eb->order ? 1 : 0);
}

gchar* lsp_edit_apply(const gchar* text, GPtrArray* edits, gboolean utf8_positions, GError** error) {
    if (text == NULL) text = "";
    if (edits == NULL || edits->len == 0) return g_strdup(text);

    gsize text_len = strlen(text);
    GArray* starts = build_line_starts(text, text_len);
    GArray* resolved = g_array_sized_new(FALSE, FALSE, sizeof(ResolvedEdit), edits->len);

    for (guint i = 0; i < edits->len; i++) {
        const LspTextEdit* te = g_ptr_array_index(edits, i);
        if (te == NULL) continue;
        gsize s = resolve_pos(text, text_len, starts, te->start_line, te->start_col, utf8_positions);
        gsize e = resolve_pos(text, text_len, starts, te->end_line, te->end_col, utf8_positions);
        if (e < s) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Invalid edit range (end before start)");
            g_array_free(resolved, TRUE);
            g_array_free(starts, TRUE);
            return NULL;
        }
        ResolvedEdit re = {.start = s, .end = e, .order = i, .new_text = te->new_text != NULL ? te->new_text : ""};
        g_array_append_val(resolved, re);
    }
    g_array_free(starts, TRUE);

    g_array_sort(resolved, cmp_resolved_asc);

    for (guint i = 1; i < resolved->len; i++) {
        const ResolvedEdit* prev = &g_array_index(resolved, ResolvedEdit, i - 1);
        const ResolvedEdit* curr = &g_array_index(resolved, ResolvedEdit, i);
        if (curr->start < prev->end) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Overlapping text edits");
            g_array_free(resolved, TRUE);
            return NULL;
        }
    }

    GString* out = g_string_sized_new(text_len);
    gsize cursor = 0;
    for (guint i = 0; i < resolved->len; i++) {
        const ResolvedEdit* re = &g_array_index(resolved, ResolvedEdit, i);
        if (re->start > cursor) {
            g_string_append_len(out, text + cursor, (gssize)(re->start - cursor));
        }
        g_string_append(out, re->new_text);
        cursor = re->end;
    }
    if (cursor < text_len) {
        g_string_append_len(out, text + cursor, (gssize)(text_len - cursor));
    }

    g_array_free(resolved, TRUE);
    return g_string_free(out, FALSE);
}

typedef struct {
    gchar* word;
    gint rank; /* 0 = exact-case prefix, 1 = ci prefix, 2 = substring */
} IdentHit;

static gint cmp_ident(gconstpointer a, gconstpointer b) {
    const IdentHit* ha = a;
    const IdentHit* hb = b;
    if (ha->rank != hb->rank) return ha->rank - hb->rank;
    return g_strcmp0(ha->word, hb->word);
}

GPtrArray* lsp_edit_buffer_identifiers(const gchar* text, gsize len, CodeLang lang,
                                       const gchar* prefix, guint limit) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    if (text == NULL || len == 0 || limit == 0) return out;
    if (prefix == NULL) prefix = "";
    gsize pfx_len = strlen(prefix);
    gchar* pfx_low = g_ascii_strdown(prefix, (gssize)pfx_len);

    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GArray* hits = g_array_new(FALSE, FALSE, sizeof(IdentHit));

    gsize i = 0;
    while (i < len) {
        guchar b = (guchar)text[i];
        if (!code_is_word_char(lang, b) || g_ascii_isdigit(b)) {
            i++;
            continue;
        }
        gsize start = i;
        while (i < len && code_is_word_char(lang, (guchar)text[i])) i++;
        gsize wlen = i - start;
        if (wlen < 2 || wlen > 128) continue;
        if (pfx_len > 0 && wlen == pfx_len && memcmp(text + start, prefix, wlen) == 0) continue;

        gchar* word = g_strndup(text + start, wlen);
        if (g_hash_table_contains(seen, word)) {
            g_free(word);
            continue;
        }
        gint rank = -1;
        if (pfx_len == 0) {
            rank = 0;
        } else if (wlen >= pfx_len && memcmp(word, prefix, pfx_len) == 0) {
            rank = 0;
        } else {
            gchar* low = g_ascii_strdown(word, (gssize)wlen);
            if (g_str_has_prefix(low, pfx_low)) rank = 1;
            else if (strstr(low, pfx_low) != NULL) rank = 2;
            g_free(low);
        }
        if (rank >= 0) {
            g_hash_table_add(seen, g_strdup(word));
            IdentHit h = {.word = word, .rank = rank};
            g_array_append_val(hits, h);
        } else {
            g_free(word);
        }
    }

    g_array_sort(hits, cmp_ident);
    guint take = MIN(hits->len, limit);
    for (guint k = 0; k < take; k++) {
        g_ptr_array_add(out, g_array_index(hits, IdentHit, k).word);
    }
    for (guint k = take; k < hits->len; k++) {
        g_free(g_array_index(hits, IdentHit, k).word);
    }

    g_array_free(hits, TRUE);
    g_hash_table_destroy(seen);
    g_free(pfx_low);
    return out;
}
