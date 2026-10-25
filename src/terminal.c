#include "terminal.h"

#ifdef HAVE_VTE

/* VS Code Dark+ integrated-terminal palette: fg/bg like the editor, plus
 * the 16 ANSI colors it ships with (normal row then bright row). */
static const gchar* VSCODE_TERM_FG = "#cccccc";
static const gchar* VSCODE_TERM_BG = "#1e1e1e";
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
    if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* l = children; l != NULL; l = l->next) {
            collect_vte_panes(GTK_WIDGET(l->data), panes);
        }
        g_list_free(children);
    }
}

GPtrArray* term_visible_panes(AppData* app) {
    GPtrArray* panes = g_ptr_array_new();
    collect_vte_panes(app->term_box, panes);
    return panes;
}

static gboolean on_vte_focus_in(GtkWidget* widget, GdkEventFocus* event, gpointer data) {
    (void)event;
    AppData* app = (AppData*)data;
    app->last_focused_vte = widget;
    return FALSE;
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
    apply_terminal_theme(VTE_TERMINAL(vte));
    g_signal_connect(vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(vte, "realize", G_CALLBACK(on_vte_realize), app);
    g_signal_connect(vte, "focus-in-event", G_CALLBACK(on_vte_focus_in), app);
    spawn_shell(app, VTE_TERMINAL(vte));

    GtkWidget* target = get_target_pane(app);
    if (target == NULL) {
        /* First terminal: placed directly in term_box */
        gtk_box_pack_start(GTK_BOX(app->term_box), vte, TRUE, TRUE, 0);
        gtk_widget_show(vte);
    } else {
        GtkWidget* parent = gtk_widget_get_parent(target);
        GtkOrientation orient = new_row ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
        GtkWidget* new_paned = gtk_paned_new(orient);

        g_object_ref(target);
        if (parent == app->term_box) {
            gtk_container_remove(GTK_CONTAINER(app->term_box), target);
            gtk_paned_pack1(GTK_PANED(new_paned), target, TRUE, TRUE);
            gtk_paned_pack2(GTK_PANED(new_paned), vte, TRUE, TRUE);
            gtk_box_pack_start(GTK_BOX(app->term_box), new_paned, TRUE, TRUE, 0);
        } else if (GTK_IS_PANED(parent)) {
            gboolean is_child1 = (gtk_paned_get_child1(GTK_PANED(parent)) == target);
            gtk_container_remove(GTK_CONTAINER(parent), target);
            gtk_paned_pack1(GTK_PANED(new_paned), target, TRUE, TRUE);
            gtk_paned_pack2(GTK_PANED(new_paned), vte, TRUE, TRUE);
            if (is_child1) {
                gtk_paned_pack1(GTK_PANED(parent), new_paned, TRUE, TRUE);
            } else {
                gtk_paned_pack2(GTK_PANED(parent), new_paned, TRUE, TRUE);
            }
        }
        g_object_unref(target);
        gtk_widget_show_all(new_paned);
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
    if (parent == app->term_box) {
        gtk_container_remove(GTK_CONTAINER(app->term_box), term_w);
        gtk_widget_hide(app->term_scroll);
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        return;
    }

    if (GTK_IS_PANED(parent)) {
        GtkPaned* parent_paned = GTK_PANED(parent);
        GtkWidget* sibling = (gtk_paned_get_child1(parent_paned) == term_w) ?
                             gtk_paned_get_child2(parent_paned) :
                             gtk_paned_get_child1(parent_paned);
        GtkWidget* grandparent = gtk_widget_get_parent(parent);

        g_object_ref(sibling);
        gtk_container_remove(GTK_CONTAINER(parent), term_w);
        gtk_container_remove(GTK_CONTAINER(parent), sibling);

        if (grandparent == app->term_box) {
            gtk_container_remove(GTK_CONTAINER(app->term_box), parent);
            gtk_box_pack_start(GTK_BOX(app->term_box), sibling, TRUE, TRUE, 0);
        } else if (GTK_IS_PANED(grandparent)) {
            gboolean is_child1 = (gtk_paned_get_child1(GTK_PANED(grandparent)) == parent);
            gtk_container_remove(GTK_CONTAINER(grandparent), parent);
            if (is_child1) {
                gtk_paned_pack1(GTK_PANED(grandparent), sibling, TRUE, TRUE);
            } else {
                gtk_paned_pack2(GTK_PANED(grandparent), sibling, TRUE, TRUE);
            }
        }
        g_object_unref(sibling);

        GPtrArray* panes = term_visible_panes(app);
        if (panes->len > 0) {
            gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(panes, 0)));
        } else {
            gtk_widget_hide(app->term_scroll);
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        }
        g_ptr_array_unref(panes);
    }
}

void on_vte_realize(GtkWidget* term, gpointer data) {
    (void)data;
    gtk_widget_grab_focus(term);
}

#endif /* HAVE_VTE */
