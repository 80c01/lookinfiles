/* Icon buttons and labels from the GTK icon theme (see icons.h).
 *
 * Icon themes rename and drop icons between versions (Adwaita moved several
 * to "legacy", then removed them). Each icon used by the chrome therefore has
 * a fallback chain that ends with an icon built into libgtk itself, which is
 * available whatever theme is installed. GTK shows the first name it finds. */
#include "icons.h"

typedef struct {
    const gchar* name;          /* name used by callers */
    const gchar* fallbacks[4];  /* tried in order; the last one is built into GTK */
} IconFallback;

/* Built into libgtk 4 (always available): folder, folder-new, bookmark-new,
 * edit-delete, user-trash, edit-clear-all, list-add, view-refresh, view-reveal,
 * pan-up, network-server, network-workgroup, window-close, go-next,
 * document-open-recent, text-x-generic, dialog-question, view-list, view-grid,
 * system-run. Icons used directly from that list need no entry here. */
static const IconFallback FALLBACKS[] = {
    {"user-bookmarks", {"bookmark-new", NULL}},
    {"accessories-text-editor", {"text-editor", "document-edit", "text-x-generic", NULL}},
    {"document-new", {"text-x-generic", NULL}},
    {"document-edit", {"edit", "text-editor", "text-x-generic", NULL}},
    {"view-sort-ascending", {"view-list", NULL}},
    {"document-send", {"go-next", NULL}},
    {"network-transmit-receive", {"network-workgroup", NULL}},
    {"utilities-terminal", {"terminal", "system-run", NULL}},
    {"help-browser", {"help-about", "dialog-question", NULL}},
    {"x-office-document", {"text-x-generic", NULL}},
    {"view-dual", {"view-grid", NULL}},
    {"view-paged", {"view-list", NULL}},
};

static GIcon* themed_icon_with_fallbacks(const gchar* icon_name) {
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(names, g_strconcat(icon_name, "-symbolic", NULL));
    for (gsize i = 0; i < G_N_ELEMENTS(FALLBACKS); i++) {
        if (g_strcmp0(FALLBACKS[i].name, icon_name) == 0) {
            for (gsize j = 0; j < G_N_ELEMENTS(FALLBACKS[i].fallbacks) && FALLBACKS[i].fallbacks[j]; j++) {
                g_ptr_array_add(names, g_strconcat(FALLBACKS[i].fallbacks[j], "-symbolic", NULL));
            }
            break;
        }
    }
    GIcon* icon = g_themed_icon_new_from_names((gchar**)names->pdata, (gint)names->len);

    /* Report an icon that no name of the chain can provide (caught by make ui-test) */
    GdkDisplay* display = gdk_display_get_default();
    if (display != NULL &&
        !gtk_icon_theme_has_gicon(gtk_icon_theme_get_for_display(display), icon)) {
        g_warning("icons: no icon available for \"%s\" in the current icon theme", icon_name);
    }
    g_ptr_array_free(names, TRUE);
    return icon;
}

static GtkWidget* symbolic_image(const gchar* icon_name) {
    GIcon* icon = themed_icon_with_fallbacks(icon_name);
    GtkWidget* image = gtk_image_new_from_gicon(icon);
    g_object_unref(icon);
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
