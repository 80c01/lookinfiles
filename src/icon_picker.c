/* Icon picker (see icon_picker.h) */
#include "icon_picker.h"
#include "icons.h"

#define PER_ROW 11

typedef struct {
    IconPickerChanged on_changed;
    gpointer data;
} PickerCallback;

static void on_toggled(GtkToggleButton* btn, gpointer user_data) {
    if (!gtk_toggle_button_get_active(btn)) {
        return;
    }
    GtkWidget* picker = GTK_WIDGET(user_data);
    const gchar* icon = g_object_get_data(G_OBJECT(btn), "icon");
    g_object_set_data(G_OBJECT(picker), "icon", (gpointer)icon);
    PickerCallback* cb = g_object_get_data(G_OBJECT(picker), "callback");
    if (cb != NULL && cb->on_changed != NULL) {
        cb->on_changed(icon, cb->data);
    }
}

GtkWidget* icon_picker_new(const gchar* const* icons, const gchar* selected,
                           IconPickerChanged on_changed, gpointer data) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    PickerCallback* cb = g_new0(PickerCallback, 1);
    cb->on_changed = on_changed;
    cb->data = data;
    g_object_set_data_full(G_OBJECT(grid), "callback", cb, g_free);

    GtkWidget* group = NULL;
    for (gint i = 0; icons[i] != NULL; i++) {
        GtkWidget* b = gtk_toggle_button_new();
        gtk_button_set_child(GTK_BUTTON(b), glyph_image_new(icons[i]));
        g_object_set_data(G_OBJECT(b), "icon", (gpointer)icons[i]);
        if (group != NULL) {
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(group));
        } else {
            group = b;
        }
        if (g_strcmp0(icons[i], selected) == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
            g_object_set_data(G_OBJECT(grid), "icon", (gpointer)icons[i]);
        }
        g_signal_connect(b, "toggled", G_CALLBACK(on_toggled), grid);
        gtk_grid_attach(GTK_GRID(grid), b, i % PER_ROW, i / PER_ROW, 1, 1);
    }
    return grid;
}

const gchar* icon_picker_get_icon(GtkWidget* picker) {
    return g_object_get_data(G_OBJECT(picker), "icon");
}
