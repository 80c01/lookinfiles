/* The menu of the workspace map (see workspace_menu.h) */
#include "workspace_menu.h"
#include "app_events.h"

typedef struct {
    AppData* app;
    gint idx;
    gboolean save; /* Save as Bookmark, else Close Workspace */
    WorkspaceCloseFn close;
} MenuAction;

static gboolean run_action(gpointer data) {
    MenuAction* a = data;
    if (a->idx < a->app->num_workspaces) {
        if (a->save) {
            a->app->session_save_ws = a->idx;
            app_emit(a->app, APP_EVENT_SAVE_WORKSPACE_BOOKMARK);
        } else {
            a->close(a->app, a->idx);
        }
    }
    g_free(a);
    return G_SOURCE_REMOVE;
}

static void on_item_clicked(GtkButton* btn, gpointer data) {
    gtk_popover_popdown(GTK_POPOVER(data));
    /* Run after the popover is gone: both open dialogs or rebuild the map */
    g_idle_add(run_action, g_memdup2(g_object_get_data(G_OBJECT(btn), "action"), sizeof(MenuAction)));
}

static void add_item(GtkWidget* box, GtkWidget* popover, const gchar* text, const gchar* hint,
                     const MenuAction* action, gboolean sensitive) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (hint != NULL) {
        GtkWidget* key = gtk_label_new(hint);
        gtk_widget_add_css_class(key, "dim-label");
        gtk_box_append(GTK_BOX(row), key);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_set_sensitive(btn, sensitive);
    g_object_set_data_full(G_OBJECT(btn), "action", g_memdup2(action, sizeof(MenuAction)), g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

void workspace_menu_popup(AppData* app, GtkWidget* map, gdouble x, gdouble y, gint idx,
                          WorkspaceCloseFn close) {
    if (app == NULL || map == NULL || idx < 0 || idx >= app->num_workspaces) {
        return;
    }
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, map);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    MenuAction save = { app, idx, TRUE, close };
    MenuAction rm = { app, idx, FALSE, close };
    add_item(box, popover, "Save as Bookmark…", NULL, &save, TRUE);
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 3);
    gtk_widget_set_margin_bottom(sep, 3);
    gtk_box_append(GTK_BOX(box), sep);
    add_item(box, popover, "Close Workspace", idx == app->current_ws ? "Alt+-" : NULL, &rm,
             app->num_workspaces > 1);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}
