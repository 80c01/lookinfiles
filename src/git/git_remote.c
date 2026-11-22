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
    if (!vfs_exec(dir, GIT_REMOTE_BRANCHES_COMMAND " 2>/dev/null", cancel, &status, &out, NULL)) return NULL;
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
    gboolean ok = vfs_exec(dir, cmd, NULL, &status, &out, &error);
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

GitState* git_remote_scan(const gchar* dir, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(dir, GIT_REMOTE_STATUS_COMMAND, cancel, &status, &out, NULL)) return NULL;
    GitState* state = NULL;
    if (status == 0) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        if (data != NULL) state = git_remote_parse(dir, data, len);
    }
    g_bytes_unref(out);
    return state;
}
