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
    GtkEventController* motion;
    GPtrArray* layouts; /* one per row: the current panels, the default, the saved ones */
    LayoutsPreview* preview; /* the arrangement at the start, put back unless one is applied */
    GtkWidget* focus;   /* the window's focus at the start (referenced) */
    guint preview_id;   /* pending preview timeout, 0 = none */
    gint previewed;     /* row shown (0 = the arrangement at the start) */
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
    layouts_preview_show(s->preview, (index == 0) ? NULL : g_ptr_array_index(s->layouts, (guint)index));
    s->previewed = index;
    /* Moved editor tabs and terminals take the focus: keep it in the list */
    GtkListBoxRow* row = gtk_list_box_get_selected_row(s->list);
    if (row != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(row));
    }
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

/* Hovering over a row previews it after the same short delay as arrows */
static void on_motion(GtkEventControllerMotion* controller, gdouble x, gdouble y, gpointer data) {
    (void)controller;
    (void)x;
    Switcher* s = data;
    GtkListBoxRow* row = gtk_list_box_get_row_at_y(s->list, (gint)y);
    if (row != NULL && row != gtk_list_box_get_selected_row(s->list)) {
        gtk_list_box_select_row(s->list, row);
        gtk_widget_grab_focus(GTK_WIDGET(row));
    }
}

/* The next layout, back to the first after the last */
static void select_next(Switcher* s) {
    gint next = (selected_index(s) + 1) % (gint)s->layouts->len;
    GtkListBoxRow* row = gtk_list_box_get_row_at_index(s->list, next);
    gtk_list_box_select_row(s->list, row);
    gtk_widget_grab_focus(GTK_WIDGET(row));
}

/* Keys pressed in the popover do not reach the window: Alt+L selects the
 * next layout, Ctrl+Q goes on to the window (quit) */
static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    (void)keycode;
    Switcher* s = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    if (mods == GDK_ALT_MASK && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        select_next(s);
        return TRUE;
    }
    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        GtkWidget* window = GTK_WIDGET(s->app->window);
        gtk_popover_popdown(GTK_POPOVER(s->popover));
        return gtk_event_controller_key_forward(controller, window);
    }
    return FALSE;
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
    gtk_widget_unparent(s->popover);
    const SavedLayout* chosen = (s->chosen > 0) ? g_ptr_array_index(s->layouts, (guint)s->chosen) : NULL;
    if (!layouts_preview_end(s->preview, chosen) && s->focus != NULL &&
        gtk_widget_get_root(s->focus) == GTK_ROOT(app->window)) {
        gtk_widget_grab_focus(s->focus);
    }
    g_clear_object(&s->focus);
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
    if (s->motion != NULL) {
        g_signal_handlers_disconnect_by_func(s->motion, on_motion, s);
        s->motion = NULL;
    }
    open_switcher = NULL;
    g_idle_add(finish_idle, s);
}

void layout_switcher_show(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    if (open_switcher != NULL) {
        select_next(open_switcher); /* Alt+L again (from the top-bar button) */
        return;
    }
    if (app->layouts_btn == NULL || !gtk_widget_get_mapped(app->layouts_btn)) {
        layouts_show_dialog(app);
        return;
    }

    Switcher* s = g_new0(Switcher, 1);
    s->app = app;
    s->chosen = -1;
    GtkWidget* focus = gtk_window_get_focus(app->window);
    s->focus = (focus != NULL) ? g_object_ref(focus) : NULL;
    s->preview = layouts_preview_begin(app);
    s->layouts = g_ptr_array_new_with_free_func(saved_layout_free);
    g_ptr_array_add(s->layouts, layouts_capture(app, "Current Layout"));
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
        gchar* title = (i == 0) ? g_strdup("Current Layout")
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
    GtkEventController* motion = gtk_event_controller_motion_new();
    s->motion = motion;
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), s);
    g_signal_connect(motion, "enter", G_CALLBACK(on_motion), s);
    gtk_widget_add_controller(list, motion);
    g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), s);
    g_signal_connect(popover, "closed", G_CALLBACK(on_closed), s);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), s);
    gtk_widget_add_controller(popover, keys);
    open_switcher = s;
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_widget_grab_focus(GTK_WIDGET(first));
}
