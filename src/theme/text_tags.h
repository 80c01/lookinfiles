#ifndef TEXT_TAGS_H
#define TEXT_TAGS_H

/* Text tags that follow the theme. text_tag_create() is
 * gtk_text_buffer_create_tag() where a string value may name a palette
 * role ("@accent", "@fg_subtle") or a font role ("@font_code",
 * "@font_ui"): the value is resolved now and remembered, and
 * text_tags_refresh_all() sets it again after the palette or fonts change,
 * in every buffer that has such tags.
 *
 * Colour properties of type GdkRGBA ("underline-rgba"...) take a string
 * here too (a colour or a role), not a GdkRGBA pointer. */
#include <gtk/gtk.h>

GtkTextTag* text_tag_create(GtkTextBuffer* buffer, const gchar* name, const gchar* first_property, ...)
    G_GNUC_NULL_TERMINATED;

/* The tag of the table named name, created with the properties if missing */
GtkTextTag* text_tag_ensure(GtkTextBuffer* buffer, const gchar* name, const gchar* first_property, ...)
    G_GNUC_NULL_TERMINATED;

/* Resolve the role values of every tag made by text_tag_create() again */
void text_tags_refresh_all(void);

/* "@role" -> current value (static string), NULL if s is not a role */
const gchar* text_tags_resolve(const gchar* s);

#endif /* TEXT_TAGS_H */
