/* Icon buttons and labels from the GTK icon theme (see icons.h). */
#include "icons.h"

static GtkWidget* symbolic_image(const gchar* icon_name) {
    gchar* name = g_strconcat(icon_name, "-symbolic", NULL);
    GtkWidget* image = gtk_image_new_from_icon_name(name);
    g_free(name);
    return image;
}

GtkWidget* icon_label_new(const gchar* icon_name, const gchar* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), symbolic_image(icon_name));
    if (text != NULL) {
        gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    }
    return box;
}

void icon_button_set_content(GtkWidget* button, const gchar* icon_name, const gchar* text) {
    if (text == NULL) {
        gtk_button_set_child(GTK_BUTTON(button), symbolic_image(icon_name));
    } else {
        gtk_button_set_child(GTK_BUTTON(button), icon_label_new(icon_name, text));
    }
}

GtkWidget* icon_button_new(const gchar* icon_name, const gchar* tooltip) {
    GtkWidget* button = gtk_button_new();
    icon_button_set_content(button, icon_name, NULL);
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_set_focusable(button, FALSE);
    if (tooltip != NULL) {
        gtk_widget_set_tooltip_text(button, tooltip);
    }
    return button;
}
