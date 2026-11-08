#ifndef ICONS_H
#define ICONS_H

/* Buttons and tab headers using the GTK icon theme (Adwaita symbolic icons),
 * so the window chrome does not depend on an emoji font. Icon names are the
 * freedesktop / Adwaita names without the "-symbolic" suffix. */
#include <gtk/gtk.h>

/* Frameless, non-focusable icon-only button (sidebar toolbars, close buttons) */
GtkWidget* icon_button_new(const gchar* icon_name, const gchar* tooltip);

/* Image of a theme icon (in a menu button, a toggle button...) */
GtkWidget* icon_image_new(const gchar* icon_name);

/* Replace the content of a button with an icon followed by text (text may be NULL) */
void icon_button_set_content(GtkWidget* button, const gchar* icon_name, const gchar* text);

/* Icon + text widget for notebook tab headers */
GtkWidget* icon_label_new(const gchar* icon_name, const gchar* text);

/* VS Code file-type icon (file_type_icons.h): an image showing the glyph of
 * a file or directory name in its colour (CSS class ft-<colour>, theme.c) */
GtkWidget* file_type_image_new(void);
void file_type_image_set(GtkWidget* image, const gchar* name, gboolean is_dir);

/* Fixed glyph in the same style, for what is not a file:
 * "folder", "notebook", "trash", "remote", "remote-connected", "link" (URL),
 * "missing" (target not found), "unknown" (not checked yet), "home", "root",
 * "canvas", "image", "note", "tip", "important", "warning", "caution", "author",
 * "description", "attachment"; or the name of a bundled Adwaita icon
 * ("starred", "emoji-food"...: notebook icons), drawn grey */
void glyph_image_set(GtkWidget* image, const gchar* glyph);
GtkWidget* glyph_image_new(const gchar* glyph);
/* Glyph + text widget (tab headers, titles) */
GtkWidget* glyph_label_new(const gchar* glyph, const gchar* text);
/* TRUE when glyph is a name glyph_image_set() knows */
gboolean glyph_exists(const gchar* glyph);

/* Same icon in an icon + text widget for notebook tab headers; the label is
 * the box's last child */
GtkWidget* file_type_label_new(const gchar* name, const gchar* text);

/* Same icon as a 16 px paintable in the palette's current colour, for icons
 * inside text (gtk_text_buffer_insert_paintable()). New reference. */
GdkPaintable* file_type_paintable(const gchar* name, gboolean is_dir);
GdkPaintable* glyph_paintable(const gchar* glyph);

/* Insert the glyph / file-type icon of name at it, then a space, both with
 * the text tag tag (NULL for none): icons in preview titles */
void text_insert_glyph(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* glyph, const gchar* tag);
void text_insert_file_icon(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* name,
                           gboolean is_dir, const gchar* tag);

#endif /* ICONS_H */
