/* Git on a remote machine (see git_remote.h) */
#include "git/git_remote.h"
#include "core/ssh_session.h"
#include "core/vfs.h"
#include "git/git_utils.h"

#include <string.h>

/* dir less the folders of prefix ("a/b/": two), or NULL when it has fewer */
static gchar* strip_prefix(const gchar* dir, const gchar* prefix, gsize len) {
    gchar* root = g_canonicalize_filename(dir, NULL);
    for (gsize i = 0; i < len; i++) {
        if (prefix[i] != '/') continue;
        gchar* up = g_path_get_dirname(root);
        gboolean top = strcmp(up, root) == 0;
        g_free(root);
        root = up;
        if (top) {
            g_free(root);
            return NULL;
        }
    }
    return root;
}

GitState* git_remote_parse(const gchar* dir, const gchar* data, gsize len) {
    const gchar* nl = memchr(data, '\n', len);
    if (nl == NULL || (gsize)(nl - data) + 1 >= len || nl[1] != '\0') return NULL;
    gchar* root = strip_prefix(dir, data, (gsize)(nl - data));
    if (root == NULL) return NULL;
    const gchar* status = nl + 2;
    gchar* branch = NULL;
    gboolean upstream = FALSE;
    guint ahead = 0, behind = 0;
    GHashTable* badges = git_parse_porcelain(root, status, len - (gsize)(status - data), &branch, &upstream, &ahead,
                                             &behind);
    GitState* state = git_state_new(badges, root, upstream, ahead, behind);
    git_state_set_branch(state, branch);
    g_free(branch);
    g_free(root);
    return state;
}

static gint by_kind_and_name(gconstpointer a, gconstpointer b) {
    const GitBranchInfo* x = *(GitBranchInfo* const*)a;
    const GitBranchInfo* y = *(GitBranchInfo* const*)b;
    if (x->is_remote != y->is_remote) return x->is_remote ? 1 : -1;
    return strcmp(x->name, y->name);
}

GPtrArray* git_remote_parse_branches(const gchar* data, gsize len) {
    GPtrArray* out = g_ptr_array_new_with_free_func(git_branch_info_free);
    gchar* text = g_strndup(data, len);
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gchar** l = lines; *l != NULL; l++) {
        const gchar* tab = strchr(*l, '\t');
        if (tab == NULL) continue;
        const gchar* ref = tab + 1;
        gboolean remote = g_str_has_prefix(ref, "refs/remotes/");
        if (!remote && !g_str_has_prefix(ref, "refs/heads/")) continue;
        const gchar* name = ref + strlen(remote ? "refs/remotes/" : "refs/heads/");
        if (name[0] == '\0' || (remote && g_str_has_suffix(name, "/HEAD"))) continue;
        GitBranchInfo* b = g_new0(GitBranchInfo, 1);
        b->name = g_strdup(name);
        b->is_remote = remote;
        b->is_head = (*l)[0] == '*';
        g_ptr_array_add(out, b);
    }
    g_strfreev(lines);
    g_free(text);
    g_ptr_array_sort(out, by_kind_and_name);
    return out;
}

GPtrArray* git_remote_list_branches(const gchar* dir, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(dir, GIT_REMOTE_BRANCHES_COMMAND " 2>/dev/null", NULL, cancel, &status, &out, NULL)) return NULL;
    GPtrArray* branches = NULL;
    if (status == 0) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        branches = git_remote_parse_branches(data != NULL ? data : "", len);
    }
    g_bytes_unref(out);
    return branches;
}

gboolean git_remote_checkout(const gchar* dir, const gchar* branch, gchar** error_msg) {
    *error_msg = NULL;
    /* "origin/x": the local x (git makes it track origin/x when it is new) */
    const gchar* slash = strchr(branch, '/');
    gchar* local = g_strdup(slash != NULL && slash[1] != '\0' ? slash + 1 : branch);
    gchar* q_local = ssh_session_quote(local);
    gchar* q_branch = ssh_session_quote(branch);
    gchar* cmd = slash != NULL
                     ? g_strdup_printf("git checkout %s 2>&1 || git checkout --track %s 2>&1", q_local, q_branch)
                     : g_strdup_printf("git checkout %s 2>&1", q_local);
    gint status = 0;
    GBytes* out = NULL;
    GError* error = NULL;
    gboolean ok = vfs_exec(dir, cmd, NULL, NULL, &status, &out, &error);
    if (!ok) {
        *error_msg = g_strdup(error->message);
        g_error_free(error);
    } else if (status != 0) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        gchar* text = g_utf8_make_valid(data != NULL ? data : "", (gssize)len);
        g_strstrip(text);
        gchar* last = strrchr(text, '\n'); /* git's last word (error: ...) */
        *error_msg = g_strdup(last != NULL ? last + 1 : text);
        g_free(text);
        ok = FALSE;
    }
    if (out != NULL) g_bytes_unref(out);
    g_free(cmd);
    g_free(q_branch);
    g_free(q_local);
    g_free(local);
    return ok;
}

/* The state of the repository dir is in; *outside: dir is in none */
static GitState* scan_one(const gchar* dir, GCancellable* cancel, gboolean* outside) {
    gint status = 0;
    GBytes* out = NULL;
    *outside = FALSE;
    if (!vfs_exec(dir, GIT_REMOTE_STATUS_COMMAND, NULL, cancel, &status, &out, NULL)) return NULL;
    GitState* state = NULL;
    if (status == 0) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        if (data != NULL) state = git_remote_parse(dir, data, len);
    }
    *outside = status == 3;
    g_bytes_unref(out);
    return state;
}

/* Repositories directly below a folder in none, scanned at most (as
 * scan_git_statuses() looks at them) */
#define CHILD_REPOS_MAX 50

GitState* git_remote_scan(const gchar* dir, GCancellable* cancel) {
    gboolean outside = FALSE;
    GitState* state = scan_one(dir, cancel, &outside);
    if (state != NULL || !outside) return state;
    /* In no repository: the badges of the repositories just below, as here */
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(dir, GIT_REMOTE_CHILDREN_COMMAND, NULL, cancel, &status, &out, NULL)) return NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    GPtrArray* children = git_remote_parse_children(dir, data != NULL ? data : "", len, CHILD_REPOS_MAX);
    g_bytes_unref(out);
    GHashTable* all = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint i = 0; i < children->len && !g_cancellable_is_cancelled(cancel); i++) {
        GitState* child = scan_one(children->pdata[i], cancel, &outside);
        GHashTable* badges = git_state_badges(child);
        if (badges != NULL) {
            GHashTableIter it;
            gpointer k, v;
            g_hash_table_iter_init(&it, badges);
            while (g_hash_table_iter_next(&it, &k, &v)) g_hash_table_replace(all, g_strdup(k), g_strdup(v));
        }
        git_state_free(child);
    }
    g_ptr_array_unref(children);
    if (g_hash_table_size(all) == 0) g_clear_pointer(&all, g_hash_table_destroy);
    return git_state_new(all, NULL, FALSE, 0, 0); /* no repository of its own: no branch */
}

GPtrArray* git_remote_parse_children(const gchar* dir, const gchar* data, gsize len, guint max) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    const gchar* end = data + len;
    for (const gchar* p = data; p < end && out->len < max;) {
        const gchar* nul = memchr(p, '\0', (gsize)(end - p));
        if (nul == NULL) break;
        if (nul > p && memchr(p, '/', (gsize)(nul - p)) == NULL) g_ptr_array_add(out, g_build_filename(dir, p, NULL));
        p = nul + 1;
    }
    return out;
}

gchar* git_remote_top(const gchar* dir, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(dir, "git rev-parse --show-prefix 2>/dev/null", NULL, cancel, &status, &out, NULL)) return NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    gchar* top = NULL;
    if (status == 0 && data != NULL) {
        const gchar* nl = memchr(data, '\n', len);
        top = strip_prefix(dir, data, nl != NULL ? (gsize)(nl - data) : len);
    }
    g_bytes_unref(out);
    return top;
}

gboolean git_remote_parse_files(const gchar* data, gsize len, GitKindFunc func, gpointer user_data) {
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GitFileKind kind = GIT_FILE_TRACKED;
    gboolean go_on = TRUE, split = FALSE;
    const gchar* end = data + len;
    for (const gchar* p = data; go_on && p < end;) {
        const gchar* nul = memchr(p, '\0', (gsize)(end - p));
        if (nul == NULL) break; /* cut short */
        const gchar* rel = p;
        p = nul + 1;
        if (strcmp(rel, "\001") == 0) {
            kind = GIT_FILE_UNTRACKED;
            split = TRUE;
        } else if (rel[0] != '\0' && g_hash_table_add(seen, g_strdup(rel))) {
            go_on = func(rel, kind, user_data);
        }
    }
    g_hash_table_destroy(seen);
    return go_on && split;
}

gboolean git_remote_list_files_by_kind(const gchar* top, GitKindFunc func, gpointer user_data, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(top, GIT_REMOTE_FILES_COMMAND, NULL, cancel, &status, &out, NULL)) return FALSE;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    gboolean ok = status == 0 && git_remote_parse_files(data != NULL ? data : "", len, func, user_data);
    g_bytes_unref(out);
    return ok;
}

gchar* git_remote_branch(const gchar* dir, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(dir, "git rev-parse --abbrev-ref HEAD 2>/dev/null", NULL, cancel, &status, &out, NULL)) return NULL;
    gchar* branch = NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    if (status == 0 && data != NULL && len > 0) {
        branch = g_strstrip(g_strndup(data, len));
        if (branch[0] == '\0') g_clear_pointer(&branch, g_free);
    }
    g_bytes_unref(out);
    return branch;
}
