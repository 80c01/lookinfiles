#ifndef ICONS_H
#define ICONS_H

/* Buttons and tab headers using the GTK icon theme (Adwaita symbolic icons),
 * so the window chrome does not depend on an emoji font. Icon names are the
 * freedesktop / Adwaita names without the "-symbolic" suffix. */
#include <gtk/gtk.h>

/* Frameless, non-focusable icon-only button (sidebar toolbars, close buttons) */
GtkWidget* icon_button_new(const gchar* icon_name, const gchar* tooltip);

/* Replace the content of a button with an icon followed by text (text may be NULL) */
void icon_button_set_content(GtkWidget* button, const gchar* icon_name, const gchar* text);

/* Icon + text widget for notebook tab headers */
GtkWidget* icon_label_new(const gchar* icon_name, const gchar* text);

#endif /* ICONS_H */
