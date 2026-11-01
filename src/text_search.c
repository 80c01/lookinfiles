/* Search the files of a project for a text (see text_search.h) */
#include "text_search.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define BINARY_SNIFF 8192
#define NO_CHAR G_MAXUINT32 /* an invalid byte in fuzzy mode: matches nothing */

/* How one search runs: one per call of text_search_files() (GRegex match
 * data is not shared between threads) */
typedef struct {
    const TextSearchOptions* opts;
    const gchar* query;
    gsize qlen;
    gboolean literal; /* byte comparison (exact case, or an ASCII query) */
    GRegex* utf8_re;  /* for valid UTF-8 texts */
    GRegex* raw_re;   /* for the others (bytes), made when first needed */
    GArray* fuzzy;    /* gunichar: the letters to find, case folded */
    GArray* chars;    /* fuzzy: the current line (gunichar) */
    GArray* offsets;  /* fuzzy: byte offset of each char of the line, then its length */
    gboolean failed;  /* the regular expression gave up on the current text */
} Matcher;

static gboolean is_ascii(const gchar* s) {
    for (; *s != '\0'; s++) {
        if ((guchar)*s >= 0x80) return FALSE;
    }
    return TRUE;
}

static gunichar fold(const Matcher* m, gunichar c) {
    return m->opts->match_case || c == NO_CHAR ? c : g_unichar_tolower(c);
}

static GRegex* compile(const gchar* query, const TextSearchOptions* opts, gboolean raw, GError** error) {
    gchar* escaped = opts->regex ? NULL : g_regex_escape_string(query, -1);
    const gchar* body = opts->regex ? query : escaped;
    gchar* pattern = opts->whole_word ? g_strdup_printf("\\b(?:%s)\\b", body) : g_strdup(body);
    GRegexCompileFlags flags = G_REGEX_MULTILINE | G_REGEX_NEWLINE_ANYCRLF | G_REGEX_OPTIMIZE;
    if (!opts->match_case) flags |= G_REGEX_CASELESS;
    if (raw) flags |= G_REGEX_RAW;
    GRegex* re = g_regex_new(pattern, flags, 0, error);
    g_free(pattern);
    g_free(escaped);
    return re;
}

gchar* text_search_check(const gchar* query, const TextSearchOptions* opts) {
    if (query == NULL || opts == NULL || opts->fuzzy || !opts->regex) return NULL;
    GError* err = NULL;
    GRegex* re = compile(query, opts, FALSE, &err);
    if (re != NULL) {
        g_regex_unref(re);
        return NULL;
    }
    gchar* why = g_strdup(err->message);
    g_error_free(err);
    return why;
}

gchar* text_search_describe(const TextSearchOptions* opts) {
    GString* s = g_string_new(opts != NULL && opts->match_case ? "case sensitive" : "any case");
    if (opts != NULL && opts->fuzzy) {
        g_string_append(s, ", letters in order");
    } else if (opts != NULL) {
        if (opts->whole_word) g_string_append(s, ", whole word");
        if (opts->regex) g_string_append(s, ", regex");
    }
    return g_string_free(s, FALSE);
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

/* Position in a text: the line of `at`, kept while moving forward */
typedef struct {
    const gchar* at;
    const gchar* line_start;
    gint line;
} Cursor;

static void cursor_move(Cursor* c, const gchar* to) {
    const gchar* nl;
    while ((nl = memchr(c->at, '\n', (gsize)(to - c->at))) != NULL) {
        c->line++;
        c->line_start = nl + 1;
        c->at = nl + 1;
    }
    c->at = to;
}

/* A match of len bytes at hit (on the cursor's line, cut at its end); FALSE
 * when the limit is reached */
static gboolean add_ref(GPtrArray* out, const gchar* path, const Cursor* c, const gchar* hit, gsize len,
                        const gchar* end) {
    if (out->len >= TEXT_SEARCH_MAX_RESULTS) return FALSE;
    const gchar* nl = memchr(hit, '\n', MIN(len, (gsize)(end - hit)));
    if (nl != NULL) len = (gsize)(nl - hit);
    CodeReference* ref = g_new0(CodeReference, 1);
    ref->path = g_strdup(path);
    ref->line = c->line;
    ref->column = (gint)(hit - c->line_start);
    ref->length = (gint)len;
    ref->text = shown_line(c->line_start, end);
    g_ptr_array_add(out, ref);
    return TRUE;
}

static gboolean search_literal(Matcher* m, const gchar* path, const gchar* text, gsize len, GPtrArray* out) {
    const gchar* query = m->query;
    gsize n = m->qlen;
    const gchar* end = text + len;
    gboolean exact = m->opts->match_case;
    gchar first_lo = g_ascii_tolower(query[0]);
    Cursor c = {text, text, 0};
    for (const gchar* q = text; q + n <= end; q++) {
        gboolean hit = exact ? (*q == query[0] && memcmp(q, query, n) == 0)
                             : (g_ascii_tolower(*q) == first_lo && g_ascii_strncasecmp(q, query, n) == 0);
        if (!hit) continue;
        cursor_move(&c, q);
        if (!add_ref(out, path, &c, q, n, end)) return FALSE;
        q += n - 1; /* the next occurrence starts after this one (no overlaps) */
    }
    return TRUE;
}

static gboolean search_regex(Matcher* m, const gchar* path, const gchar* text, gsize len, GPtrArray* out) {
    GRegex* re = m->utf8_re;
    if (!g_utf8_validate_len(text, len, NULL)) {
        if (m->raw_re == NULL) m->raw_re = compile(m->query, m->opts, TRUE, NULL);
        re = m->raw_re;
    }
    if (re == NULL) return TRUE;
    gboolean go_on = TRUE;
    Cursor c = {text, text, 0};
    GMatchInfo* info = NULL;
    GError* err = NULL;
    g_regex_match_full(re, text, (gssize)len, 0, 0, &info, &err);
    while (err == NULL && g_match_info_matches(info)) {
        gint s = 0, e = 0;
        if (g_match_info_fetch_pos(info, 0, &s, &e) && e > s) {
            cursor_move(&c, text + s);
            if (!add_ref(out, path, &c, text + s, (gsize)(e - s), text + len)) {
                go_on = FALSE;
                break;
            }
        }
        if (!g_match_info_next(info, &err)) break;
    }
    if (err != NULL) {
        m->failed = TRUE; /* the matches after this point are lost */
        g_error_free(err);
    }
    g_match_info_free(info);
    return go_on;
}

/* Decode a line into m->chars / m->offsets (invalid bytes as NO_CHAR) */
static void decode_line(Matcher* m, const gchar* line, gsize ll) {
    g_array_set_size(m->chars, 0);
    g_array_set_size(m->offsets, 0);
    for (gsize i = 0; i < ll;) {
        gunichar c = g_utf8_get_char_validated(line + i, (gssize)(ll - i));
        gsize step = 1;
        if (c == (gunichar)-1 || c == (gunichar)-2) {
            c = NO_CHAR;
        } else {
            step = (gsize)(g_utf8_next_char(line + i) - (line + i));
        }
        c = fold(m, c);
        guint off = (guint)i;
        g_array_append_val(m->chars, c);
        g_array_append_val(m->offsets, off);
        i += step;
    }
    guint total = (guint)ll;
    g_array_append_val(m->offsets, total);
}

/* The shortest span of the line holding the letters in order (the leftmost
 * of equal ones), in chars [*from, *to); FALSE when there is none */
static gboolean shortest_span(const Matcher* m, guint* from, guint* to) {
    const gunichar* a = (const gunichar*)(gpointer)m->chars->data;
    const gunichar* q = (const gunichar*)(gpointer)m->fuzzy->data;
    const guint* off = (const guint*)(gpointer)m->offsets->data;
    guint n = m->chars->len, k = m->fuzzy->len;
    gboolean found = FALSE;
    for (guint i = 0; i < n;) {
        /* Forward: the first end of a span starting at i or later */
        guint j = 0, e = i;
        for (; e < n && j < k; e++) {
            if (a[e] == q[j]) j++;
        }
        if (j < k) break;
        /* Backward: the latest start for that end */
        guint s = e;
        for (gint r = (gint)k - 1; r >= 0; s--) {
            if (a[s - 1] == q[r]) r--;
        }
        if (!found || off[e] - off[s] < off[*to] - off[*from]) {
            *from = s;
            *to = e;
            found = TRUE;
        }
        i = s + 1;
    }
    return found;
}

static gboolean search_fuzzy(Matcher* m, const gchar* path, const gchar* text, gsize len, GPtrArray* out) {
    const gchar* end = text + len;
    Cursor c = {text, text, 0};
    for (const gchar* line = text; line < end;) {
        const gchar* nl = memchr(line, '\n', (gsize)(end - line));
        gsize ll = nl != NULL ? (gsize)(nl - line) : (gsize)(end - line);
        decode_line(m, line, ll);
        guint from = 0, to = 0;
        if (shortest_span(m, &from, &to)) {
            const guint* off = (const guint*)(gpointer)m->offsets->data;
            cursor_move(&c, line);
            if (!add_ref(out, path, &c, line + off[from], off[to] - off[from], end)) return FALSE;
        }
        if (nl == NULL) break;
        line = nl + 1;
    }
    return TRUE;
}

/* Matches in one text; FALSE when the limit is reached */
static gboolean search_text(Matcher* m, const gchar* path, const gchar* text, gsize len, GPtrArray* out) {
    if (m->fuzzy != NULL) return search_fuzzy(m, path, text, len, out);
    if (m->literal) return search_literal(m, path, text, len, out);
    return search_regex(m, path, text, len, out);
}

/* FALSE when there is nothing to search (an empty query or an invalid
 * regular expression) */
static gboolean matcher_init(Matcher* m, const gchar* query, const TextSearchOptions* opts) {
    memset(m, 0, sizeof(*m));
    m->opts = opts;
    m->query = query;
    m->qlen = strlen(query);
    if (opts->fuzzy) {
        gchar* valid = g_utf8_make_valid(query, -1);
        m->fuzzy = g_array_new(FALSE, FALSE, sizeof(gunichar));
        for (const gchar* p = valid; *p != '\0'; p = g_utf8_next_char(p)) {
            gunichar c = g_utf8_get_char(p);
            if (g_unichar_isspace(c)) continue;
            c = fold(m, c);
            g_array_append_val(m->fuzzy, c);
        }
        g_free(valid);
        m->chars = g_array_new(FALSE, FALSE, sizeof(gunichar));
        m->offsets = g_array_new(FALSE, FALSE, sizeof(guint));
        return m->fuzzy->len > 0;
    }
    if (!opts->regex && !opts->whole_word && (opts->match_case || is_ascii(query))) {
        m->literal = TRUE;
        return TRUE;
    }
    m->utf8_re = compile(query, opts, FALSE, NULL);
    return m->utf8_re != NULL;
}

static void matcher_clear(Matcher* m) {
    if (m->utf8_re != NULL) g_regex_unref(m->utf8_re);
    if (m->raw_re != NULL) g_regex_unref(m->raw_re);
    if (m->fuzzy != NULL) g_array_unref(m->fuzzy);
    if (m->chars != NULL) g_array_unref(m->chars);
    if (m->offsets != NULL) g_array_unref(m->offsets);
}

static gint compare_paths(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

GPtrArray* text_search_files(const gchar* query, const TextSearchOptions* opts, GPtrArray* files,
                             GCancellable* cancellable, TextSearchStats* stats) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    TextSearchStats unused;
    if (stats == NULL) stats = &unused;
    memset(stats, 0, sizeof(*stats));
    if (query == NULL || query[0] == '\0' || opts == NULL || files == NULL) return out;
    Matcher m;
    if (!matcher_init(&m, query, opts)) {
        matcher_clear(&m);
        return out;
    }
    /* Files in path order: the results come out sorted */
    GPtrArray* sorted = g_ptr_array_copy(files, NULL, NULL);
    g_ptr_array_set_free_func(sorted, NULL); /* the copy inherits it: the paths stay owned by files */
    g_ptr_array_sort(sorted, compare_paths);
    gsize budget = TEXT_SEARCH_MAX_BYTES;
    for (guint i = 0; i < sorted->len; i++) {
        if (g_cancellable_is_cancelled(cancellable)) break;
        const gchar* path = g_ptr_array_index(sorted, i);
        GStatBuf st;
        if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size > TEXT_SEARCH_MAX_FILE_BYTES) {
            stats->skipped_large++;
            continue;
        }
        if ((gsize)st.st_size > budget) {
            stats->stop = TEXT_SEARCH_MAX_BYTES_HIT;
            break;
        }
        gchar* text = NULL;
        gsize len = 0;
        if (!g_file_get_contents(path, &text, &len, NULL)) continue;
        budget -= len;
        gboolean go_on = TRUE;
        if (!is_binary(text, len)) {
            m.failed = FALSE;
            go_on = search_text(&m, path, text, len, out);
            stats->searched++;
            if (m.failed) stats->too_complex++;
        }
        g_free(text);
        if (!go_on) {
            stats->stop = TEXT_SEARCH_MAX_RESULTS_HIT;
            break;
        }
    }
    g_ptr_array_unref(sorted);
    matcher_clear(&m);
    return out;
}
