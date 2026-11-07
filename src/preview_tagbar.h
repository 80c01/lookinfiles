#ifndef PREVIEW_TAGBAR_H
#define PREVIEW_TAGBAR_H

/* The tagbar of the preview (docs/previews.md): the headings of the
 * rendered preview (Markdown, HTML, Org, documents) beside the text, read
 * from the heading tags of the buffer (code_outline_heading_tag_level()),
 * so every format that tags its headings has one without a branch of its
 * own. It follows the scrolling and a click or Enter scrolls to the
 * heading. Shown in previews with headings when turned on (settings.ini:
 * [Preview] tagbar); same width as the editor tagbar. */
#include "app_types.h"

/* Put content (the preview's scrolled text view) and the tagbar side by
 * side; returns the GtkPaned to put in its place */
GtkWidget* preview_tagbar_wrap(AppData* app, GtkWidget* content, GtkWidget* text_view);
/* o, or Ctrl+K Ctrl+O, in the preview: show / hide the tagbar (saved in
 * settings.ini). TRUE when the key was taken. */
gboolean preview_tagbar_key(AppData* app, guint keyval, GdkModifierType state);
/* Turn the tagbar on or off (not saved, focus unchanged) */
void preview_tagbar_set_shown(AppData* app, gboolean shown);

#endif /* PREVIEW_TAGBAR_H */
