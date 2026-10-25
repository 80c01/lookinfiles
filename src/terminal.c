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

void term_spawn_pane(AppData* app, gboolean new_row) {
    GtkWidget* vte = vte_terminal_new();
    apply_terminal_theme(VTE_TERMINAL(vte));
    g_signal_connect(vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(vte, "realize", G_CALLBACK(on_vte_realize), app);
    spawn_shell(app, VTE_TERMINAL(vte));

    GtkWidget* row;
    GList* rows = gtk_container_get_children(GTK_CONTAINER(app->term_box));
    if (new_row || rows == NULL) {
        /* A fresh terminal row below the running ones */
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_pack_start(GTK_BOX(app->term_box), row, TRUE, TRUE, 0);
        gtk_widget_show(row);
    } else {
        GList* last = g_list_last(rows);
        row = GTK_WIDGET(last->data);
    }
    g_list_free(rows);
    gtk_box_pack_start(GTK_BOX(row), vte, TRUE, TRUE, 0);
    gtk_widget_show(vte);
}

GPtrArray* term_visible_panes(AppData* app) {
    GPtrArray* panes = g_ptr_array_new();
    GList* rows =
        gtk_container_get_children(GTK_CONTAINER(app->term_box));
    for (GList* r = rows; r != NULL; r = r->next) {
        GList* panes_in_row =
            gtk_container_get_children(GTK_CONTAINER(r->data));
        for (GList* l = panes_in_row; l != NULL; l = l->next) {
            if (gtk_widget_get_visible(GTK_WIDGET(l->data))) {
                g_ptr_array_add(panes, l->data);
            }
        }
        g_list_free(panes_in_row);
    }
    g_list_free(rows);
    return panes;
}

void on_term_child_exited(VteTerminal* term, gint status, gpointer data) {
    (void)status;
    AppData* app = (AppData*)data;
    GtkWidget* row = gtk_widget_get_parent(GTK_WIDGET(term));
    if (row != NULL && gtk_widget_get_parent(row) ==
                       GTK_WIDGET(app->term_box)) {
        gtk_container_remove(GTK_CONTAINER(row), GTK_WIDGET(term));
        if (gtk_container_get_children(GTK_CONTAINER(row)) == NULL) {
            gtk_container_remove(GTK_CONTAINER(app->term_box), row);
        }
    }
    if (gtk_container_get_children(GTK_CONTAINER(app->term_box)) == NULL) {
        gtk_widget_hide(app->term_scroll);
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else {
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len > 0) {
            gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(panes, 0)));
        }
        g_ptr_array_unref(panes);
    }
}

void on_vte_realize(GtkWidget* term, gpointer data) {
    (void)data;
    gtk_widget_grab_focus(term);
}

#endif /* HAVE_VTE */
