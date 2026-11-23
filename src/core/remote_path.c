/* Paths of connected remote machines as "<machine>:<remote path>" (see
 * remote_path.h). */
#include "core/remote_path.h"
#include "explorer/remote_store.h"
#include "core/app_events.h"
#include "core/vfs.h"

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

/* A local path as compared with a mount: on Windows '\\' is a separator too
 * (g_build_filename() makes them), so it becomes '/', and what follows the
 * mount is then the remote path, with '/' (as vfs.c does). Caller frees. */
static gchar* sep_key(const gchar* path) {
    gchar* p = g_strdup(path);
#ifdef G_OS_WIN32
    g_strdelimit(p, "\\", '/');
#endif
    return p;
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

static gchar* map_to_display(const gchar* path, const gchar* mount, const gchar* name, const gchar* root);

gchar* remote_path_map_to_display(const gchar* path, const gchar* mount, const gchar* name,
                                  const gchar* root) {
    if (path == NULL || mount == NULL || mount[0] == '\0' || name == NULL) return NULL;
    gchar* p = sep_key(path);
    gchar* m = sep_key(mount);
    gchar* out = map_to_display(p, m, name, root);
    g_free(p);
    g_free(m);
    return out;
}

/* remote_path_map_to_display() on paths with '/' only */
static gchar* map_to_display(const gchar* path, const gchar* mount, const gchar* name, const gchar* root) {
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
        gchar* local_key = sep_key(local);
        gchar* m_key = sep_key(m);
        if (under_mount(local_key, m_key, mount_len(m_key))) {
            *out_local = local;
        } else {
            g_free(local); /* "..": out of the mounted folder */
        }
        g_free(local_key);
        g_free(m_key);
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

/* The connected machine path is on, at its mount or below */
static RemoteMachine* machine_of(AppData* app, const gchar* path, gboolean* is_root) {
    if (path == NULL) return NULL;
    gchar* p = sep_key(path);
    RemoteMachine* found = NULL;
    for (GList* l = remote_machine_list(app); l != NULL && found == NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        if (!rm->is_connected || rm->mount_point == NULL) continue;
        gchar* m = sep_key(rm->mount_point);
        gsize n = mount_len(m);
        if (under_mount(p, m, n)) {
            const gchar* rest = p + n;
            while (*rest == '/') rest++;
            if (is_root != NULL) *is_root = rest[0] == '\0';
            found = rm;
        }
        g_free(m);
    }
    g_free(p);
    return found;
}

gchar* remote_path_display_name(AppData* app, const gchar* path) {
    if (path == NULL) return NULL;
    gboolean root = FALSE;
    RemoteMachine* rm = machine_of(app, path, &root);
    if (rm != NULL && root) return g_strdup(machine_label(rm));
    return g_path_get_basename(path);
}

gchar* remote_path_display_parent(AppData* app, const gchar* path) {
    gboolean root = FALSE;
    if (machine_of(app, path, &root) == NULL) return NULL;
    if (root) return g_strdup("");
    gchar* shown = remote_path_display(app, path);
    gchar* slash = strrchr(shown, '/');
    gchar* colon = strchr(shown, ':');
    if (slash != NULL && colon != NULL && slash > colon) {
        if (slash == colon + 1) slash++; /* "server:/etc" -> "server:/" */
        *slash = '\0';
    } else if (colon != NULL) {
        colon[1] = '\0'; /* "server:~" -> "server:" */
    }
    return shown;
}

gboolean remote_path_locate(AppData* app, const gchar* path, SshTarget* target, gchar** remote,
                            gchar** machine) {
    if (remote != NULL) *remote = NULL;
    if (machine != NULL) *machine = NULL;
    if (path == NULL) return FALSE;
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        if (!rm->is_connected || rm->mount_point == NULL || rm->host == NULL) continue;
        const gchar* label = machine_label(rm);
        gchar* shown = remote_path_map_to_display(path, rm->mount_point, label, rm->remote_path);
        if (shown == NULL) continue;
        if (target != NULL) *target = remote_machine_target(rm);
        if (remote != NULL) *remote = g_strdup(shown + strlen(label) + 1); /* after "<name>:" */
        if (machine != NULL) *machine = g_strdup(label);
        g_free(shown);
        return TRUE;
    }
    return FALSE;
}

gboolean remote_path_active(AppData* app, SshTarget* target, gchar** machine) {
    if (machine != NULL) *machine = NULL;
    RemoteMachine* rm = remote_get_active_machine(app);
    if (rm == NULL || !rm->is_connected || rm->host == NULL || rm->host[0] == '\0') return FALSE;
    if (target != NULL) *target = remote_machine_target(rm);
    if (machine != NULL) *machine = g_strdup(machine_label(rm));
    return TRUE;
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

gboolean remote_path_refuse(AppData* app, const gchar* path, const gchar* message) {
    if (path == NULL || !vfs_is_remote(path)) return FALSE;
    app_notice(app, message, FALSE);
    return TRUE;
}
