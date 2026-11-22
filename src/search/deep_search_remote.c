/* The pieces of a deep search on a remote machine (see deep_search_remote.h) */
#include "search/deep_search_remote.h"
#include "code/code_index.h"

#include <string.h>

gchar* deep_remote_find_command(const DeepQuery* q) {
    /* First a record of the folder itself ('r'): it could be read */
    GString* s = g_string_new("printf 'r 0 0 .\\0'; find \"$PWD\" -mindepth 1");
    if (q->max_depth > 0) g_string_append_printf(s, " -maxdepth %d", q->max_depth);
    if (q->one_filesystem) g_string_append(s, " -xdev");
    g_string_append(s, " \\( -path /proc -o -path /sys -o -path /dev");
    if (!q->hidden) g_string_append(s, " -o -name '.*'");
    g_string_append(s, " \\) -prune -o -printf '%y %s %T@ %P\\0' 2>/dev/null; exit 0");
    return g_string_free(s, FALSE);
}

gboolean deep_remote_parse_entry(const gchar* record, DeepRemoteEntry* e) {
    memset(e, 0, sizeof(*e));
    if (record == NULL || record[0] == '\0' || record[1] != ' ') return FALSE;
    e->type = record[0];
    gchar* end = NULL;
    e->size = g_ascii_strtoull(record + 2, &end, 10);
    if (end == NULL || *end != ' ') return FALSE;
    e->mtime = g_ascii_strtoll(end + 1, &end, 10); /* "1700000000.123": the seconds */
    if (end == NULL) return FALSE;
    end = strchr(end, ' ');
    if (end == NULL || end[1] == '\0') return FALSE;
    e->rel = end + 1;
    return TRUE;
}

void deep_remote_match_free(gpointer data) {
    DeepRemoteMatch* m = data;
    if (m == NULL) return;
    g_free(m->text);
    g_free(m);
}

GHashTable* deep_remote_parse_matches(const gchar* data, gsize len, const gchar* query, const TextSearchOptions* opts,
                                      guint max_count) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, deep_remote_match_free);
    TextMatcher* tm = text_matcher_new(query, opts);
    if (tm == NULL) return out;
    GPtrArray* refs = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    const gchar* end = data + len;
    for (const gchar* p = data; p < end;) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        if (nl == NULL) break; /* cut short */
        const gchar* nul = memchr(p, '\0', (gsize)(nl - p));
        const gchar* path = p;
        p = nl + 1;
        if (nul == NULL) continue;
        const gchar* q = nul + 1;
        gint line = 0;
        while (q < nl && g_ascii_isdigit(*q)) line = line * 10 + (*q++ - '0');
        if (q >= nl || *q != ':' || line <= 0) continue;
        q++;
        DeepRemoteMatch* m = g_hash_table_lookup(out, path);
        if (m != NULL && m->more) continue;
        g_ptr_array_set_size(refs, 0);
        text_matcher_search_text(tm, path, line - 1, q, (gsize)(nl - q), refs);
        if (refs->len == 0) continue;
        if (m == NULL) {
            const CodeReference* first = refs->pdata[0];
            m = g_new0(DeepRemoteMatch, 1);
            m->line = first->line;
            m->column = first->column;
            m->length = first->length;
            m->text = g_strdup(first->text);
            g_hash_table_insert(out, g_strdup(path), m);
        }
        m->count += refs->len;
        if (m->count >= max_count) {
            m->count = max_count;
            m->more = TRUE;
        }
    }
    g_ptr_array_unref(refs);
    text_matcher_free(tm);
    return out;
}
