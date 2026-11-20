/* A filter of a disk scan by git (see git_filter.h) */
#include "git/git_filter.h"
#include "git/git_utils.h"

#include <string.h>

struct GitFilter {
    GitFilterMode mode;
    gboolean top;        /* the root is the top of the work tree */
    gchar* prefix;       /* the root below the top (git_path_prefix()) */
    gsize prefix_len;
    GCancellable* cancel;
    GHashTable* listed;  /* entries listed below the root, '/'-separated (a
                            folder among them: a nested repository) */
    GHashTable* holders; /* folders holding a listed entry, at any depth */
};

void git_filter_free(GitFilter* f) {
    if (f == NULL) return;
    g_free(f->prefix);
    g_hash_table_destroy(f->listed);
    g_hash_table_destroy(f->holders);
    g_free(f);
}

static gboolean on_listed(const gchar* rel, GitFileKind kind, gpointer data) {
    (void)kind;
    GitFilter* f = data;
    if (g_cancellable_is_cancelled(f->cancel)) return FALSE;
    if (f->prefix_len > 0 && strncmp(rel, f->prefix, f->prefix_len) != 0) return TRUE;
    gchar* r = g_strdup(rel + f->prefix_len);
    gsize len = strlen(r);
    while (len > 0 && r[len - 1] == '/') r[--len] = '\0'; /* "repo/": a nested repository */
    if (len == 0) {
        g_free(r);
        return TRUE;
    }
    /* Its folders hold it (the ones above a folder already in are in) */
    gchar* dir = g_strdup(r);
    gchar* slash;
    while ((slash = strrchr(dir, '/')) != NULL) {
        *slash = '\0';
        if (!g_hash_table_add(f->holders, g_strdup(dir))) break;
    }
    g_free(dir);
    g_hash_table_add(f->listed, r);
    return TRUE;
}

GitFilter* git_filter_new(const gchar* root, GitFilterMode mode, GCancellable* cancel) {
    if (root == NULL || mode == GIT_FILTER_NONE) return NULL;
    gchar* canon = g_canonicalize_filename(root, NULL);
    gchar* top = find_git_root(canon);
    if (top == NULL) {
        g_free(canon);
        return NULL;
    }
    GitFilter* f = g_new0(GitFilter, 1);
    f->mode = mode;
    f->top = strcmp(canon, top) == 0;
    f->prefix = git_path_prefix(top, canon);
    f->prefix_len = strlen(f->prefix);
    f->cancel = cancel;
    f->listed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    f->holders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean ok = git_list_files_by_kind(top, FALSE, on_listed, f) && !g_cancellable_is_cancelled(cancel);
    f->cancel = NULL;
    g_free(top);
    g_free(canon);
    if (!ok) {
        git_filter_free(f);
        return NULL;
    }
    return f;
}

DiskFilterVerdict git_filter_check(const gchar* rel, gboolean dir, gpointer filter) {
    GitFilter* f = filter;
#if G_DIR_SEPARATOR != '/'
    gchar* norm = g_strdelimit(g_strdup(rel), G_DIR_SEPARATOR_S, '/');
    rel = norm;
#endif
    gboolean listed = g_hash_table_contains(f->listed, rel);
    gboolean holder = dir && g_hash_table_contains(f->holders, rel);
    gboolean history = dir && f->top && strcmp(rel, ".git") == 0;
#if G_DIR_SEPARATOR != '/'
    g_free(norm);
#endif
    if (f->mode == GIT_FILTER_REPO) {
        if (!dir) return listed ? DISK_FILTER_KEEP : DISK_FILTER_SKIP;
        if (listed) return DISK_FILTER_ALL; /* a nested repository, whole */
        return holder ? DISK_FILTER_KEEP : DISK_FILTER_SKIP;
    }
    /* The ignored files */
    if (!dir) return listed ? DISK_FILTER_SKIP : DISK_FILTER_KEEP;
    if (listed || history) return DISK_FILTER_SKIP;
    return holder ? DISK_FILTER_KEEP : DISK_FILTER_ALL; /* nothing listed below: all ignored */
}

void git_filter_apply(GitFilter* filter, DiskScan* scan) {
    if (filter != NULL) disk_scan_set_filter(scan, git_filter_check, filter);
}
