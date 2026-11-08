/* Built-in code index (see code_index.h) */
#include "code_index.h"
#include "code_scan.h"
#include "git/git_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

gchar* code_index_project_root(const gchar* path) {
    if (path == NULL || path[0] == '\0') return NULL;
    gchar* canon = g_canonicalize_filename(path, NULL);
    gchar* dir = g_file_test(canon, G_FILE_TEST_IS_DIR) ? g_strdup(canon) : g_path_get_dirname(canon);
    g_free(canon);
    gchar* root = find_git_root(dir);
    if (root != NULL) {
        g_free(dir);
        return root;
    }
    return dir;
}

/* CODE_LANG_NONE: every file (code_index_all_files()) */
static gboolean in_family(const gchar* path, CodeLang family) {
    if (family == CODE_LANG_NONE) return TRUE;
    CodeLang lang = code_lang_from_name(path);
    return lang != CODE_LANG_NONE && code_lang_family(lang) == family;
}

/* Which files are listed */
typedef struct {
    CodeLang family;
    gboolean ignored; /* also the files git ignores, hidden and build folders */
    CodeIndexKeepFunc keep;
    gpointer keep_data;
    guint max_files;
} Listing;

static gboolean kept(const Listing* l, const gchar* rel) {
    return in_family(rel, l->family) && (l->keep == NULL || l->keep(rel, l->keep_data));
}

/* Add root/rel when it is kept; FALSE when the file limit is hit */
static gboolean add_file(GPtrArray* out, const gchar* root, const gchar* rel, gsize rel_len,
                         const Listing* l, gboolean* truncated) {
    gchar* name = g_strndup(rel, rel_len);
    if (kept(l, name)) {
        if (out->len >= l->max_files) {
            if (truncated != NULL) *truncated = TRUE;
            g_free(name);
            return FALSE;
        }
        g_ptr_array_add(out, g_build_filename(root, name, NULL));
    }
    g_free(name);
    return TRUE;
}

/* Where git_files() adds the files git_list_files() reports */
typedef struct {
    const gchar* root;
    const Listing* l;
    GPtrArray* out;
    GCancellable* cancellable;
    gboolean* truncated;
} GitFilesTarget;

static gboolean on_git_file(const gchar* rel, gpointer data) {
    GitFilesTarget* t = data;
    if (g_cancellable_is_cancelled(t->cancellable)) return FALSE;
    return add_file(t->out, t->root, rel, strlen(rel), t->l, t->truncated);
}

/* The files of `git ls-files --cached --others`: FALSE when root is not a work tree */
static gboolean git_files(const gchar* root, const Listing* l, GPtrArray* out, GCancellable* cancellable,
                          gboolean* truncated) {
    GitFilesTarget t = {root, l, out, cancellable, truncated};
    return git_list_files(root, l->ignored, on_git_file, &t);
}

static gboolean skipped_dir(const gchar* name, gboolean ignored) {
    static const gchar* const SKIP[] = {
        "node_modules", "build", "target", "dist", "out", "__pycache__", "venv", "vendor", "_build",
    };
    if (strcmp(name, ".git") == 0) return TRUE;
    if (ignored) return FALSE;
    if (name[0] == '.') return TRUE;
    for (gsize i = 0; i < G_N_ELEMENTS(SKIP); i++) {
        if (strcmp(name, SKIP[i]) == 0) return TRUE;
    }
    return FALSE;
}

/* full relative to root (a directory above it) */
static const gchar* relative_to(const gchar* root, const gchar* full) {
    const gchar* rel = full + strlen(root);
    while (*rel == '/' || *rel == '\\') rel++;
    return rel;
}

/* Directory walk under root; FALSE when the file limit is hit */
static gboolean walk(const gchar* root, const gchar* dir, gint depth, const Listing* l, GPtrArray* out,
                     GCancellable* cancellable, gboolean* truncated) {
    if (depth > 12 || g_cancellable_is_cancelled(cancellable)) return TRUE;
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d == NULL) return TRUE;
    gboolean go_on = TRUE;
    const gchar* name;
    while (go_on && (name = g_dir_read_name(d)) != NULL) {
        gchar* full = g_build_filename(dir, name, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_SYMLINK)) {
            /* Symbolic links are not followed (loops) */
        } else if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
            if (!skipped_dir(name, l->ignored)) go_on = walk(root, full, depth + 1, l, out, cancellable, truncated);
        } else if (kept(l, relative_to(root, full))) {
            if (out->len >= l->max_files) {
                if (truncated != NULL) *truncated = TRUE;
                go_on = FALSE;
            } else {
                g_ptr_array_add(out, g_strdup(full));
            }
        }
        g_free(full);
    }
    g_dir_close(d);
    return go_on;
}

/* Files of l->family (CODE_LANG_NONE: all) under root */
static GPtrArray* list_files(const gchar* root, const Listing* l, GCancellable* cancellable, gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    if (truncated != NULL) *truncated = FALSE;
    if (root == NULL) return out;
    gchar* git_root = find_git_root(root);
    gboolean in_git = git_root != NULL && strcmp(git_root, root) == 0;
    g_free(git_root);
    if (!in_git || !git_files(root, l, out, cancellable, truncated)) {
        g_ptr_array_set_size(out, 0);
        walk(root, root, 0, l, out, cancellable, truncated);
    }
    return out;
}

GPtrArray* code_index_project_files(const gchar* root, CodeLang family, GCancellable* cancellable,
                                    gboolean* truncated) {
    if (family == CODE_LANG_NONE) {
        if (truncated != NULL) *truncated = FALSE;
        return g_ptr_array_new_with_free_func(g_free);
    }
    Listing l = {family, FALSE, NULL, NULL, CODE_INDEX_MAX_FILES};
    return list_files(root, &l, cancellable, truncated);
}

GPtrArray* code_index_all_files(const gchar* root, GCancellable* cancellable, gboolean* truncated) {
    return code_index_some_files(root, FALSE, NULL, NULL, CODE_INDEX_MAX_FILES, cancellable, truncated);
}

GPtrArray* code_index_some_files(const gchar* root, gboolean ignored, CodeIndexKeepFunc keep, gpointer keep_data,
                                 guint max_files, GCancellable* cancellable, gboolean* truncated) {
    Listing l = {CODE_LANG_NONE, ignored, keep, keep_data, max_files};
    return list_files(root, &l, cancellable, truncated);
}

void code_match_free(CodeMatch* match) {
    if (match == NULL) return;
    g_free(match->path);
    code_symbol_free(match->symbol);
    g_free(match);
}

typedef struct {
    CodeMatch* match;
    gint tier;      /* 0: the kind wanted, 1: the other one */
    gint locality;  /* 0: origin file, 1: its directory, 2: elsewhere */
} Ranked;

static gint compare_ranked(gconstpointer a, gconstpointer b) {
    const Ranked* x = a;
    const Ranked* y = b;
    if (x->tier != y->tier) return x->tier - y->tier;
    if (x->locality != y->locality) return x->locality - y->locality;
    gint c = strcmp(x->match->path, y->match->path);
    if (c != 0) return c;
    return x->match->symbol->line - y->match->symbol->line;
}

/* Scan one file (or its open buffer) for the name; FALSE when the byte
 * budget is spent */
static gint locality_of(const CodeQuery* q, const gchar* path, const gchar* origin_dir) {
    if (q->origin_path != NULL && strcmp(path, q->origin_path) == 0) return 0;
    gchar* dir = g_path_get_dirname(path);
    gint locality = origin_dir != NULL && strcmp(dir, origin_dir) == 0 ? 1 : 2;
    g_free(dir);
    return locality;
}

static void add_ranked(const CodeQuery* q, GArray* ranked, const gchar* path, CodeSymbol* sym, gint locality) {
    CodeMatch* m = g_new0(CodeMatch, 1);
    m->path = g_strdup(path);
    m->symbol = sym;
    Ranked r = {m, sym->declaration == q->declaration ? 0 : 1, locality};
    g_array_append_val(ranked, r);
}

static gboolean scan_file(const CodeQuery* q, const gchar* path, GArray* ranked, const gchar* origin_dir,
                          gsize* budget) {
    const gchar* text = q->texts != NULL ? g_hash_table_lookup(q->texts, path) : NULL;
    gchar* owned = NULL;
    gsize len = 0;
    if (text != NULL) {
        len = strlen(text);
    } else {
        GStatBuf st;
        if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CODE_INDEX_MAX_FILE_BYTES) {
            return TRUE;
        }
        if ((gsize)st.st_size > *budget) return FALSE;
        if (!g_file_get_contents(path, &owned, &len, NULL)) return TRUE;
        text = owned;
    }
    *budget -= MIN(*budget, len);
    CodeLang lang = code_lang_from_name(path);
    if (lang == CODE_LANG_NONE) lang = q->lang;
    GPtrArray* syms = code_scan_text(text, len, lang, q->name);
    gint locality = locality_of(q, path, origin_dir);
    for (guint i = 0; i < syms->len; i++) add_ranked(q, ranked, path, g_ptr_array_index(syms, i), locality);
    /* The symbols now belong to the matches */
    g_ptr_array_set_free_func(syms, NULL);
    g_ptr_array_free(syms, TRUE);
    g_free(owned);
    return TRUE;
}

GPtrArray* code_index_find(const CodeQuery* query, GPtrArray* files, GCancellable* cancellable,
                           gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_match_free);
    if (truncated != NULL) *truncated = FALSE;
    if (query == NULL || query->name == NULL || query->name[0] == '\0') return out;
    GArray* ranked = g_array_new(FALSE, FALSE, sizeof(Ranked));
    gchar* origin_dir = query->origin_path != NULL ? g_path_get_dirname(query->origin_path) : NULL;
    gsize budget = CODE_INDEX_MAX_BYTES;
    if (query->tags != NULL && files != NULL && code_tags_index_update(query->tags, files, cancellable)) {
        /* ctags: indexed files from the index, open buffers and the rest scanned */
        GHashTable* listed = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint i = 0; i < files->len; i++) g_hash_table_add(listed, g_ptr_array_index(files, i));
        GPtrArray* hits = code_tags_index_lookup(query->tags, query->name);
        for (guint i = 0; i < hits->len; i++) {
            CodeTagsHit* h = g_ptr_array_index(hits, i);
            if (query->texts != NULL && g_hash_table_contains(query->texts, h->path)) continue;
            add_ranked(query, ranked, h->path, code_symbol_copy(h->symbol), locality_of(query, h->path, origin_dir));
        }
        g_ptr_array_unref(hits);
        if (query->texts != NULL) {
            GHashTableIter it;
            gpointer key, value;
            g_hash_table_iter_init(&it, query->texts);
            while (g_hash_table_iter_next(&it, &key, &value)) {
                gboolean origin = query->origin_path != NULL && strcmp(key, query->origin_path) == 0;
                if (origin || g_hash_table_contains(listed, key)) scan_file(query, key, ranked, origin_dir, &budget);
            }
        }
        if (query->origin_path != NULL && !g_hash_table_contains(listed, query->origin_path) &&
            (query->texts == NULL || !g_hash_table_contains(query->texts, query->origin_path))) {
            scan_file(query, query->origin_path, ranked, origin_dir, &budget);
        }
        g_hash_table_destroy(listed);
        files = NULL; /* nothing more to scan */
    } else if (query->origin_path != NULL) {
        scan_file(query, query->origin_path, ranked, origin_dir, &budget);
    }
    for (guint i = 0; files != NULL && i < files->len; i++) {
        if (g_cancellable_is_cancelled(cancellable)) break;
        const gchar* path = g_ptr_array_index(files, i);
        if (query->origin_path != NULL && strcmp(path, query->origin_path) == 0) continue;
        if (!scan_file(query, path, ranked, origin_dir, &budget)) {
            if (truncated != NULL) *truncated = TRUE;
            break;
        }
    }
    g_array_sort(ranked, compare_ranked);
    for (guint i = 0; i < ranked->len; i++) {
        g_ptr_array_add(out, g_array_index(ranked, Ranked, i).match);
    }
    g_array_free(ranked, TRUE);
    g_free(origin_dir);
    return out;
}

/* ------------------------------------------------------------------------- */
/* References                                                                */
/* ------------------------------------------------------------------------- */

void code_reference_free(CodeReference* ref) {
    if (ref == NULL) return;
    g_free(ref->path);
    g_free(ref->text);
    g_free(ref);
}

static gboolean word_byte(CodeLang lang, guchar b) {
    return b >= 0x80 || code_is_word_char(lang, b);
}

/* Occurrences of name in one text; FALSE when the limit is reached */
static gboolean references_in(const gchar* path, const gchar* text, gsize len, const gchar* name, CodeLang lang,
                              GPtrArray* out) {
    gsize n = strlen(name);
    gint line = 0;
    const gchar* line_start = text;
    const gchar* end = text + len;
    const gchar* p = text;
    while (p + n <= end) {
        const gchar* hit = NULL;
        for (const gchar* q = p; q + n <= end; q++) {
            if (*q == '\n') {
                line++;
                line_start = q + 1;
                continue;
            }
            if (*q == name[0] && memcmp(q, name, n) == 0 && (q == text || !word_byte(lang, (guchar)q[-1])) &&
                (q + n == end || !word_byte(lang, (guchar)q[n]))) {
                hit = q;
                break;
            }
        }
        if (hit == NULL) break;
        if (out->len >= CODE_INDEX_MAX_REFERENCES) return FALSE;
        const gchar* nl = memchr(line_start, '\n', (gsize)(end - line_start));
        gsize ll = nl != NULL ? (gsize)(nl - line_start) : (gsize)(end - line_start);
        CodeReference* ref = g_new0(CodeReference, 1);
        ref->path = g_strdup(path);
        ref->line = line;
        ref->column = (gint)(hit - line_start);
        /* Untrimmed text (columns must stay right): tabs and CR as spaces */
        gchar* raw = g_strndup(line_start, ll);
        for (gchar* c = raw; *c != '\0'; c++) {
            if (*c == '\t' || *c == '\r') *c = ' ';
        }
        if (!g_utf8_validate(raw, -1, NULL)) {
            gchar* valid = g_utf8_make_valid(raw, -1);
            g_free(raw);
            raw = valid;
        }
        ref->text = raw;
        g_ptr_array_add(out, ref);
        p = hit + n;
    }
    return TRUE;
}

static gint compare_refs(gconstpointer a, gconstpointer b, gpointer origin) {
    const CodeReference* x = *(CodeReference* const*)a;
    const CodeReference* y = *(CodeReference* const*)b;
    gboolean xo = origin != NULL && strcmp(x->path, origin) == 0;
    gboolean yo = origin != NULL && strcmp(y->path, origin) == 0;
    if (xo != yo) return xo ? -1 : 1;
    gint c = strcmp(x->path, y->path);
    if (c != 0) return c;
    if (x->line != y->line) return x->line - y->line;
    return x->column - y->column;
}

GPtrArray* code_index_references(const gchar* name, CodeLang lang, const gchar* origin_path, GHashTable* texts,
                                 GPtrArray* files, GCancellable* cancellable, gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    if (truncated != NULL) *truncated = FALSE;
    if (name == NULL || name[0] == '\0') return out;
    GPtrArray* all = g_ptr_array_new();
    if (origin_path != NULL) g_ptr_array_add(all, (gpointer)origin_path);
    for (guint i = 0; files != NULL && i < files->len; i++) {
        const gchar* f = g_ptr_array_index(files, i);
        if (origin_path == NULL || strcmp(f, origin_path) != 0) g_ptr_array_add(all, (gpointer)f);
    }
    gsize budget = CODE_INDEX_MAX_BYTES;
    for (guint i = 0; i < all->len; i++) {
        if (g_cancellable_is_cancelled(cancellable)) break;
        const gchar* path = g_ptr_array_index(all, i);
        const gchar* text = texts != NULL ? g_hash_table_lookup(texts, path) : NULL;
        gchar* owned = NULL;
        gsize len = 0;
        if (text != NULL) {
            len = strlen(text);
        } else {
            GStatBuf st;
            if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CODE_INDEX_MAX_FILE_BYTES) continue;
            if ((gsize)st.st_size > budget) {
                if (truncated != NULL) *truncated = TRUE;
                break;
            }
            if (!g_file_get_contents(path, &owned, &len, NULL)) continue;
            text = owned;
        }
        budget -= MIN(budget, len);
        CodeLang file_lang = code_lang_from_name(path);
        gboolean go_on = !code_text_has_word(text, len, name, file_lang != CODE_LANG_NONE ? file_lang : lang) ||
                         references_in(path, text, len, name, file_lang != CODE_LANG_NONE ? file_lang : lang, out);
        g_free(owned);
        if (!go_on) {
            if (truncated != NULL) *truncated = TRUE;
            break;
        }
    }
    g_ptr_array_free(all, TRUE);
    g_ptr_array_sort_with_data(out, compare_refs, (gpointer)origin_path);
    return out;
}
