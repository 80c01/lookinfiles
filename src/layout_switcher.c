/* Alt+L layout switcher: preview layouts from a popover (see layout_switcher.h) */
#include "layout_switcher.h"
#include "layout_store.h"
#include "layouts.h"

/* Wait this long on a row before previewing it, so that holding an arrow
 * key does not rebuild the panels at every step */
#define PREVIEW_DELAY_MS 120

typedef struct {
    AppData* app;
    GtkWidget* popover;
    GtkListBox* list;
    GPtrArray* layouts; /* one per row: the current panels, the default, the saved ones */
    guint preview_id;   /* pending preview timeout, 0 = none */
    gint previewed;     /* row whose panels are shown (0 = the current ones) */
    gint chosen;        /* row applied with Enter or a click, -1 = none */
    gboolean edit;      /* open the Edit Layouts dialog once closed */
} Switcher;

static Switcher* open_switcher = NULL;

static gint selected_index(Switcher* s) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(s->list);
    return (row != NULL) ? gtk_list_box_row_get_index(row) : -1;
}

static void show_row(Switcher* s, gint index) {
    if (index < 0 || (guint)index >= s->layouts->len || index == s->previewed) {
        return;
    }
    layouts_preview(s->app, g_ptr_array_index(s->layouts, (guint)index));
    s->previewed = index;
}

static gboolean on_preview_timeout(gpointer data) {
    Switcher* s = data;
    s->preview_id = 0;
    show_row(s, selected_index(s));
    return G_SOURCE_REMOVE;
}

static void on_row_selected(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    (void)row;
    Switcher* s = data;
    if (s->preview_id != 0) {
        g_source_remove(s->preview_id);
    }
    s->preview_id = g_timeout_add(PREVIEW_DELAY_MS, on_preview_timeout, s);
}

static void on_row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    Switcher* s = data;
    s->chosen = gtk_list_box_row_get_index(row);
    gtk_popover_popdown(GTK_POPOVER(s->popover));
}

static void on_edit_clicked(GtkButton* button, gpointer data) {
    (void)button;
    Switcher* s = data;
    s->edit = TRUE;
    gtk_popover_popdown(GTK_POPOVER(s->popover));
}

/* After the popover closed: apply the chosen layout, or put the panels back */
static gboolean finish_idle(gpointer data) {
    Switcher* s = data;
    AppData* app = s->app;
    const SavedLayout* current = g_ptr_array_index(s->layouts, 0);
    gtk_widget_unparent(s->popover);
    if (s->chosen > 0) {
        if (!layouts_apply(app, g_ptr_array_index(s->layouts, (guint)s->chosen))) {
            layouts_preview(app, current); /* terminals kept: back to the panels at open */
        }
    } else if (s->previewed != 0) {
        layouts_preview(app, current);
    }
    gboolean edit = s->edit;
    g_ptr_array_unref(s->layouts);
    g_free(s);
    if (edit) {
        layouts_show_dialog(app);
    }
    return G_SOURCE_REMOVE;
}

static void on_closed(GtkPopover* popover, gpointer data) {
    (void)popover;
    Switcher* s = data;
    if (s->preview_id != 0) {
        g_source_remove(s->preview_id);
        s->preview_id = 0;
    }
    g_signal_handlers_disconnect_by_func(s->list, on_row_selected, s);
    open_switcher = NULL;
    g_idle_add(finish_idle, s);
}

void layout_switcher_show(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    if (open_switcher != NULL) {
        /* Alt+L again: the next layout, back to the first after the last */
        Switcher* s = open_switcher;
        gint next = (selected_index(s) + 1) % (gint)s->layouts->len;
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(s->list, next);
        gtk_list_box_select_row(s->list, row);
        gtk_widget_grab_focus(GTK_WIDGET(row));
        return;
    }
    if (app->layouts_btn == NULL || !gtk_widget_get_mapped(app->layouts_btn)) {
        layouts_show_dialog(app);
        return;
    }

    Switcher* s = g_new0(Switcher, 1);
    s->app = app;
    s->chosen = -1;
    s->layouts = g_ptr_array_new_with_free_func(saved_layout_free);
    g_ptr_array_add(s->layouts, layouts_capture(app, "Current Panels"));
    gchar* path = layout_store_default_path();
    SavedLayout* def = layout_store_load_default(path);
    GPtrArray* named = layout_store_load(path);
    g_free(path);
    g_ptr_array_add(s->layouts, def);
    for (guint i = 0; i < named->len; i++) {
        g_ptr_array_add(s->layouts, saved_layout_copy(g_ptr_array_index(named, i)));
    }
    g_ptr_array_unref(named);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* list = gtk_list_box_new();
    s->list = GTK_LIST_BOX(list);
    gtk_widget_set_name(list, "layout-list");
    gtk_list_box_set_selection_mode(s->list, GTK_SELECTION_SINGLE);
    for (guint i = 0; i < s->layouts->len; i++) {
        const SavedLayout* l = g_ptr_array_index(s->layouts, i);
        gchar* title = (i == 0) ? g_strdup("Current Panels")
                     : (i == 1) ? layouts_default_title(l)
                                : g_strdup(l->name);
        gtk_list_box_append(s->list, layouts_row_new(title, l));
        g_free(title);
    }
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 360);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget* edit = gtk_button_new_with_mnemonic("_Edit Layouts…");
    gtk_button_set_has_frame(GTK_BUTTON(edit), FALSE);
    gtk_widget_set_tooltip_text(edit, "Save, Delete or Reset Layouts");
    gtk_box_append(GTK_BOX(box), edit);

    GtkWidget* popover = gtk_popover_new();
    s->popover = popover;
    gtk_widget_set_parent(popover, app->layouts_btn);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_size_request(popover, 320, -1);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    /* The current panels are selected first: nothing to preview yet */
    GtkListBoxRow* first = gtk_list_box_get_row_at_index(s->list, 0);
    gtk_list_box_select_row(s->list, first);
    g_signal_connect(list, "row-selected", G_CALLBACK(on_row_selected), s);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), s);
    g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), s);
    g_signal_connect(popover, "closed", G_CALLBACK(on_closed), s);
    open_switcher = s;
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_widget_grab_focus(GTK_WIDGET(first));
}
