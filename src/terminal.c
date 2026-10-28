#include "terminal.h"
#include "topbar.h"
#include "workspace.h"
#include "remote.h"
#include "ui.h"

#ifdef HAVE_VTE

/* VS Code Dark+ integrated-terminal palette: fg/bg like the editor, plus
 * the 16 ANSI colors it ships with (normal row then bright row). */
static const gchar* VSCODE_TERM_FG = "#cccccc";
static const gchar* VSCODE_TERM_BG = "#1f2021";
static const gchar* VSCODE_TERM_CURSOR = "#ffffff";
static const gchar* VSCODE_TERM_SELECTION = "#264f78";
static const gchar* VSCODE_TERM_ANSI[16] = {
    "#000000", "#cd3131", "#0dbc79", "#e5e510",
    "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5",
    "#666666", "#f14c4c", "#23d18b", "#f5f543",
    "#3b8eea", "#d670d6", "#29b8db", "#e5e5e5",
};

static void apply_terminal_theme(VteTerminal* term) {
    GdkRGBA fg, bg;
    GdkRGBA ansi[16];
    gsize i;

    if (!gdk_rgba_parse(&fg, VSCODE_TERM_FG) || !gdk_rgba_parse(&bg, VSCODE_TERM_BG)) {
        return; /* invalid literal: keep VTE defaults */
    }
    for (i = 0; i < 16; i++) {
        gdk_rgba_parse(&ansi[i], VSCODE_TERM_ANSI[i]);
    }
    vte_terminal_set_colors(term, &fg, &bg, ansi, 16);

    GdkRGBA cursor;
    if (gdk_rgba_parse(&cursor, VSCODE_TERM_CURSOR)) {
        vte_terminal_set_color_cursor(term, &cursor);
    }
    GdkRGBA selection;
    if (gdk_rgba_parse(&selection, VSCODE_TERM_SELECTION)) {
        vte_terminal_set_color_highlight(term, &selection);
    }
}

/* Async spawn callback: report failures, then hand child PID to terminal */
static void on_spawn_result(VteTerminal* term, GPid pid, GError* error,
                            gpointer data) {
    (void)data;
    if (error != NULL) {
        g_printerr("[ Cannot start terminal shell: %s ]\n", error->message);
        g_error_free(error);
        return;
    }
    vte_terminal_watch_child(term, pid);
}

static void spawn_shell(AppData* app, VteTerminal* term) {
    RemoteMachine* rm = remote_get_active_machine(app);
    if (rm != NULL && rm->host != NULL && rm->host[0] != '\0') {
        GPtrArray* args = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(args, g_strdup("ssh"));
        g_ptr_array_add(args, g_strdup("-t"));
        if (rm->port > 0 && rm->port != 22) {
            g_ptr_array_add(args, g_strdup("-p"));
            g_ptr_array_add(args, g_strdup_printf("%d", rm->port));
        }
        if (rm->identity_file != NULL && rm->identity_file[0] != '\0') {
            g_ptr_array_add(args, g_strdup("-i"));
            g_ptr_array_add(args, g_strdup(rm->identity_file));
        }

        gchar* target = NULL;
        if (rm->user != NULL && rm->user[0] != '\0') {
            target = g_strdup_printf("%s@%s", rm->user, rm->host);
        } else {
            target = g_strdup(rm->host);
        }
        g_ptr_array_add(args, target);

        if (rm->remote_path != NULL && rm->remote_path[0] != '\0' &&
            g_strcmp0(rm->remote_path, "~") != 0 && g_strcmp0(rm->remote_path, "") != 0) {
            g_ptr_array_add(args, g_strdup_printf("cd %s 2>/dev/null; exec $SHELL -l", rm->remote_path));
        }
        g_ptr_array_add(args, NULL);

        vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, NULL, (gchar**)args->pdata, NULL,
                                 G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                                 on_spawn_result, app);
        g_ptr_array_free(args, TRUE);
        return;
    }

    const gchar* shell = g_getenv("SHELL");
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/bash";
    }
    gchar* argv[2] = { (gchar*)shell, NULL };
    vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, app->base_dir, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                             on_spawn_result, app);
}

static void collect_vte_panes(GtkWidget* widget, GPtrArray* panes) {
    if (widget == NULL || !gtk_widget_get_visible(widget)) {
        return;
    }
    if (VTE_IS_TERMINAL(widget)) {
        g_ptr_array_add(panes, widget);
        return;
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        collect_vte_panes(child, panes);
    }
}

GPtrArray* term_visible_panes(AppData* app) {
    GPtrArray* panes = g_ptr_array_new();
    collect_vte_panes(app->term_box, panes);
    return panes;
}

static void on_vte_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    if (widget != NULL) {
        app->last_focused_vte = widget;
        update_active_panel_highlight(app);
    }
}

static GtkWidget* get_target_pane(AppData* app) {
    GPtrArray* panes = term_visible_panes(app);
    GtkWidget* target = NULL;
    if (panes->len > 0) {
        if (app->last_focused_vte != NULL) {
            for (guint i = 0; i < panes->len; i++) {
                if (g_ptr_array_index(panes, i) == app->last_focused_vte) {
                    target = app->last_focused_vte;
                    break;
                }
            }
        }
        if (target == NULL) {
            target = GTK_WIDGET(g_ptr_array_index(panes, panes->len - 1));
        }
    }
    g_ptr_array_unref(panes);
    return target;
}

void term_spawn_pane(AppData* app, gboolean new_row) {
    GtkWidget* vte = vte_terminal_new();
    gtk_widget_set_hexpand(vte, TRUE);
    gtk_widget_set_vexpand(vte, TRUE);
    apply_terminal_theme(VTE_TERMINAL(vte));
    g_signal_connect(vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(vte, "realize", G_CALLBACK(on_vte_realize), app);

    GtkEventController* focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_vte_focus_enter), app);
    gtk_widget_add_controller(vte, focus_ctrl);

    spawn_shell(app, VTE_TERMINAL(vte));

    GtkWidget* target = get_target_pane(app);
    if (target == NULL) {
        /* First terminal: placed directly in term_box */
        gtk_box_append(GTK_BOX(app->term_box), vte);
    } else {
        GtkWidget* parent = gtk_widget_get_parent(target);
        GtkOrientation orient = new_row ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
        GtkWidget* new_paned = gtk_paned_new(orient);
        gtk_widget_set_hexpand(new_paned, TRUE);
        gtk_widget_set_vexpand(new_paned, TRUE);
        wire_paned_controllers(new_paned, app);

        g_object_ref(target);
        if (parent == app->term_box) {
            gtk_box_remove(GTK_BOX(app->term_box), target);
            gtk_paned_set_start_child(GTK_PANED(new_paned), target);
            gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_end_child(GTK_PANED(new_paned), vte);
            gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
            gtk_box_append(GTK_BOX(app->term_box), new_paned);
        } else if (GTK_IS_PANED(parent)) {
            gboolean is_start = (gtk_paned_get_start_child(GTK_PANED(parent)) == target);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(parent), NULL);
            } else {
                gtk_paned_set_end_child(GTK_PANED(parent), NULL);
            }
            gtk_paned_set_start_child(GTK_PANED(new_paned), target);
            gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_end_child(GTK_PANED(new_paned), vte);
            gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(parent), new_paned);
            } else {
                gtk_paned_set_end_child(GTK_PANED(parent), new_paned);
            }
        }
        g_object_unref(target);
    }

    app->last_focused_vte = vte;
}

void on_term_child_exited(VteTerminal* term, gint status, gpointer data) {
    (void)status;
    AppData* app = (AppData*)data;
    GtkWidget* term_w = GTK_WIDGET(term);

    if (app->last_focused_vte == term_w) {
        app->last_focused_vte = NULL;
    }

    GtkWidget* parent = gtk_widget_get_parent(term_w);
    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), term_w);
        if (parent == app->term_box) {
            workspace_save_current(app);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->term_scroll, FALSE);
            gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
            gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
            if (!files_vis && !preview_vis && app->viewer_box != NULL) {
                gtk_widget_set_visible(app->viewer_box, TRUE);
                preview_vis = TRUE;
            }
            update_path_label(app);
            workspace_apply_positions(app);
            if (files_vis) {
                gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
            } else if (preview_vis) {
                gtk_widget_grab_focus(app->text_view);
            }
        }
        return;
    }

    if (GTK_IS_PANED(parent)) {
        GtkPaned* parent_paned = GTK_PANED(parent);
        GtkWidget* sibling = (gtk_paned_get_start_child(parent_paned) == term_w) ?
                             gtk_paned_get_end_child(parent_paned) :
                             gtk_paned_get_start_child(parent_paned);
        GtkWidget* grandparent = gtk_widget_get_parent(parent);

        g_object_ref(sibling);
        gtk_paned_set_start_child(parent_paned, NULL);
        gtk_paned_set_end_child(parent_paned, NULL);

        if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), parent);
            gtk_box_append(GTK_BOX(grandparent), sibling);
        } else if (GTK_IS_PANED(grandparent)) {
            gboolean is_start = (gtk_paned_get_start_child(GTK_PANED(grandparent)) == parent);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(grandparent), sibling);
            } else {
                gtk_paned_set_end_child(GTK_PANED(grandparent), sibling);
            }
        }
        g_object_unref(sibling);

        GPtrArray* panes = term_visible_panes(app);
        if (panes->len > 0) {
            gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(panes, 0)));
        } else {
            workspace_save_current(app);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->term_scroll, FALSE);
            gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
            gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
            if (!files_vis && !preview_vis && app->viewer_box != NULL) {
                gtk_widget_set_visible(app->viewer_box, TRUE);
                preview_vis = TRUE;
            }
            update_path_label(app);
            workspace_apply_positions(app);
            if (files_vis) {
                gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
            } else if (preview_vis) {
                gtk_widget_grab_focus(app->text_view);
            }
        }
        g_ptr_array_unref(panes);
    }
}

void on_vte_realize(GtkWidget* term, gpointer data) {
    (void)data;
    gtk_widget_grab_focus(term);
}

#endif /* HAVE_VTE */
