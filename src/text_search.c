/* Search the files of a project for a text (see text_search.h) */
#include "text_search.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define BINARY_SNIFF 8192

gboolean text_search_exact_case(const gchar* query) {
    for (const gchar* c = query; c != NULL && *c != '\0'; c++) {
        if (g_ascii_isupper(*c)) return TRUE;
    }
    return FALSE;
}

static gboolean is_binary(const gchar* text, gsize len) {
    return memchr(text, '\0', MIN(len, BINARY_SNIFF)) != NULL;
}

/* The line at line_start as shown: tabs and CR as spaces (columns stay
 * right), valid UTF-8 */
static gchar* shown_line(const gchar* line_start, const gchar* end) {
    const gchar* nl = memchr(line_start, '\n', (gsize)(end - line_start));
    gsize ll = nl != NULL ? (gsize)(nl - line_start) : (gsize)(end - line_start);
    gchar* raw = g_strndup(line_start, ll);
    for (gchar* c = raw; *c != '\0'; c++) {
        if (*c == '\t' || *c == '\r') *c = ' ';
    }
    if (!g_utf8_validate(raw, -1, NULL)) {
        gchar* valid = g_utf8_make_valid(raw, -1);
        g_free(raw);
        raw = valid;
    }
    return raw;
}

/* Occurrences of query in one text; FALSE when the limit is reached */
static gboolean search_text(const gchar* path, const gchar* text, gsize len, const gchar* query,
                            gboolean exact, GPtrArray* out) {
    gsize n = strlen(query);
    gint line = 0;
    const gchar* line_start = text;
    const gchar* end = text + len;
    gchar first_lo = g_ascii_tolower(query[0]);
    for (const gchar* q = text; q + n <= end; q++) {
        if (*q == '\n') {
            line++;
            line_start = q + 1;
            continue;
        }
        gboolean hit = exact ? (*q == query[0] && memcmp(q, query, n) == 0)
                             : (g_ascii_tolower(*q) == first_lo && g_ascii_strncasecmp(q, query, n) == 0);
        if (!hit) continue;
        if (out->len >= CODE_INDEX_MAX_REFERENCES) return FALSE;
        CodeReference* ref = g_new0(CodeReference, 1);
        ref->path = g_strdup(path);
        ref->line = line;
        ref->column = (gint)(q - line_start);
        ref->text = shown_line(line_start, end);
        g_ptr_array_add(out, ref);
        /* The next occurrence starts after this one (no overlaps) */
        for (const gchar* skip = q + 1; skip < q + n; skip++) {
            if (*skip == '\n') {
                line++;
                line_start = skip + 1;
            }
        }
        q += n - 1;
    }
    return TRUE;
}

static gint compare_paths(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

GPtrArray* text_search_files(const gchar* query, GPtrArray* files, GCancellable* cancellable,
                             gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    if (truncated != NULL) *truncated = FALSE;
    if (query == NULL || query[0] == '\0' || files == NULL) return out;
    gboolean exact = text_search_exact_case(query);
    /* Files in path order: the results come out sorted */
    GPtrArray* sorted = g_ptr_array_copy(files, NULL, NULL);
    g_ptr_array_set_free_func(sorted, NULL); /* the copy inherits it: the paths stay owned by files */
    g_ptr_array_sort(sorted, compare_paths);
    gsize budget = CODE_INDEX_MAX_BYTES;
    for (guint i = 0; i < sorted->len; i++) {
        if (g_cancellable_is_cancelled(cancellable)) break;
        const gchar* path = g_ptr_array_index(sorted, i);
        GStatBuf st;
        if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CODE_INDEX_MAX_FILE_BYTES) continue;
        if ((gsize)st.st_size > budget) {
            if (truncated != NULL) *truncated = TRUE;
            break;
        }
        gchar* text = NULL;
        gsize len = 0;
        if (!g_file_get_contents(path, &text, &len, NULL)) continue;
        budget -= len;
        gboolean go_on = TRUE;
        if (!is_binary(text, len)) {
            go_on = search_text(path, text, len, query, exact, out);
        }
        g_free(text);
        if (!go_on) {
            if (truncated != NULL) *truncated = TRUE;
            break;
        }
    }
    g_ptr_array_unref(sorted);
    return out;
}
