#include "remote.h"
#include "palette.h"
#include "dialog.h"
#include "tree_nav.h"
#include "app_events.h"
#include "file_list.h"
#include "terminal.h"
#include "explorer_tree.h"
#include "icon_picker.h"
#include "notebook_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>



RemoteMachine* remote_get_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, "remote") ? remote_find_by_id(app, row->path) : NULL;
}

/* Folder row selected in the remote view, NULL otherwise */
static RemoteFolder* remote_get_selected_folder(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, "remote_folder") ? remote_folder_find(app, row->path) : NULL;
}

/* Folder where a new machine goes: the selected folder, or the selected machine's */
static RemoteFolder* remote_context_folder(AppData* app) {
    RemoteFolder* folder = remote_get_selected_folder(app);
    return (folder != NULL) ? folder : remote_machine_folder(app, remote_get_selected(app));
}

/* ------------------------------------------------------------------------- */

static void remote_append_machine_row(ExplorerRow* parent, RemoteMachine* rm) {
    gchar* target = NULL;
    if (rm->user != NULL && rm->user[0] != '\0') {
        target = g_strdup_printf("%s@%s", rm->user, rm->host);
    } else {
        target = g_strdup(rm->host);
    }

    gchar* status_markup = NULL;
    if (rm->is_connected) {
        status_markup = g_markup_printf_escaped("<span foreground=\"%s\" weight=\"bold\">● Connected</span>  <span foreground=\"%s\">(%s:%d)</span>",
                                        palette_color(PAL_SUCCESS), palette_color(PAL_FG_SUBTLE), target, rm->port);
    } else {
        status_markup = g_markup_printf_escaped("<span foreground=\"%s\">%s:%d</span>",
                                        palette_color(PAL_FG_SUBTLE), target, rm->port);
    }

    /* The row path holds the machine id (see remote_get_selected()) */
    const gchar* name = rm->name ? rm->name : rm->host;
    ExplorerRow* row = explorer_row_append(parent, "remote", name, name, rm->id);
    explorer_row_set_glyph(row, rm->is_connected ? "remote-connected" : "remote");
    explorer_row_set_badge(row, status_markup);

    g_free(target);
    g_free(status_markup);
}

/* Folders first (their machines inside), then the machines of the top level */
void remote_append_rows(ExplorerRow* root, AppData* app) {
    if (root == NULL || app == NULL) {
        return;
    }

    if (app->remotes == NULL && app->remote_folders == NULL) {
        explorer_row_append(root, "-", "", "No remote machines yet — press a to add one", "");
        return;
    }

    for (GList* f = app->remote_folders; f != NULL; f = f->next) {
        RemoteFolder* folder = (RemoteFolder*)f->data;
        /* The row path holds the folder id (see remote_get_selected_folder()) */
        ExplorerRow* row = explorer_row_append_container(root, "remote_folder", folder->name,
                                                         folder->name, folder->id);
        explorer_row_set_glyph(row, folder->icon);
        guint n = remote_folder_count(app, folder);
        if (n > 0) {
            gchar* badge = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), n);
            explorer_row_set_badge(row, badge);
            g_free(badge);
        }
        for (GList* l = app->remotes; l != NULL; l = l->next) {
            RemoteMachine* rm = (RemoteMachine*)l->data;
            if (rm != NULL && remote_machine_folder(app, rm) == folder) {
                remote_append_machine_row(row, rm);
            }
        }
        if (n == 0) {
            explorer_row_append(row, "-", "", "  (empty)", "");
        }
    }

    for (GList* l = app->remotes; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm != NULL && remote_machine_folder(app, rm) == NULL) {
            remote_append_machine_row(root, rm);
        }
    }
}

/* F5 / refresh button: re-read ~/.ssh/config, then redraw the list */
void remote_rescan(AppData* app) {
    if (app == NULL) {
        return;
    }
    remote_reload_ssh_config(app);
    remote_refresh(app);
}

typedef struct {
    const gchar* type;
    const gchar* path;
} RemoteRowKey;

static gboolean remote_match_row(ExplorerRow* row, gpointer data) {
    RemoteRowKey* key = (RemoteRowKey*)data;
    return explorer_row_is(row, key->type) && g_strcmp0(row->path, key->path) == 0;
}

/* Rebuild the list, expand the folders left expanded and select the row of
 * the given type ("remote" or "remote_folder") and id, else the first row */
static void remote_refresh_select(AppData* app, const gchar* type, const gchar* id) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        return;
    }
    /* A pending directory scan would overwrite the remote listing */
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_scan_pending = FALSE;

    tree_nav_block_selection_handler(app);
    explorer_tree_clear(app->tree);
    ExplorerRow* root = explorer_tree_get_root(app->tree);
    remote_append_rows(root, app);
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* child = explorer_row_child(root, i);
        RemoteFolder* folder = explorer_row_is(child, "remote_folder") ? remote_folder_find(app, child->path) : NULL;
        if (folder != NULL && folder->expanded) {
            explorer_tree_set_expanded(app->tree, child, TRUE);
        }
    }
    tree_nav_unblock_selection_handler(app);

    ExplorerRow* row = NULL;
    if (type != NULL && id != NULL) {
        RemoteRowKey key = { type, id };
        row = explorer_row_find(root, remote_match_row, &key);
    }
    if (row != NULL) {
        explorer_tree_expand_to(app->tree, row);
        select_row(app, row);
    } else {
        select_first_row(app);
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void remote_refresh(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        return;
    }
    ExplorerRow* sel = tree_nav_selected(app);
    gchar* type = (sel != NULL) ? g_strdup(sel->type) : NULL;
    gchar* id = (sel != NULL) ? g_strdup(sel->path) : NULL;
    remote_refresh_select(app, type, id);
    g_free(type);
    g_free(id);
}

gboolean remote_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    if (app == NULL || !explorer_row_is(row, "remote_folder")) {
        return FALSE;
    }
    RemoteFolder* folder = remote_folder_find(app, row->path);
    if (folder != NULL && folder->expanded != expanded) {
        folder->expanded = expanded;
        remote_save(app);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Modal Dialogs                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMainLoop* loop;
    gint response;
} RemoteDialogHelper;

static void on_remote_dlg_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    RemoteDialogHelper* d = (RemoteDialogHelper*)user_data;
    d->response = response_id;
    if (d->loop != NULL && g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static gint remote_run_modal(GtkWidget* dlg, RemoteDialogHelper* d, GtkWidget* focus) {
    d->loop = g_main_loop_new(NULL, FALSE);
    d->response = APP_RESPONSE_CANCEL;
    g_signal_connect(dlg, "response", G_CALLBACK(on_remote_dlg_response), d);
    app_dialog_present(APP_DIALOG(dlg));
    if (focus != NULL) {
        gtk_widget_grab_focus(focus);
    }
    g_main_loop_run(d->loop);
    g_main_loop_unref(d->loop);
    return d->response;
}

static GtkWidget* remote_dialog_body(GtkWidget* dlg) {
    GtkWidget* content = app_dialog_get_content_area(APP_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(content), box);
    return box;
}

static GtkWidget* create_labeled_entry(GtkWidget* box, const gchar* label_text, const gchar* initial_value, const gchar* placeholder) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* lbl = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_add_css_class(lbl, "field-label");
    GtkWidget* entry = gtk_entry_new();
    if (initial_value != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(entry), initial_value);
    }
    if (placeholder != NULL) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    }
    gtk_box_append(GTK_BOX(row), lbl);
    gtk_box_append(GTK_BOX(row), entry);
    gtk_box_append(GTK_BOX(box), row);
    return entry;
}

/* "Folder" drop-down of the machine dialogs: the top level, then every folder */
static GtkWidget* create_folder_dropdown(GtkWidget* box, AppData* app, RemoteFolder* current) {
    GPtrArray* labels = g_ptr_array_new();
    g_ptr_array_add(labels, (gpointer)"Remote machines (top level)");
    guint selected = 0;
    for (GList* l = app->remote_folders; l != NULL; l = l->next) {
        RemoteFolder* folder = (RemoteFolder*)l->data;
        if (folder == current) {
            selected = labels->len;
        }
        g_ptr_array_add(labels, folder->name);
    }
    g_ptr_array_add(labels, NULL);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* lbl = gtk_label_new("Folder:");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_add_css_class(lbl, "field-label");
    GtkWidget* dd = gtk_drop_down_new_from_strings((const gchar* const*)labels->pdata);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), selected);
    gtk_box_append(GTK_BOX(row), lbl);
    gtk_box_append(GTK_BOX(row), dd);
    gtk_box_append(GTK_BOX(box), row);
    g_ptr_array_unref(labels);
    return dd;
}

static RemoteFolder* folder_from_dropdown(AppData* app, GtkWidget* dd) {
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    if (idx == 0 || idx == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }
    return (RemoteFolder*)g_list_nth_data(app->remote_folders, idx - 1);
}

void remote_add_dialog(AppData* app) {
    if (app == NULL) {
        return;
    }
    GtkWidget* dlg = app_dialog_new("Add Remote Machine (SSH)", app->window,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Save & Connect", APP_RESPONSE_APPLY,
                                    "_Save", APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = remote_dialog_body(dlg);

    GtkWidget* name_ent = create_labeled_entry(box, "Friendly Name:", "Dev Server", "e.g. AWS Production");
    GtkWidget* host_ent = create_labeled_entry(box, "Host / IP Address *:", "", "e.g. 192.168.1.100 or dev.example.com");
    GtkWidget* user_ent = create_labeled_entry(box, "User:", g_get_user_name(), "e.g. ubuntu, root, dev");
    GtkWidget* port_ent = create_labeled_entry(box, "Port:", "22", "default: 22");
    GtkWidget* path_ent = create_labeled_entry(box, "Remote Directory:", "~", "default: ~ or /var/www");
    GtkWidget* key_ent  = create_labeled_entry(box, "Identity Key Path (optional):", "", "e.g. ~/.ssh/id_ed25519");
    GtkWidget* folder_dd = create_folder_dropdown(box, app, remote_context_folder(app));

    RemoteDialogHelper d = { NULL, APP_RESPONSE_CANCEL };
    gint resp = remote_run_modal(dlg, &d, host_ent);

    if (resp == APP_RESPONSE_OK || resp == APP_RESPONSE_APPLY) {
        const gchar* host_txt = gtk_editable_get_text(GTK_EDITABLE(host_ent));
        const gchar* name_txt = gtk_editable_get_text(GTK_EDITABLE(name_ent));
        const gchar* user_txt = gtk_editable_get_text(GTK_EDITABLE(user_ent));
        const gchar* port_txt = gtk_editable_get_text(GTK_EDITABLE(port_ent));
        const gchar* path_txt = gtk_editable_get_text(GTK_EDITABLE(path_ent));
        const gchar* key_txt  = gtk_editable_get_text(GTK_EDITABLE(key_ent));

        if (host_txt != NULL && host_txt[0] != '\0') {
            RemoteMachine* rm = g_new0(RemoteMachine, 1);
            rm->id = g_strdup_printf("rem_%u_%u", (guint)time(NULL), (guint)g_random_int());
            rm->name = (name_txt && name_txt[0]) ? g_strdup(name_txt) : g_strdup(host_txt);
            rm->host = g_strdup(host_txt);
            rm->user = (user_txt && user_txt[0]) ? g_strdup(user_txt) : g_strdup("");
            rm->port = (port_txt && atoi(port_txt) > 0) ? atoi(port_txt) : 22;
            rm->remote_path = (path_txt && path_txt[0]) ? g_strdup(path_txt) : g_strdup("~");
            rm->identity_file = (key_txt && key_txt[0]) ? g_strdup(key_txt) : g_strdup("");
            rm->from_ssh_config = FALSE;
            rm->is_connected = FALSE;
            RemoteFolder* folder = folder_from_dropdown(app, folder_dd);
            remote_machine_set_folder(rm, folder);
            if (folder != NULL) {
                folder->expanded = TRUE;
            }
            app->remotes = g_list_append(app->remotes, rm);
            remote_save(app);
            remote_refresh_select(app, "remote", rm->id);

            if (resp == APP_RESPONSE_APPLY) {
                app_dialog_destroy(APP_DIALOG(dlg));
                remote_connect_machine(app, rm);
                return;
            }
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
}

void remote_edit_dialog(AppData* app) {
    if (remote_get_selected_folder(app) != NULL) {
        remote_edit_folder_dialog(app);
        return;
    }
    RemoteMachine* rm = remote_get_selected(app);
    if (rm == NULL) {
        return;
    }

    GtkWidget* dlg = app_dialog_new("Edit Remote Machine", app->window,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Save", APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = remote_dialog_body(dlg);

    gchar port_buf[16];
    snprintf(port_buf, sizeof(port_buf), "%d", rm->port);

    GtkWidget* name_ent = create_labeled_entry(box, "Friendly Name:", rm->name, "");
    GtkWidget* host_ent = create_labeled_entry(box, "Host / IP Address *:", rm->host, "");
    GtkWidget* user_ent = create_labeled_entry(box, "User:", rm->user, "");
    GtkWidget* port_ent = create_labeled_entry(box, "Port:", port_buf, "");
    GtkWidget* path_ent = create_labeled_entry(box, "Remote Directory:", rm->remote_path, "");
    GtkWidget* key_ent  = create_labeled_entry(box, "Identity Key Path (optional):", rm->identity_file, "");
    GtkWidget* folder_dd = create_folder_dropdown(box, app, remote_machine_folder(app, rm));

    RemoteDialogHelper d = { NULL, APP_RESPONSE_CANCEL };
    gint resp = remote_run_modal(dlg, &d, name_ent);

    if (resp == APP_RESPONSE_OK) {
        const gchar* host_txt = gtk_editable_get_text(GTK_EDITABLE(host_ent));
        const gchar* name_txt = gtk_editable_get_text(GTK_EDITABLE(name_ent));
        const gchar* user_txt = gtk_editable_get_text(GTK_EDITABLE(user_ent));
        const gchar* port_txt = gtk_editable_get_text(GTK_EDITABLE(port_ent));
        const gchar* path_txt = gtk_editable_get_text(GTK_EDITABLE(path_ent));
        const gchar* key_txt  = gtk_editable_get_text(GTK_EDITABLE(key_ent));

        if (host_txt != NULL && host_txt[0] != '\0') {
            g_free(rm->name);
            g_free(rm->host);
            g_free(rm->user);
            g_free(rm->remote_path);
            g_free(rm->identity_file);

            rm->name = (name_txt && name_txt[0]) ? g_strdup(name_txt) : g_strdup(host_txt);
            rm->host = g_strdup(host_txt);
            rm->user = (user_txt && user_txt[0]) ? g_strdup(user_txt) : g_strdup("");
            rm->port = (port_txt && atoi(port_txt) > 0) ? atoi(port_txt) : 22;
            rm->remote_path = (path_txt && path_txt[0]) ? g_strdup(path_txt) : g_strdup("~");
            rm->identity_file = (key_txt && key_txt[0]) ? g_strdup(key_txt) : g_strdup("");
            rm->from_ssh_config = FALSE; /* User explicitly saved this machine */
            RemoteFolder* folder = folder_from_dropdown(app, folder_dd);
            remote_machine_set_folder(rm, folder);
            if (folder != NULL) {
                folder->expanded = TRUE;
            }
            remote_save(app);
            remote_refresh_select(app, "remote", rm->id);
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
}

void remote_delete_selected(AppData* app) {
    if (remote_get_selected_folder(app) != NULL) {
        remote_delete_folder_selected(app);
        return;
    }
    RemoteMachine* rm = remote_get_selected(app);
    if (rm == NULL) {
        return;
    }

    gchar* msg = g_strdup_printf("Are you sure you want to remove remote machine '%s' (%s)?",
                                 rm->name ? rm->name : rm->host, rm->host);
    GtkWidget* dlg = app_dialog_new("Remove Remote Machine", app->window,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Delete", APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    GtkWidget* box = remote_dialog_body(dlg);
    GtkWidget* lbl = gtk_label_new(msg);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), lbl);
    g_free(msg);

    RemoteDialogHelper d = { NULL, APP_RESPONSE_CANCEL };
    gint resp = remote_run_modal(dlg, &d, NULL);
    app_dialog_destroy(APP_DIALOG(dlg));

    if (resp == APP_RESPONSE_OK) {
        if (rm->is_connected) {
            remote_disconnect_machine(app, rm);
        }
        app->remotes = g_list_remove(app->remotes, rm);
        remote_machine_free(rm);
        remote_save(app);
        remote_refresh(app);
    }
}

/* Name and icon of a folder; TRUE on OK with *out_name (non-empty) and *out_icon set */
static gboolean remote_folder_dialog(AppData* app, const gchar* title, const gchar* ok_label,
                                     const gchar* init_name, const gchar* init_icon,
                                     gchar** out_name, const gchar** out_icon) {
    GtkWidget* dlg = app_dialog_new(title, app->window,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    ok_label, APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = remote_dialog_body(dlg);
    GtkWidget* name_ent = create_labeled_entry(box, "Name:", init_name, "e.g. Production");
    gtk_entry_set_activates_default(GTK_ENTRY(name_ent), TRUE);

    GtkWidget* lbl = gtk_label_new("Icon:");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_add_css_class(lbl, "field-label");
    gtk_box_append(GTK_BOX(box), lbl);
    GtkWidget* picker = icon_picker_new(NOTEBOOK_ICONS, init_icon, NULL, NULL);
    gtk_box_append(GTK_BOX(box), picker);

    RemoteDialogHelper d = { NULL, APP_RESPONSE_CANCEL };
    gint resp = remote_run_modal(dlg, &d, name_ent);
    gboolean ok = FALSE;
    if (resp == APP_RESPONSE_OK) {
        gchar* name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(name_ent))));
        if (name[0] != '\0') {
            const gchar* icon = icon_picker_get_icon(picker);
            *out_name = name;
            *out_icon = (icon != NULL) ? icon : init_icon;
            ok = TRUE;
        } else {
            g_free(name);
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    return ok;
}

void remote_new_folder_dialog(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* name = NULL;
    const gchar* icon = BOOKMARK_FOLDER_ICON_DEFAULT;
    if (remote_folder_dialog(app, "New Remote Folder", "_Create", NULL, icon, &name, &icon)) {
        RemoteFolder* folder = remote_folder_add(app, name, icon);
        remote_save(app);
        remote_refresh_select(app, "remote_folder", folder->id);
    }
    g_free(name);
}

void remote_edit_folder_dialog(AppData* app) {
    RemoteFolder* folder = remote_get_selected_folder(app);
    if (folder == NULL) {
        return;
    }
    gchar* name = NULL;
    const gchar* icon = folder->icon;
    if (remote_folder_dialog(app, "Edit Remote Folder", "_Save", folder->name, folder->icon, &name, &icon)) {
        g_free(folder->name);
        folder->name = name;
        name = NULL;
        folder->icon = icon;
        remote_save(app);
        remote_refresh_select(app, "remote_folder", folder->id);
    }
    g_free(name);
}

void remote_delete_folder_selected(AppData* app) {
    RemoteFolder* folder = remote_get_selected_folder(app);
    if (folder == NULL) {
        return;
    }
    guint n = remote_folder_count(app, folder);
    gchar* msg = (n > 0)
        ? g_strdup_printf("Delete the folder “%s”? Its %u machine%s move%s to the top level.",
                          folder->name, n, n == 1 ? "" : "s", n == 1 ? "s" : "")
        : g_strdup_printf("Delete the empty folder “%s”?", folder->name);
    GtkWidget* dlg = app_dialog_new("Delete Remote Folder", app->window,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Delete", APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    GtkWidget* box = remote_dialog_body(dlg);
    GtkWidget* lbl = gtk_label_new(msg);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), lbl);
    g_free(msg);

    RemoteDialogHelper d = { NULL, APP_RESPONSE_CANCEL };
    gint resp = remote_run_modal(dlg, &d, NULL);
    app_dialog_destroy(APP_DIALOG(dlg));

    if (resp == APP_RESPONSE_OK) {
        remote_folder_remove(app, folder);
        remote_save(app);
        remote_refresh_select(app, NULL, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Connecting, Mounting & Disconnecting                                      */
/* ------------------------------------------------------------------------- */

gboolean remote_connect_machine(AppData* app, RemoteMachine* rm) {
    if (app == NULL || rm == NULL) {
        return FALSE;
    }

    /* Check if sshfs is installed */
    if (g_find_program_in_path("sshfs") == NULL) {
        GtkWidget* dlg = app_dialog_new("sshfs Required", app->window,
                                        "_Close", APP_RESPONSE_OK,
                                        NULL);
        GtkWidget* box = remote_dialog_body(dlg);
        GtkWidget* lbl = gtk_label_new("To browse and edit remote files, 'sshfs' is required.\n\n"
                                       "Please install it:\n"
                                       "  Ubuntu / Debian: sudo apt install sshfs\n"
                                       "  Fedora: sudo dnf install fuse-sshfs\n"
                                       "  Arch: sudo pacman -S sshfs\n\n"
                                       "You can still open an SSH terminal session using the terminal button.");
        gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_box_append(GTK_BOX(box), lbl);
        RemoteDialogHelper d = { NULL, APP_RESPONSE_OK };
        remote_run_modal(dlg, &d, NULL);
        app_dialog_destroy(APP_DIALOG(dlg));
        return FALSE;
    }

    gchar* mount_dir = remote_get_cache_dir(rm->id);

    /* If already mounted and connected, simply switch to files view */
    if (rm->is_connected && rm->mount_point != NULL && g_file_test(rm->mount_point, G_FILE_TEST_IS_DIR)) {
        g_free(app->current_remote_id);
        app->current_remote_id = g_strdup(rm->id);
        if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
            VirtualLayout* cur = &app->workspaces[app->current_ws];
            g_free(cur->remote_id);
            cur->remote_id = g_strdup(rm->id);
        }
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        reload_directory(app, rm->mount_point);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        g_free(mount_dir);
        return TRUE;
    }

    /* Build SSHFS argument list */
    GPtrArray* args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, g_strdup("sshfs"));

    GString* target = g_string_new("");
    if (rm->user != NULL && rm->user[0] != '\0') {
        g_string_append_printf(target, "%s@", rm->user);
    }
    g_string_append(target, rm->host);
    g_string_append(target, ":");
    if (rm->remote_path != NULL && rm->remote_path[0] != '\0' && g_strcmp0(rm->remote_path, "~") != 0) {
        g_string_append(target, rm->remote_path);
    }
    g_ptr_array_add(args, g_string_free(target, FALSE));
    g_ptr_array_add(args, g_strdup(mount_dir));

    if (rm->port > 0 && rm->port != 22) {
        g_ptr_array_add(args, g_strdup("-p"));
        g_ptr_array_add(args, g_strdup_printf("%d", rm->port));
    }

    g_ptr_array_add(args, g_strdup("-o"));
    g_ptr_array_add(args, g_strdup("reconnect,ServerAliveInterval=15,ServerAliveCountMax=3,follow_symlinks,idmap=user,StrictHostKeyChecking=accept-new"));

    if (rm->identity_file != NULL && rm->identity_file[0] != '\0') {
        g_ptr_array_add(args, g_strdup("-o"));
        g_ptr_array_add(args, g_strdup_printf("IdentityFile=%s", rm->identity_file));
    }

    g_ptr_array_add(args, NULL);

    gchar* stderr_buf = NULL;
    gint exit_status = 0;
    GError* err = NULL;

    gboolean ok = g_spawn_sync(NULL, (gchar**)args->pdata, NULL,
                               G_SPAWN_SEARCH_PATH, NULL, NULL,
                               NULL, &stderr_buf, &exit_status, &err);
    g_ptr_array_free(args, TRUE);

    if (err != NULL) {
        g_error_free(err);
    }

    if (!ok || exit_status != 0) {
        GtkWidget* dlg = app_dialog_new("Connection Failed", app->window,
                                        "_Close", APP_RESPONSE_OK,
                                        NULL);
        GtkWidget* box = remote_dialog_body(dlg);
        gchar* err_msg = g_strdup_printf("Could not connect to '%s' (%s):\n\n%s",
                                         rm->name ? rm->name : rm->host, rm->host,
                                         (stderr_buf && stderr_buf[0]) ? stderr_buf : "sshfs process exited with an error.");
        GtkWidget* lbl = gtk_label_new(err_msg);
        gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_box_append(GTK_BOX(box), lbl);
        g_free(err_msg);
        g_free(stderr_buf);
        RemoteDialogHelper d = { NULL, APP_RESPONSE_OK };
        remote_run_modal(dlg, &d, NULL);
        app_dialog_destroy(APP_DIALOG(dlg));
        g_free(mount_dir);
        return FALSE;
    }
    g_free(stderr_buf);

    rm->is_connected = TRUE;
    g_free(rm->mount_point);
    rm->mount_point = mount_dir;

    g_free(app->current_remote_id);
    app->current_remote_id = g_strdup(rm->id);

    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        VirtualLayout* cur = &app->workspaces[app->current_ws];
        g_free(cur->remote_id);
        cur->remote_id = g_strdup(rm->id);
    }

    /* Switch to Files Explorer view and navigate into remote mount */
    app->sidebar_view = SIDEBAR_VIEW_FILES;
    reload_directory(app, rm->mount_point);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    return TRUE;
}

gboolean remote_disconnect_machine(AppData* app, RemoteMachine* rm) {
    if (rm == NULL) {
        return FALSE;
    }
    if (rm->mount_point != NULL) {
        gchar* cmd = g_strdup_printf("fusermount3 -u \"%s\" 2>/dev/null || fusermount -u \"%s\" 2>/dev/null || umount \"%s\" 2>/dev/null",
                                     rm->mount_point, rm->mount_point, rm->mount_point);
        int ignored = system(cmd);
        (void)ignored;
        g_free(cmd);
    }
    rm->is_connected = FALSE;

    if (app != NULL && g_strcmp0(app->current_remote_id, rm->id) == 0) {
        g_free(app->current_remote_id);
        app->current_remote_id = NULL;
        if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
            VirtualLayout* cur = &app->workspaces[app->current_ws];
            g_free(cur->remote_id);
            cur->remote_id = NULL;
        }
        /* Fall back to home directory */
        const gchar* home = g_get_home_dir();
        if (home != NULL) {
            reload_directory(app, home);
        }
    }
    if (app != NULL) {
        remote_refresh(app);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
    }
    return TRUE;
}

void remote_connect_selected(AppData* app) {
    RemoteMachine* rm = remote_get_selected(app);
    if (rm != NULL) {
        remote_connect_machine(app, rm);
    }
}

void remote_disconnect_selected(AppData* app) {
    RemoteMachine* rm = remote_get_selected(app);
    if (rm != NULL) {
        remote_disconnect_machine(app, rm);
    }
}

void remote_toggle_connect_selected(AppData* app) {
    RemoteMachine* rm = remote_get_selected(app);
    if (rm == NULL) {
        return;
    }
    if (rm->is_connected) {
        /* If already connected, either navigate into files or disconnect */
        if (app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
            /* Open in files explorer */
            remote_connect_machine(app, rm);
        } else {
            remote_disconnect_machine(app, rm);
        }
    } else {
        remote_connect_machine(app, rm);
    }
}

void remote_activate_selected(AppData* app) {
    if (remote_get_selected_folder(app) != NULL) {
        explorer_tree_toggle_expanded(app->tree, tree_nav_selected(app));
        return;
    }
    RemoteMachine* rm = remote_get_selected(app);
    if (rm == NULL) {
        remote_add_dialog(app);
        return;
    }
    remote_connect_machine(app, rm);
}

/* ------------------------------------------------------------------------- */
/* Remote Terminal Integration                                               */
/* ------------------------------------------------------------------------- */

void remote_open_terminal(AppData* app, RemoteMachine* rm) {
    if (app == NULL || rm == NULL) {
        return;
    }

    /* Make sure terminal is visible */
    if (app->term_scroll != NULL && !gtk_widget_get_visible(app->term_scroll)) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
        if (app->right_paned != NULL) {
            gtk_widget_set_visible(app->right_paned, TRUE);
        }
        app_emit(app, APP_EVENT_CHROME_CHANGED);
    }

#ifdef HAVE_VTE
    /* If this machine is not currently active, set it temporarily or for this workspace */
    g_free(app->current_remote_id);
    app->current_remote_id = g_strdup(rm->id);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        VirtualLayout* cur = &app->workspaces[app->current_ws];
        g_free(cur->remote_id);
        cur->remote_id = g_strdup(rm->id);
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    term_spawn_pane(app, FALSE);
#endif
}

void remote_open_terminal_selected(AppData* app) {
    RemoteMachine* rm = remote_get_selected(app);
    if (rm != NULL) {
        remote_open_terminal(app, rm);
    }
}

/* ------------------------------------------------------------------------- */
/* Keyboard and Context Menu                                                 */
/* ------------------------------------------------------------------------- */

gboolean remote_handle_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        return FALSE;
    }

    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;

    switch (keyval) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        remote_activate_selected(app);
        return TRUE;
    case GDK_KEY_a:
        if (!ctrl) {
            remote_add_dialog(app);
            return TRUE;
        }
        break;
    case GDK_KEY_A:
        if (!ctrl) {
            remote_new_folder_dialog(app);
            return TRUE;
        }
        break;
    case GDK_KEY_e:
    case GDK_KEY_r:
    case GDK_KEY_F2:
        if (!ctrl) {
            remote_edit_dialog(app);
            return TRUE;
        }
        break;
    case GDK_KEY_F5:
        remote_rescan(app);
        return TRUE;
    case GDK_KEY_d:
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        if (!ctrl) {
            remote_delete_selected(app);
            return TRUE;
        }
        break;
    case GDK_KEY_c:
        if (!ctrl) {
            remote_toggle_connect_selected(app);
            return TRUE;
        }
        break;
    case GDK_KEY_t:
        if (!ctrl) {
            remote_open_terminal_selected(app);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static void on_remote_ctx_action(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWidget* popover = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "popover"));
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
    gint action = GPOINTER_TO_INT(user_data);
    AppData* app = (AppData*)g_object_get_data(G_OBJECT(btn), "app");
    if (app == NULL) return;

    switch (action) {
    case 1: remote_connect_selected(app); break;
    case 2: remote_disconnect_selected(app); break;
    case 3: remote_open_terminal_selected(app); break;
    case 4: remote_edit_dialog(app); break;
    case 5: remote_delete_selected(app); break;
    case 6: remote_add_dialog(app); break;
    case 7: remote_rescan(app); break;
    case 8: remote_new_folder_dialog(app); break;
    case 9: remote_activate_selected(app); break;
    default: break;
    }
}

static void add_menu_item(GtkWidget* box, GtkWidget* popover, AppData* app, const gchar* label, const gchar* shortcut, gint action) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_add_css_class(btn, "context-menu-item");

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(row), lbl);

    if (shortcut != NULL) {
        GtkWidget* sc = gtk_label_new(shortcut);
        gtk_widget_add_css_class(sc, "shortcut");
        gtk_box_append(GTK_BOX(row), sc);
    }

    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data(G_OBJECT(btn), "popover", popover);
    g_object_set_data(G_OBJECT(btn), "app", app);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_remote_ctx_action), GINT_TO_POINTER(action));
    gtk_box_append(GTK_BOX(box), btn);
}

static void on_remote_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    AppData* app = (AppData*)user_data;
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        return;
    }

    ExplorerRow* row = explorer_tree_row_at(app->tree, x, y);
    if (row != NULL) {
        select_row(app, row);
    }

    RemoteMachine* rm = remote_get_selected(app);
    RemoteFolder* folder = remote_get_selected_folder(app);

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(app->tree_view));
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);

    if (rm != NULL) {
        if (!rm->is_connected) {
            add_menu_item(box, popover, app, "Connect (Browse Files)", "Enter", 1);
        } else {
            add_menu_item(box, popover, app, "Open in Explorer", "Enter", 1);
            add_menu_item(box, popover, app, "Disconnect", "c", 2);
        }
        add_menu_item(box, popover, app, "Open SSH Terminal", "t", 3);
        add_menu_item(box, popover, app, "Edit Machine…", "F2", 4);
        add_menu_item(box, popover, app, "Delete Machine", "Delete", 5);

        GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_append(GTK_BOX(box), sep);
    }

    if (folder != NULL) {
        add_menu_item(box, popover, app, "Expand / Collapse", "Enter", 9);
        add_menu_item(box, popover, app, "Edit Folder…", "F2", 4);
        add_menu_item(box, popover, app, "Delete Folder", "Delete", 5);

        GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_append(GTK_BOX(box), sep);
    }

    add_menu_item(box, popover, app, folder != NULL ? "Add Machine Here…" : "Add Remote Machine…", "a", 6);
    add_menu_item(box, popover, app, "New Folder…", "Shift+A", 8);
    add_menu_item(box, popover, app, "Re-scan ~/.ssh/config", "F5", 7);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void remote_attach_context_menu(AppData* app) {
    if (app == NULL || app->tree_view == NULL) {
        return;
    }
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_remote_right_click), app);
    gtk_widget_add_controller(GTK_WIDGET(app->tree_view), GTK_EVENT_CONTROLLER(click));
}
