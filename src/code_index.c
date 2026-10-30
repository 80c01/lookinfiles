/* Built-in code index (see code_index.h) */
#include "code_index.h"
#include "code_scan.h"
#include "git_utils.h"
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

static gboolean in_family(const gchar* path, CodeLang family) {
    CodeLang lang = code_lang_from_name(path);
    return lang != CODE_LANG_NONE && code_lang_family(lang) == family;
}

/* Add root/rel when it belongs to family; FALSE when the file limit is hit */
static gboolean add_file(GPtrArray* out, const gchar* root, const gchar* rel, gsize rel_len,
                         CodeLang family, gboolean* truncated) {
    gchar* name = g_strndup(rel, rel_len);
    if (in_family(name, family)) {
        if (out->len >= CODE_INDEX_MAX_FILES) {
            if (truncated != NULL) *truncated = TRUE;
            g_free(name);
            return FALSE;
        }
        g_ptr_array_add(out, g_build_filename(root, name, NULL));
    }
    g_free(name);
    return TRUE;
}

/* `git ls-files`: FALSE when root is not a work tree or git is missing */
static gboolean git_files(const gchar* root, CodeLang family, GPtrArray* out, GCancellable* cancellable,
                          gboolean* truncated) {
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_set_cwd(launcher, root);
    GError* err = NULL;
    GSubprocess* proc = g_subprocess_launcher_spawn(launcher, &err, "git", "ls-files", "-z", "--cached",
                                                    "--others", "--exclude-standard", NULL);
    g_object_unref(launcher);
    if (proc == NULL) {
        g_clear_error(&err);
        return FALSE;
    }
    GBytes* bytes = NULL;
    gboolean ok = g_subprocess_communicate(proc, NULL, cancellable, &bytes, NULL, &err) &&
                  g_subprocess_get_if_exited(proc) && g_subprocess_get_exit_status(proc) == 0;
    g_clear_error(&err);
    g_object_unref(proc);
    if (!ok || bytes == NULL) {
        if (bytes != NULL) g_bytes_unref(bytes);
        return FALSE;
    }
    gsize len = 0;
    const gchar* data = g_bytes_get_data(bytes, &len);
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gsize start = 0;
    for (gsize i = 0; i < len; i++) {
        if (data[i] != '\0') continue;
        if (i > start) {
            /* --cached and --others can list a file twice (merge conflicts) */
            gchar* rel = g_strndup(data + start, i - start);
            if (g_hash_table_add(seen, rel) &&
                !add_file(out, root, data + start, i - start, family, truncated)) {
                break;
            }
        }
        start = i + 1;
    }
    g_hash_table_destroy(seen);
    g_bytes_unref(bytes);
    return TRUE;
}

static gboolean skipped_dir(const gchar* name) {
    static const gchar* const SKIP[] = {
        "node_modules", "build", "target", "dist", "out", "__pycache__", "venv", "vendor", "_build",
    };
    if (name[0] == '.') return TRUE;
    for (gsize i = 0; i < G_N_ELEMENTS(SKIP); i++) {
        if (strcmp(name, SKIP[i]) == 0) return TRUE;
    }
    return FALSE;
}

/* Directory walk; FALSE when the file limit is hit */
static gboolean walk(const gchar* dir, gint depth, CodeLang family, GPtrArray* out,
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
            if (!skipped_dir(name)) go_on = walk(full, depth + 1, family, out, cancellable, truncated);
        } else if (in_family(name, family)) {
            if (out->len >= CODE_INDEX_MAX_FILES) {
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

GPtrArray* code_index_project_files(const gchar* root, CodeLang family, GCancellable* cancellable,
                                    gboolean* truncated) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    if (truncated != NULL) *truncated = FALSE;
    if (root == NULL || family == CODE_LANG_NONE) return out;
    gchar* git_root = find_git_root(root);
    gboolean in_git = git_root != NULL && strcmp(git_root, root) == 0;
    g_free(git_root);
    if (!in_git || !git_files(root, family, out, cancellable, truncated)) {
        g_ptr_array_set_size(out, 0);
        walk(root, 0, family, out, cancellable, truncated);
    }
    return out;
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
    gchar* dir = g_path_get_dirname(path);
    gboolean same_file = q->origin_path != NULL && strcmp(path, q->origin_path) == 0;
    gint locality = same_file ? 0 : (origin_dir != NULL && strcmp(dir, origin_dir) == 0) ? 1 : 2;
    g_free(dir);
    for (guint i = 0; i < syms->len; i++) {
        CodeSymbol* sym = g_ptr_array_index(syms, i);
        CodeMatch* m = g_new0(CodeMatch, 1);
        m->path = g_strdup(path);
        m->symbol = sym;
        Ranked r = {m, sym->declaration == q->declaration ? 0 : 1, locality};
        g_array_append_val(ranked, r);
    }
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
    if (query->origin_path != NULL) scan_file(query, query->origin_path, ranked, origin_dir, &budget);
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
