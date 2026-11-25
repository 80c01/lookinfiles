#include "explorer/local_machine.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/remote_path.h"
#include "explorer/explorer_tree.h"
#include "explorer/file_list.h"
#include "explorer/tree_nav.h"
#include "terminal/terminal.h"
#include "theme/palette.h"

#include <string.h>

/* The disks listed by the last local_machine_append_row(), the volume
 * monitor whose changes redraw the view, and what is collapsed (for the
 * session: both start expanded) */
static struct {
    AppData* app;
    GVolumeMonitor* monitor;
    GPtrArray* disks;
    gboolean machine_collapsed;
    gboolean disks_collapsed;
    guint changed_idle;
} lm;

static gboolean on_changed_idle(gpointer data) {
    (void)data;
    lm.changed_idle = 0;
    if (lm.app != NULL && lm.app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        app_emit(lm.app, APP_EVENT_FILES_CHANGED); /* rebuilds the Machines view */
    }
    return G_SOURCE_REMOVE;
}

/* A disk came, went or changed: redraw once the burst of signals is over */
static void on_monitor_changed(GVolumeMonitor* monitor, GObject* object, gpointer data) {
    (void)monitor;
    (void)object;
    (void)data;
    if (lm.changed_idle == 0) {
        lm.changed_idle = g_timeout_add(200, on_changed_idle, NULL);
    }
}

static void ensure_monitor(AppData* app) {
    lm.app = app;
    if (lm.monitor != NULL) {
        return;
    }
    lm.monitor = g_volume_monitor_get();
    static const gchar* const signals[] = {"mount-added", "mount-removed", "mount-changed",
                                           "volume-added", "volume-removed", "volume-changed"};
    for (gsize i = 0; i < G_N_ELEMENTS(signals); i++) {
        g_signal_connect(lm.monitor, signals[i], G_CALLBACK(on_monitor_changed), NULL);
    }
}

static void append_disk_row(ExplorerRow* parent, const DiskVolume* v) {
    ExplorerRow* row = explorer_row_append(parent, LOCAL_DISK_ROW, v->name, v->name, v->id);
    explorer_row_set_glyph(row, disk_volume_glyph(v->kind));
    gchar* badge = NULL;
    if (v->path == NULL) {
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_FAINT),
                                        _("not mounted"));
    } else if (g_strcmp0(v->path, v->name) != 0) {
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_SUBTLE),
                                        v->path);
    }
    if (badge != NULL) {
        explorer_row_set_badge(row, badge);
        g_free(badge);
    }
}

ExplorerRow* local_machine_append_row(ExplorerRow* parent, AppData* app) {
    if (parent == NULL) {
        return NULL;
    }
    ensure_monitor(app);
    g_clear_pointer(&lm.disks, g_ptr_array_unref);
    lm.disks = disk_volumes_list(lm.monitor);

    const gchar* host = g_get_host_name();
    ExplorerRow* row = explorer_row_append_container(parent, LOCAL_MACHINE_ROW, host, host, "");
    explorer_row_set_glyph(row, "computer");
    gchar* badge = NULL;
    if (local_machine_is_current(app)) {
        badge = g_markup_printf_escaped("<span foreground=\"%s\" weight=\"bold\">● %s</span>  <span foreground=\"%s\">(%s)</span>",
                                        palette_color(PAL_SUCCESS), _("This computer"),
                                        palette_color(PAL_FG_SUBTLE), g_get_user_name());
    } else {
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s (%s)</span>", palette_color(PAL_FG_SUBTLE),
                                        _("This computer"), g_get_user_name());
    }
    explorer_row_set_badge(row, badge);
    g_free(badge);

    ExplorerRow* disks = explorer_row_append_container(row, LOCAL_DISKS_ROW, _("Disks"), _("Disks"), "");
    explorer_row_set_glyph(disks, "root");
    gchar* count = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), lm.disks->len);
    explorer_row_set_badge(disks, count);
    g_free(count);
    for (guint i = 0; i < lm.disks->len; i++) {
        append_disk_row(disks, g_ptr_array_index(lm.disks, i));
    }
    return row;
}

void local_machine_restore_expanded(AppData* app, ExplorerRow* root) {
    if (app == NULL || app->tree == NULL || root == NULL) {
        return;
    }
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* machine = explorer_row_child(root, i);
        if (!explorer_row_is(machine, LOCAL_MACHINE_ROW) || lm.machine_collapsed) {
            continue;
        }
        explorer_tree_set_expanded(app->tree, machine, TRUE);
        for (guint j = 0; j < explorer_row_n_children(machine); j++) {
            ExplorerRow* group = explorer_row_child(machine, j);
            if (explorer_row_is(group, LOCAL_DISKS_ROW) && !lm.disks_collapsed) {
                explorer_tree_set_expanded(app->tree, group, TRUE);
            }
        }
    }
}

gboolean local_machine_row_toggled(ExplorerRow* row, gboolean expanded) {
    if (explorer_row_is(row, LOCAL_MACHINE_ROW)) {
        lm.machine_collapsed = !expanded;
        return TRUE;
    }
    if (explorer_row_is(row, LOCAL_DISKS_ROW)) {
        lm.disks_collapsed = !expanded;
        return TRUE;
    }
    return FALSE;
}

gboolean local_machine_any_expanded(void) {
    return !lm.machine_collapsed;
}

void local_machine_set_expanded(gboolean expanded) {
    lm.machine_collapsed = !expanded;
    lm.disks_collapsed = !expanded;
}

gboolean local_machine_selected(AppData* app) {
    return app != NULL && explorer_row_is(tree_nav_selected(app), LOCAL_MACHINE_ROW);
}

gboolean local_machine_owns_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, LOCAL_MACHINE_ROW) || explorer_row_is(row, LOCAL_DISKS_ROW) ||
           explorer_row_is(row, LOCAL_DISK_ROW);
}

const DiskVolume* local_machine_disk(const gchar* id) {
    return disk_volumes_find(lm.disks, id);
}

const DiskVolume* local_machine_selected_disk(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, LOCAL_DISK_ROW) ? local_machine_disk(row->path) : NULL;
}

gboolean local_machine_is_current(AppData* app) {
    return app == NULL || app->current_remote_id == NULL;
}

void local_machine_make_current(AppData* app) {
    if (app == NULL) {
        return;
    }
    g_clear_pointer(&app->current_remote_id, g_free);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        g_clear_pointer(&app->workspaces[app->current_ws].remote_id, g_free);
    }
}

/* The folder shown when it is local, else the home folder. Caller frees. */
static gchar* local_folder(AppData* app) {
    if (app->base_dir != NULL && !remote_path_locate(app, app->base_dir, NULL, NULL, NULL)) {
        return g_strdup(app->base_dir);
    }
    return g_strdup(g_get_home_dir());
}

void local_machine_open_dir(AppData* app, const gchar* dir) {
    if (app == NULL || dir == NULL) {
        return;
    }
    local_machine_make_current(app);
    explorer_set_view(app, SIDEBAR_VIEW_FILES);
    reload_directory(app, dir);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void local_machine_open(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* dir = local_folder(app);
    local_machine_open_dir(app, dir);
    g_free(dir);
}

static void open_terminal_in(AppData* app, const gchar* dir) {
    local_machine_make_current(app);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    terminal_open(app, FALSE, dir);
}

void local_machine_open_terminal(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* dir = local_folder(app);
    open_terminal_in(app, dir);
    g_free(dir);
}

/* ------------------------------------------------------------------------- */
/* Disks: open, mount, unmount, eject                                        */
/* ------------------------------------------------------------------------- */

static void notice_error(const gchar* format, const gchar* name, GError* error) {
    if (lm.app == NULL || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
        return;
    }
    gchar* msg = g_strdup_printf(format, name, error != NULL ? error->message : "");
    app_notice(lm.app, msg, FALSE);
    g_free(msg);
}

static void on_volume_mounted(GObject* source, GAsyncResult* res, gpointer data) {
    gchar* name = (gchar*)data;
    GVolume* volume = G_VOLUME(source);
    GError* error = NULL;
    if (!g_volume_mount_finish(volume, res, &error)) {
        /* TRANSLATORS: %1$s is a disk name, %2$s the reason */
        notice_error(_("Could not mount %1$s: %2$s"), name, error);
        g_clear_error(&error);
        g_free(name);
        return;
    }
    GMount* mount = g_volume_get_mount(volume);
    GFile* root = (mount != NULL) ? g_mount_get_root(mount) : NULL;
    gchar* path = (root != NULL) ? g_file_get_path(root) : NULL;
    if (path != NULL && lm.app != NULL) {
        local_machine_open_dir(lm.app, path);
    }
    g_free(path);
    g_clear_object(&root);
    g_clear_object(&mount);
    g_free(name);
}

static void mount_disk(const DiskVolume* v) {
    if (v->volume == NULL || !v->can_mount) {
        return;
    }
    GMountOperation* op = g_mount_operation_new();
    g_volume_mount(v->volume, G_MOUNT_MOUNT_NONE, op, NULL, on_volume_mounted, g_strdup(v->name));
    g_object_unref(op);
}

static void on_unmounted(GObject* source, GAsyncResult* res, gpointer data) {
    gchar* name = (gchar*)data;
    GError* error = NULL;
    if (!g_mount_unmount_with_operation_finish(G_MOUNT(source), res, &error)) {
        /* TRANSLATORS: %1$s is a disk name, %2$s the reason */
        notice_error(_("Could not unmount %1$s: %2$s"), name, error);
        g_clear_error(&error);
    }
    g_free(name);
}

static void on_ejected(GObject* source, GAsyncResult* res, gpointer data) {
    gchar* name = (gchar*)data;
    GError* error = NULL;
    if (!g_mount_eject_with_operation_finish(G_MOUNT(source), res, &error)) {
        /* TRANSLATORS: %1$s is a disk name, %2$s the reason */
        notice_error(_("Could not eject %1$s: %2$s"), name, error);
        g_clear_error(&error);
    } else if (lm.app != NULL) {
        gchar* msg = g_strdup_printf(_("%s can be removed safely"), name);
        app_notice(lm.app, msg, FALSE);
        g_free(msg);
    }
    g_free(name);
}

/* path is dir or below it */
static gboolean path_under(const gchar* path, const gchar* dir) {
    gsize n = strlen(dir);
    return path != NULL && strncmp(path, dir, n) == 0 &&
           (path[n] == '\0' || G_IS_DIR_SEPARATOR(path[n]) || (n > 0 && G_IS_DIR_SEPARATOR(dir[n - 1])));
}

static void unmount_disk(AppData* app, const DiskVolume* v) {
    if (v->mount == NULL || (!v->can_eject && !v->can_unmount)) {
        if (v->path != NULL) {
            gchar* msg = g_strdup_printf(_("%s cannot be unmounted"), v->name);
            app_notice(app, msg, FALSE);
            g_free(msg);
        }
        return;
    }
    /* The explorer would keep the disk busy: leave it first */
    if (path_under(app->base_dir, v->path)) {
        reload_directory(app, g_get_home_dir());
    }
    GMountOperation* op = g_mount_operation_new();
    if (v->can_eject) {
        g_mount_eject_with_operation(v->mount, G_MOUNT_UNMOUNT_NONE, op, NULL, on_ejected, g_strdup(v->name));
    } else {
        g_mount_unmount_with_operation(v->mount, G_MOUNT_UNMOUNT_NONE, op, NULL, on_unmounted, g_strdup(v->name));
    }
    g_object_unref(op);
}

static void open_disk(AppData* app, const DiskVolume* v) {
    if (v->path != NULL) {
        local_machine_open_dir(app, v->path);
    } else {
        mount_disk(v);
    }
}

void local_machine_activate_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, LOCAL_MACHINE_ROW)) {
        local_machine_open(app);
    } else if (explorer_row_is(row, LOCAL_DISKS_ROW)) {
        explorer_tree_toggle_expanded(app->tree, row);
    } else {
        const DiskVolume* v = local_machine_selected_disk(app);
        if (v != NULL) {
            open_disk(app, v);
        }
    }
}

void local_machine_toggle_selected(AppData* app) {
    const DiskVolume* v = local_machine_selected_disk(app);
    if (v == NULL) {
        local_machine_activate_selected(app);
    } else if (v->path != NULL) {
        unmount_disk(app, v);
    } else {
        mount_disk(v);
    }
}

void local_machine_terminal_selected(AppData* app) {
    const DiskVolume* v = local_machine_selected_disk(app);
    if (v != NULL && v->path != NULL) {
        open_terminal_in(app, v->path);
    } else if (v == NULL) {
        local_machine_open_terminal(app);
    }
}
