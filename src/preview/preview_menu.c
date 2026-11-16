/* The right-click menu of the Preview tab (see preview_menu.h) */
#include "preview/preview_menu.h"
#include "preview/preview_tagbar.h"
#include "preview/preview_location.h"
#include "widgets/minimap.h"
#include "core/i18n.h"

typedef enum {
    PREVIEW_ACTION_COPY_PATH,
    PREVIEW_ACTION_TAGBAR,
    PREVIEW_ACTION_MINIMAP,
} PreviewMenuAction;

static void on_item_clicked(GtkButton* button, gpointer data) {
    PreviewMenuAction action = GPOINTER_TO_INT(data);
    AppData* app = g_object_get_data(G_OBJECT(button), "preview-menu-app");
    GtkWidget* popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover != NULL) gtk_popover_popdown(GTK_POPOVER(popover));
    switch (action) {
    case PREVIEW_ACTION_COPY_PATH:
        preview_location_copy(app);
        break;
    case PREVIEW_ACTION_TAGBAR:
        preview_tagbar_toggle(app);
        break;
    case PREVIEW_ACTION_MINIMAP:
        minimap_viewer_set_enabled(app, !minimap_viewer_enabled(app));
        break;
    }
}

/* A row of the menu, as in the editor tab menu: the label, then the key */
static void menu_item(GtkWidget* box, AppData* app, const gchar* label, const gchar* shortcut,
                      PreviewMenuAction action) {
    GtkWidget* btn = gtk_button_new();
    gtk_widget_set_name(btn, "tab-context-btn");
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(row, TRUE);
    GtkWidget* lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(row), lbl);
    if (shortcut != NULL) {
        GtkWidget* sc = gtk_label_new(shortcut);
        gtk_widget_add_css_class(sc, "shortcut-badge");
        gtk_box_append(GTK_BOX(row), sc);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data(G_OBJECT(btn), "preview-menu-app", app);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), GINT_TO_POINTER(action));
    gtk_box_append(GTK_BOX(box), btn);
}

/* Right click on the tab title: only the items that apply, labels following
 * the current state */
static void on_tab_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = data;
    GtkWidget* label = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    if (preview_location_current(app) != NULL) menu_item(box, app, _("Copy Path"), "y", PREVIEW_ACTION_COPY_PATH);
    if (preview_tagbar_available(app)) {
        menu_item(box, app, preview_tagbar_shown(app) ? _("Hide Tagbar") : _("Show Tagbar"), "o",
                  PREVIEW_ACTION_TAGBAR);
    }
    if (app->viewer_minimap != NULL) {
        menu_item(box, app, minimap_viewer_enabled(app) ? _("Hide Minimap") : _("Show Minimap"), NULL,
                  PREVIEW_ACTION_MINIMAP);
    }
    if (gtk_widget_get_first_child(box) == NULL) {
        g_object_ref_sink(box);
        g_object_unref(box);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    GtkWidget* popover = gtk_popover_new();
    /* The expanding menu rows must not make the tab expand to the strip width */
    gtk_widget_set_hexpand(popover, FALSE);
    gtk_widget_set_vexpand(popover, FALSE);
    gtk_widget_set_parent(popover, label);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void preview_menu_attach(AppData* app) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* label = gtk_notebook_get_tab_label(nb, gtk_notebook_get_nth_page(nb, 0));
    if (label == NULL) return;
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_tab_right_click), app);
    gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(click));
}
