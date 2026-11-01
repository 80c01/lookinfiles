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

/* VS Code file-type icon (file_type_icons.h): an image showing the glyph of
 * a file or directory name in its colour (CSS class ft-<colour>, theme.c) */
GtkWidget* file_type_image_new(void);
void file_type_image_set(GtkWidget* image, const gchar* name, gboolean is_dir);

/* Fixed glyph in the same style, for explorer rows that are not files:
 * "folder", "notebook", "trash", "remote", "remote-connected", "link" (URL),
 * "missing" (target not found), "unknown" (target not checked yet) */
void glyph_image_set(GtkWidget* image, const gchar* glyph);

/* Same icon in an icon + text widget for notebook tab headers; the label is
 * the box's last child */
GtkWidget* file_type_label_new(const gchar* name, const gchar* text);

/* Same icon as a 16 px paintable in the palette's current colour, for icons
 * inside text (gtk_text_buffer_insert_paintable()). New reference. */
GdkPaintable* file_type_paintable(const gchar* name, gboolean is_dir);

#endif /* ICONS_H */
