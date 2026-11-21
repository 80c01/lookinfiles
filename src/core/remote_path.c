/* Paths of connected remote machines as "<machine>:<remote path>" (see
 * remote_path.h). */
#include "core/remote_path.h"
#include "explorer/remote_store.h"

#include <string.h>

/* The remote folder mounted, without a trailing '/' ("~" for the home
 * directory, "/" for the root). Caller frees. */
static gchar* norm_root(const gchar* root) {
    if (root == NULL || root[0] == '\0' || g_strcmp0(root, "~") == 0 || g_strcmp0(root, "~/") == 0) {
        return g_strdup("~");
    }
    gchar* r = g_strdup(root);
    gsize n = strlen(r);
    while (n > 1 && r[n - 1] == '/') r[--n] = '\0';
    return r;
}

/* Length of mount without its trailing '/' */
static gsize mount_len(const gchar* mount) {
    gsize n = strlen(mount);
    while (n > 1 && mount[n - 1] == '/') n--;
    return n;
}

/* path is mount (n bytes) or under it */
static gboolean under_mount(const gchar* path, const gchar* mount, gsize n) {
    return strncmp(path, mount, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

gchar* remote_path_map_to_display(const gchar* path, const gchar* mount, const gchar* name,
                                  const gchar* root) {
    if (path == NULL || mount == NULL || mount[0] == '\0' || name == NULL) return NULL;
    gsize n = mount_len(mount);
    if (!under_mount(path, mount, n)) return NULL;
    const gchar* rest = path + n;
    while (rest[0] == '/' && rest[1] == '/') rest++;
    gchar* r = norm_root(root);
    gchar* out;
    if (rest[0] == '\0' || g_strcmp0(rest, "/") == 0) {
        out = g_strconcat(name, ":", r, NULL);
    } else if (g_str_has_suffix(r, "/")) {
        out = g_strconcat(name, ":", r, rest + 1, NULL);
    } else {
        out = g_strconcat(name, ":", r, rest, NULL);
    }
    g_free(r);
    return out;
}

gboolean remote_path_map_from_display(const gchar* text, const gchar* mount, const gchar* name,
                                      const gchar* root, gchar** out_local) {
    if (out_local != NULL) *out_local = NULL;
    if (text == NULL || mount == NULL || mount[0] == '\0' || name == NULL || name[0] == '\0') return FALSE;
    gchar* t = g_strstrip(g_strdup(text));
    gsize name_len = strlen(name);
    if (strncmp(t, name, name_len) != 0 || t[name_len] != ':') {
        g_free(t);
        return FALSE;
    }
    const gchar* after = t + name_len + 1;
    gchar* r = norm_root(root);
    gsize r_len = strlen(r);
    const gchar* rel = NULL; /* relative to the mounted folder */
    if (after[0] == '\0' || g_strcmp0(after, r) == 0) {
        rel = "";
    } else if (g_strcmp0(r, "/") == 0 && after[0] == '/') {
        rel = after + 1;
    } else if (strncmp(after, r, r_len) == 0 && after[r_len] == '/') {
        rel = after + r_len + 1;
    } else if (after[0] != '/' && after[0] != '~') {
        rel = after;
    }
    if (rel != NULL && out_local != NULL) {
        gchar* m = g_strndup(mount, mount_len(mount));
        gchar* local = rel[0] == '\0' ? g_strdup(m) : g_canonicalize_filename(rel, m);
        if (under_mount(local, m, strlen(m))) {
            *out_local = local;
        } else {
            g_free(local); /* "..": out of the mounted folder */
        }
        g_free(m);
    }
    g_free(r);
    g_free(t);
    return TRUE;
}

/* Name a machine is written with */
static const gchar* machine_label(const RemoteMachine* rm) {
    return (rm->name != NULL && rm->name[0] != '\0') ? rm->name : rm->host;
}

gchar* remote_path_display(AppData* app, const gchar* path) {
    if (path == NULL) return NULL;
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        if (!rm->is_connected || rm->mount_point == NULL) continue;
        gchar* shown = remote_path_map_to_display(path, rm->mount_point, machine_label(rm), rm->remote_path);
        if (shown != NULL) return shown;
    }
    return g_strdup(path);
}

gboolean remote_path_parse(AppData* app, const gchar* text, gchar** out_local) {
    if (out_local != NULL) *out_local = NULL;
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        if (!rm->is_connected || rm->mount_point == NULL) continue;
        if (remote_path_map_from_display(text, rm->mount_point, machine_label(rm), rm->remote_path, out_local)) {
            return TRUE;
        }
    }
    return FALSE;
}
