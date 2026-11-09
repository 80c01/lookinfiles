/* Context menu of the Canvas page (see canvas_menu.h) */
#include "canvas/canvas_menu.h"
#include "canvas/canvas_clip.h"
#include "canvas/canvas_page_private.h"
#include "core/i18n.h"

/* Where the menu was opened, in page coordinates */
#define MENU_X_KEY "canvas-menu-x"
#define MENU_Y_KEY "canvas-menu-y"

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

static void on_paste(GtkButton* btn, gpointer data) {
    CanvasPage* p = data;
    GtkWidget* popover = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    gint x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(popover), MENU_X_KEY));
    gint y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(popover), MENU_Y_KEY));
    gtk_popover_popdown(GTK_POPOVER(popover));
    if (!p->closed) canvas_clip_paste_at(p, x, y);
}

static GtkWidget* menu_item(const gchar* text, const gchar* hint) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    GtkWidget* key = gtk_label_new(hint);
    gtk_widget_add_css_class(key, "dim-label");
    gtk_box_append(GTK_BOX(row), key);
    gtk_button_set_child(GTK_BUTTON(btn), row);
    return btn;
}

static void on_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    CanvasPage* p = data;
    if (p->closed || p->dir == NULL) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    /* The popover hangs on the page root: the surface may be zoomed */
    graphene_point_t at = GRAPHENE_POINT_INIT((float)x, (float)y), on_root;
    if (!gtk_widget_compute_point(p->surface, p->root, &at, &on_root)) return;

    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    gtk_widget_set_parent(popover, p->root);
    GdkRectangle rect = {(gint)on_root.x, (gint)on_root.y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    g_object_set_data(G_OBJECT(popover), MENU_X_KEY, GINT_TO_POINTER((gint)x));
    g_object_set_data(G_OBJECT(popover), MENU_Y_KEY, GINT_TO_POINTER((gint)y));
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* paste = menu_item(_("Paste"), "Ctrl+V");
    gtk_widget_set_sensitive(paste, canvas_clip_can_paste(p));
    g_signal_connect(paste, "clicked", G_CALLBACK(on_paste), p);
    gtk_box_append(GTK_BOX(box), paste);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void canvas_menu_attach(CanvasPage* p) {
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), p);
    gtk_widget_add_controller(p->surface, GTK_EVENT_CONTROLLER(click));
}
