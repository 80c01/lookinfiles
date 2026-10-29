#include "remote.h"
#include "dialog.h"
#include "tree_nav.h"
#include "app_events.h"
#include "file_list.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>



RemoteMachine* remote_get_selected(AppData* app) {
    if (app == NULL || app->selection == NULL || app->model == NULL) {
        return NULL;
    }
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return NULL;
    }
    gchar* type = NULL;
    gchar* id = NULL;
    gtk_tree_model_get(app->model, &it,
                       COL_TYPE, &type,
                       COL_PATH, &id, -1);
    RemoteMachine* rm = NULL;
    if (g_strcmp0(type, "remote") == 0 && id != NULL) {
        rm = remote_find_by_id(app, id);
    }
    g_free(type);
    g_free(id);
    return rm;
}

/* ------------------------------------------------------------------------- */

void remote_append_to_store(GtkTreeStore* store, AppData* app) {
    if (store == NULL || app == NULL) {
        return;
    }

    if (app->remotes == NULL) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "",
                           COL_TYPE, "-",
                           COL_LABEL, "🌐 No remote machines yet — press a to add one",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           COL_BM_ID, 0,
                           -1);
        return;
    }

    for (GList* l = app->remotes; l != NULL; l = l->next) {
        RemoteMachine* rm = (RemoteMachine*)l->data;
        if (rm == NULL) {
            continue;
        }

        const gchar* icon = rm->is_connected ? "🟢 🌐" : "🌐";
        gchar* label = g_strdup_printf("%s %s", icon, rm->name ? rm->name : rm->host);

        gchar* target = NULL;
        if (rm->user != NULL && rm->user[0] != '\0') {
            target = g_strdup_printf("%s@%s", rm->user, rm->host);
        } else {
            target = g_strdup(rm->host);
        }

        gchar* status_markup = NULL;
        if (rm->is_connected) {
            status_markup = g_markup_printf_escaped("<span foreground=\"#73c991\" weight=\"bold\">● Connected</span>  <span foreground=\"#888888\">(%s:%d)</span>",
                                            target, rm->port);
        } else {
            status_markup = g_markup_printf_escaped("<span foreground=\"#888888\">%s:%d</span>",
                                            target, rm->port);
        }

        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, rm->name ? rm->name : rm->host,
                           COL_TYPE, "remote",
                           COL_LABEL, label,
                           COL_PATH, rm->id,
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, status_markup,
                           COL_BM_ID, 0,
                           -1);

        g_free(label);
        g_free(target);
        g_free(status_markup);
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

void remote_refresh(AppData* app) {
    if (app == NULL || app->model == NULL || app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        return;
    }
    GtkTreeStore* store = GTK_TREE_STORE(app->model);
    gtk_tree_store_clear(store);
    tree_nav_reset_column_widths(app);
    remote_append_to_store(store, app);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
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
    gtk_window_present(GTK_WINDOW(dlg));
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
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
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
            app->remotes = g_list_append(app->remotes, rm);
            remote_save(app);
            remote_refresh(app);

            if (resp == APP_RESPONSE_APPLY) {
                gtk_window_destroy(GTK_WINDOW(dlg));
                remote_connect_machine(app, rm);
                return;
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dlg));
}

void remote_edit_dialog(AppData* app) {
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
            remote_save(app);
            remote_refresh(app);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dlg));
}

void remote_delete_selected(AppData* app) {
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
    gtk_window_destroy(GTK_WINDOW(dlg));

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
        gtk_window_destroy(GTK_WINDOW(dlg));
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
        gtk_window_destroy(GTK_WINDOW(dlg));
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

    GtkTreePath* path = NULL;
    if (gtk_tree_view_get_path_at_pos(app->tree_view, (gint)x, (gint)y, &path, NULL, NULL, NULL) && path != NULL) {
        gtk_tree_selection_select_path(app->selection, path);
        gtk_tree_path_free(path);
    }

    RemoteMachine* rm = remote_get_selected(app);

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

    add_menu_item(box, popover, app, "Add Remote Machine…", "a", 6);
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
