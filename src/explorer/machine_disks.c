#include "explorer/machine_disks.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/vfs.h"
#include "explorer/explorer_tree.h"
#include "explorer/file_list.h"
#include "explorer/remote_session.h"
#include "explorer/tree_nav.h"
#include "terminal/terminal.h"
#include "theme/palette.h"

#include <string.h>

/* Per machine id: its disks, why they could not be listed, whether a
 * listing runs, whether its row is expanded and its Disks row collapsed
 * (session only: machines start collapsed, their Disks row expanded) */
static struct {
    AppData* app;
    GHashTable* lists;    /* id -> GPtrArray of RemoteDisk */
    GHashTable* errors;   /* id -> message */
    GHashTable* loading;  /* id set */
    GHashTable* expanded; /* id set */
    GHashTable* disks_collapsed; /* id set */
} md;

static void ensure_tables(void) {
    if (md.lists != NULL) {
        return;
    }
    md.lists = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
    md.errors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    md.loading = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    md.expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    md.disks_collapsed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

void machine_disks_forget(const gchar* id) {
    ensure_tables();
    if (id == NULL) {
        g_hash_table_remove_all(md.lists);
        g_hash_table_remove_all(md.errors);
    } else {
        g_hash_table_remove(md.lists, id);
        g_hash_table_remove(md.errors, id);
    }
}

/* ------------------------------------------------------------------------- */
/* Listing on a worker thread                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* id;
    gchar* mount; /* the machine's folder: vfs_exec() runs there */
} ListJob;

static void list_job_free(gpointer data) {
    ListJob* job = data;
    g_free(job->id);
    g_free(job->mount);
    g_free(job);
}

static void list_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    ListJob* job = data;
    gint status = 0;
    GBytes* out = NULL;
    GError* error = NULL;
    if (!vfs_exec(job->mount, REMOTE_DISKS_COMMAND, NULL, cancel, &status, &out, &error)) {
        g_task_return_error(task, error);
        return;
    }
    gsize len = 0;
    const gchar* bytes = (out != NULL) ? g_bytes_get_data(out, &len) : NULL;
    gchar* text = g_strndup(bytes != NULL ? bytes : "", len);
    g_clear_pointer(&out, g_bytes_unref);
    GPtrArray* disks = remote_disks_parse(text);
    g_free(text);
    if (disks->len == 0) {
        g_ptr_array_unref(disks);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s",
                                _("neither findmnt nor df answered on the machine"));
        return;
    }
    g_task_return_pointer(task, disks, (GDestroyNotify)g_ptr_array_unref);
}

static void on_listed(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    GTask* task = G_TASK(res);
    ListJob* job = g_task_get_task_data(task);
    GError* error = NULL;
    GPtrArray* disks = g_task_propagate_pointer(task, &error);
    g_hash_table_remove(md.loading, job->id);
    RemoteMachine* rm = (md.app != NULL) ? remote_find_by_id(md.app, job->id) : NULL;
    if (rm == NULL || !rm->is_connected) {
        /* Gone or disconnected meanwhile: nothing to show */
        g_clear_pointer(&disks, g_ptr_array_unref);
        g_clear_error(&error);
        return;
    }
    if (disks != NULL) {
        g_hash_table_replace(md.lists, g_strdup(job->id), disks);
    } else {
        g_hash_table_replace(md.errors, g_strdup(job->id), g_strdup(error != NULL ? error->message : ""));
        g_clear_error(&error);
    }
    if (md.app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        app_emit(md.app, APP_EVENT_FILES_CHANGED); /* rebuilds the Machines view */
    }
}

/* List the disks of rm unless they are listed or being listed */
static void maybe_list(RemoteMachine* rm) {
    ensure_tables();
    if (rm == NULL || !rm->is_connected || rm->mount_point == NULL ||
        g_hash_table_contains(md.lists, rm->id) || g_hash_table_contains(md.errors, rm->id) ||
        g_hash_table_contains(md.loading, rm->id)) {
        return;
    }
    g_hash_table_add(md.loading, g_strdup(rm->id));
    ListJob* job = g_new0(ListJob, 1);
    job->id = g_strdup(rm->id);
    job->mount = g_strdup(rm->mount_point);
    GTask* task = g_task_new(NULL, NULL, on_listed, NULL);
    g_task_set_task_data(task, job, list_job_free);
    g_task_run_in_thread(task, list_thread);
    g_object_unref(task);
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

static void append_hint(ExplorerRow* parent, const gchar* id, const gchar* text) {
    explorer_row_append(parent, MACHINE_HINT_ROW, text, text, id);
}

static void append_disk_row(ExplorerRow* parent, const RemoteDisk* d) {
    ExplorerRow* row = explorer_row_append(parent, MACHINE_DISK_ROW, d->name, d->name, d->target);
    explorer_row_set_glyph(row, disk_volume_glyph(d->kind));
    gchar* badge = NULL;
    if (d->size > 0) {
        gchar* avail = g_format_size(d->avail);
        /* TRANSLATORS: %1$s is a folder, %2$s a size ("12.3 GB") */
        gchar* text = g_strdup_printf(_("%1$s, %2$s free"), d->target, avail);
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_SUBTLE), text);
        g_free(text);
        g_free(avail);
    } else {
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_SUBTLE),
                                        d->target);
    }
    explorer_row_set_badge(row, badge);
    g_free(badge);
}

void machine_disks_append(ExplorerRow* machine_row, AppData* app, RemoteMachine* rm) {
    if (machine_row == NULL || rm == NULL) {
        return;
    }
    ensure_tables();
    md.app = app;
    if (!rm->is_connected) {
        append_hint(machine_row, rm->id, _("Not connected — Enter to connect"));
        return;
    }
    if (g_hash_table_contains(md.expanded, rm->id)) {
        maybe_list(rm);
    }
    ExplorerRow* group = explorer_row_append_container(machine_row, MACHINE_DISKS_ROW, _("Disks"), _("Disks"),
                                                       rm->id);
    explorer_row_set_glyph(group, "root");
    GPtrArray* disks = g_hash_table_lookup(md.lists, rm->id);
    const gchar* error = g_hash_table_lookup(md.errors, rm->id);
    if (disks != NULL) {
        gchar* count = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), disks->len);
        explorer_row_set_badge(group, count);
        g_free(count);
        for (guint i = 0; i < disks->len; i++) {
            append_disk_row(group, g_ptr_array_index(disks, i));
        }
    } else if (error != NULL) {
        gchar* text = g_strdup_printf(_("Could not list the disks: %s"), error);
        append_hint(group, rm->id, text);
        g_free(text);
    } else {
        append_hint(group, rm->id, _("Loading…"));
    }
}

static void restore_machine(AppData* app, ExplorerRow* machine) {
    if (!g_hash_table_contains(md.expanded, machine->path)) {
        return;
    }
    explorer_tree_set_expanded(app->tree, machine, TRUE);
    for (guint j = 0; j < explorer_row_n_children(machine); j++) {
        ExplorerRow* group = explorer_row_child(machine, j);
        if (explorer_row_is(group, MACHINE_DISKS_ROW) && !g_hash_table_contains(md.disks_collapsed, machine->path)) {
            explorer_tree_set_expanded(app->tree, group, TRUE);
        }
    }
}

void machine_disks_restore_expanded(AppData* app, ExplorerRow* root) {
    if (app == NULL || app->tree == NULL || root == NULL) {
        return;
    }
    ensure_tables();
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* row = explorer_row_child(root, i);
        if (explorer_row_is(row, "remote")) {
            restore_machine(app, row);
        } else if (explorer_row_is(row, "remote_folder") && explorer_tree_is_expanded(app->tree, row)) {
            for (guint j = 0; j < explorer_row_n_children(row); j++) {
                ExplorerRow* machine = explorer_row_child(row, j);
                if (explorer_row_is(machine, "remote")) {
                    restore_machine(app, machine);
                }
            }
        }
    }
}

gboolean machine_disks_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    ensure_tables();
    if (explorer_row_is(row, "remote")) {
        if (expanded) {
            g_hash_table_add(md.expanded, g_strdup(row->path));
            md.app = app;
            maybe_list(remote_find_by_id(app, row->path));
        } else {
            g_hash_table_remove(md.expanded, row->path);
        }
        return TRUE;
    }
    if (explorer_row_is(row, MACHINE_DISKS_ROW)) {
        if (expanded) {
            g_hash_table_remove(md.disks_collapsed, row->path);
        } else {
            g_hash_table_add(md.disks_collapsed, g_strdup(row->path));
        }
        return TRUE;
    }
    return FALSE;
}

gboolean machine_disks_any_expanded(void) {
    return md.expanded != NULL && g_hash_table_size(md.expanded) > 0;
}

void machine_disks_set_expanded(AppData* app, gboolean expanded) {
    ensure_tables();
    g_hash_table_remove_all(md.expanded);
    g_hash_table_remove_all(md.disks_collapsed);
    if (!expanded) {
        return;
    }
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        if (rm->is_connected) {
            g_hash_table_add(md.expanded, g_strdup(rm->id));
            md.app = app;
            maybe_list(rm);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Selection and keys                                                        */
/* ------------------------------------------------------------------------- */

gboolean machine_disks_owns_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, MACHINE_DISKS_ROW) || explorer_row_is(row, MACHINE_DISK_ROW) ||
           explorer_row_is(row, MACHINE_HINT_ROW);
}

const gchar* machine_disks_selected_machine(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, MACHINE_DISKS_ROW) || explorer_row_is(row, MACHINE_HINT_ROW)) {
        return row->path;
    }
    ExplorerRow* group = explorer_row_is(row, MACHINE_DISK_ROW) ? explorer_row_get_parent(row) : NULL;
    return explorer_row_is(group, MACHINE_DISKS_ROW) ? group->path : NULL;
}

const RemoteDisk* machine_disks_find(const gchar* id, const gchar* target) {
    GPtrArray* disks = (md.lists != NULL && id != NULL) ? g_hash_table_lookup(md.lists, id) : NULL;
    for (guint i = 0; disks != NULL && target != NULL && i < disks->len; i++) {
        RemoteDisk* d = g_ptr_array_index(disks, i);
        if (strcmp(d->target, target) == 0) {
            return d;
        }
    }
    return NULL;
}

/* The selected disk's machine (connected) and local folder, NULL when the
 * row is not a disk. Caller frees. */
static gchar* selected_disk_local(AppData* app, RemoteMachine** out_rm) {
    ExplorerRow* row = tree_nav_selected(app);
    if (!explorer_row_is(row, MACHINE_DISK_ROW)) {
        return NULL;
    }
    RemoteMachine* rm = remote_find_by_id(app, machine_disks_selected_machine(app));
    if (rm == NULL || !rm->is_connected) {
        return NULL;
    }
    gchar* local = remote_machine_disk_local(rm, row->path);
    if (local == NULL) {
        app_notice(app, _("Could not create a private folder for the disk"), FALSE);
        return NULL;
    }
    remote_session_files_open(rm); /* the disk's own mount, when it has one */
    g_free(app->current_remote_id);
    app->current_remote_id = g_strdup(rm->id);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        VirtualLayout* cur = &app->workspaces[app->current_ws];
        g_free(cur->remote_id);
        cur->remote_id = g_strdup(rm->id);
    }
    *out_rm = rm;
    return local;
}

gboolean machine_disks_activate_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, MACHINE_HINT_ROW)) {
        return FALSE;
    }
    if (explorer_row_is(row, MACHINE_DISKS_ROW)) {
        explorer_tree_toggle_expanded(app->tree, row);
        return TRUE;
    }
    RemoteMachine* rm = NULL;
    gchar* local = selected_disk_local(app, &rm);
    if (local != NULL) {
        explorer_set_view(app, SIDEBAR_VIEW_FILES);
        reload_directory(app, local);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        g_free(local);
    }
    return TRUE;
}

void machine_disks_terminal_selected(AppData* app) {
    RemoteMachine* rm = NULL;
    gchar* local = selected_disk_local(app, &rm);
    if (local != NULL) {
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        terminal_open(app, FALSE, local);
        g_free(local);
    }
}
