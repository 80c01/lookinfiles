/* Search in the files of a remote machine (see remote_search.h) */
#include "search/remote_search.h"
#include "core/ssh_session.h"
#include "core/vfs.h"

#include <string.h>

/* Folders left out of a listing outside git (as code_index.c) */
static const gchar* const SKIPPED[] = {
    "node_modules", "build", "target", "dist", "out", "__pycache__", "venv", "vendor", "_build",
};

gchar* remote_search_list_command(gboolean ignored) {
    GString* s = g_string_new("if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then ");
    g_string_append(s, ignored ? "git ls-files -z -co" : "git ls-files -z -co --exclude-standard");
    g_string_append(s, "; else find . -type d \\( -name .git");
    if (!ignored) {
        g_string_append(s, " -o -name '.?*'");
        for (gsize i = 0; i < G_N_ELEMENTS(SKIPPED); i++) g_string_append_printf(s, " -o -name %s", SKIPPED[i]);
    }
    g_string_append(s, " \\) -prune -o -type f -print0; fi");
    return g_string_free(s, FALSE);
}

/* The letters of a fuzzy query in order, as an extended regular
 * expression: "abc" -> "a.*b.*c" (spaces left out, as text_search.c) */
static gchar* fuzzy_pattern(const gchar* query) {
    GString* s = g_string_new(NULL);
    gchar* valid = g_utf8_make_valid(query, -1);
    for (const gchar* p = valid; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c)) continue;
        if (s->len > 0) g_string_append(s, ".*");
        if (strchr("\\.[]()*+?{}|^$", (gchar)c) != NULL && c < 0x80) g_string_append_c(s, '\\');
        g_string_append_unichar(s, c);
    }
    g_free(valid);
    return g_string_free(s, FALSE);
}

/* grep over the NUL-separated files of its input, after list (NULL: the
 * input of the command itself) */
static gchar* grep_command(const gchar* list, const gchar* query, const TextSearchOptions* opts, guint max_per_file) {
    /* A superset of the matches: whole words and fuzzy runs are checked here
     * afterwards (text_search.h) */
    gchar* pattern = opts->fuzzy ? fuzzy_pattern(query) : g_strdup(query);
    gchar* quoted = ssh_session_quote(pattern);
    gchar* source = list != NULL ? g_strdup_printf("{ %s; } 2>/dev/null | ", list) : g_strdup("");
    gchar* max = max_per_file > 0 ? g_strdup_printf(" -m %u", max_per_file) : g_strdup("");
    const gchar* kind = opts->fuzzy ? "-E" : opts->regex ? "$R" : "-F";
    gchar* cmd = g_strdup_printf("export LC_ALL=C.UTF-8; R=-E; %s"
                                 "%sxargs -0 grep -nHI --null%s %s%s -e %s -- 2>/dev/null"
                                 " | head -c %d; exit 0",
                                 opts->regex && !opts->fuzzy ? "printf 'a\\n' | grep -qP a 2>/dev/null && R=-P; " : "",
                                 source, max, kind, opts->match_case ? "" : " -i", quoted, REMOTE_SEARCH_MAX_OUTPUT);
    g_free(max);
    g_free(source);
    g_free(quoted);
    g_free(pattern);
    return cmd;
}

gchar* remote_search_grep_command(const gchar* query, const TextSearchOptions* opts, gboolean ignored) {
    gchar* list = remote_search_list_command(ignored);
    gchar* cmd = grep_command(list, query, opts, 0);
    g_free(list);
    return cmd;
}

gchar* remote_search_grep_input_command(const gchar* query, const TextSearchOptions* opts, guint max_per_file) {
    return grep_command(NULL, query, opts, max_per_file);
}

/* rel without a leading "./" */
static const gchar* plain_rel(const gchar* rel) {
    while (rel[0] == '.' && rel[1] == '/') rel += 2;
    return rel;
}

GPtrArray* remote_search_parse_list(const gchar* root, const gchar* data, gsize len, CodeIndexKeepFunc keep,
                                    gpointer keep_data, guint max, gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    if (truncated != NULL) *truncated = FALSE;
    const gchar* end = data + len;
    for (const gchar* p = data; p < end;) {
        const gchar* nul = memchr(p, '\0', (gsize)(end - p));
        if (nul == NULL) break; /* cut short */
        const gchar* rel = plain_rel(p);
        p = nul + 1;
        if (rel[0] == '\0' || (keep != NULL && !keep(rel, keep_data))) continue;
        if (out->len >= max) {
            if (truncated != NULL) *truncated = TRUE;
            break;
        }
        g_ptr_array_add(out, g_build_filename(root, rel, NULL));
    }
    return out;
}

static gint by_place(gconstpointer a, gconstpointer b) {
    const CodeReference* x = *(CodeReference* const*)a;
    const CodeReference* y = *(CodeReference* const*)b;
    gint c = strcmp(x->path, y->path);
    if (c != 0) return c;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    return x->column < y->column ? -1 : x->column > y->column ? 1 : 0;
}

GPtrArray* remote_search_parse_lines(const gchar* root, const gchar* data, gsize len, const gchar* query,
                                     const TextSearchOptions* opts, CodeIndexKeepFunc keep, gpointer keep_data,
                                     TextSearchStats* stats) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    TextSearchStats unused;
    if (stats == NULL) stats = &unused;
    memset(stats, 0, sizeof(*stats));
    TextMatcher* tm = text_matcher_new(query, opts);
    if (tm == NULL) return out;
    gchar* last_rel = NULL; /* the file of the line before, and whether it is kept */
    gchar* last_full = NULL;
    gboolean last_kept = FALSE;
    const gchar* end = data + len;
    for (const gchar* p = data; p < end;) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        if (nl == NULL) break; /* cut short */
        const gchar* nul = memchr(p, '\0', (gsize)(nl - p));
        const gchar* rec = p;
        p = nl + 1;
        if (nul == NULL) continue;
        const gchar* q = nul + 1;
        gint line = 0;
        while (q < nl && g_ascii_isdigit(*q)) line = line * 10 + (*q++ - '0');
        if (q >= nl || *q != ':' || line <= 0) continue;
        q++;
        const gchar* rel = plain_rel(rec);
        if (g_strcmp0(rel, last_rel) != 0) {
            g_free(last_rel);
            g_free(last_full);
            last_rel = g_strdup(rel);
            last_full = g_build_filename(root, rel, NULL);
            last_kept = keep == NULL || keep(rel, keep_data);
            if (last_kept) stats->searched++;
        }
        if (!last_kept) continue;
        if (!text_matcher_search_text(tm, last_full, line - 1, q, (gsize)(nl - q), out)) {
            stats->stop = TEXT_SEARCH_MAX_RESULTS_HIT;
            break;
        }
    }
    g_free(last_rel);
    g_free(last_full);
    text_matcher_free(tm);
    g_ptr_array_sort(out, by_place);
    return out;
}

GPtrArray* remote_search_files(const gchar* root, const gchar* query, const TextSearchOptions* opts, gboolean ignored,
                               CodeIndexKeepFunc keep, gpointer keep_data, GCancellable* cancel,
                               TextSearchStats* stats, gboolean* cut, GError** error) {
    *cut = FALSE;
    gchar* cmd = remote_search_grep_command(query, opts, ignored);
    gint status = 0;
    GBytes* bytes = NULL;
    gboolean ok = vfs_exec(root, cmd, NULL, cancel, &status, &bytes, error);
    g_free(cmd);
    if (!ok) return NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(bytes, &len);
    *cut = len >= REMOTE_SEARCH_MAX_OUTPUT;
    GPtrArray* refs = remote_search_parse_lines(root, data != NULL ? data : "", len, query, opts, keep, keep_data,
                                                stats);
    g_bytes_unref(bytes);
    return refs;
}

GPtrArray* remote_search_all_files(const gchar* root, guint max, GCancellable* cancel, gboolean* truncated,
                                   GError** error) {
    gchar* cmd = remote_search_list_command(FALSE);
    gint status = 0;
    GBytes* bytes = NULL;
    gboolean ok = vfs_exec(root, cmd, NULL, cancel, &status, &bytes, error);
    g_free(cmd);
    if (!ok) return NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(bytes, &len);
    GPtrArray* files = remote_search_parse_list(root, data != NULL ? data : "", len, NULL, NULL, max, truncated);
    g_bytes_unref(bytes);
    return files;
}
