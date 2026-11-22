/* Git on a remote machine (see git_remote.h) */
#include "git/git_remote.h"
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
