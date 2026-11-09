#ifndef PREVIEW_TAGBAR_H
#define PREVIEW_TAGBAR_H

/* The tagbar of the preview (docs/previews.md): the headings of the
 * rendered preview (Markdown, HTML, Org, documents), or the symbols of
 * source code (preview_symbols.h), beside the text, read
 * from the heading tags of the buffer (code_outline_heading_tag_level()),
 * so every format that tags its headings has one without a branch of its
 * own; a format whose headings are not in the text (PDF bookmarks) hands
 * them over with preview_tagbar_set_outline(). It follows the scrolling and a click or Enter scrolls to the
 * heading. Shown in previews with headings when turned on; each workspace
 * keeps its own choice, a new one follows the preference (settings.ini:
 * [Preview] tagbar); same width as the editor tagbar. */
#include "core/app_types.h"

/* Put content (the preview's scrolled text view) and the tagbar side by
 * side; returns the GtkPaned to put in its place */
GtkWidget* preview_tagbar_wrap(AppData* app, GtkWidget* content, GtkWidget* text_view);
/* o, or Ctrl+K Ctrl+O, in the preview: show / hide the tagbar in the
 * current workspace, and save it as the preference (settings.ini) for new
 * workspaces and the next start. TRUE when the key was taken. */
gboolean preview_tagbar_key(AppData* app, guint keyval, GdkModifierType state);
/* Headings that are not tagged in the buffer, or symbols (level 0)
 * (CodeOutlineEntry*, lines of
 * the buffer, referenced): listed with the tagged ones while owner (the
 * preview's cancellable, referenced) is not cancelled, i.e. until the next
 * preview */
void preview_tagbar_set_outline(AppData* app, GCancellable* owner, GPtrArray* entries);
/* Show the tagbar, the focus in it, or hide it, the focus back in the text,
 * in the current workspace only (not saved; o also saves the preference) */
void preview_tagbar_toggle(AppData* app);
/* Whether the preview has headings or symbols to list / the tagbar shows */
gboolean preview_tagbar_available(AppData* app);
gboolean preview_tagbar_shown(AppData* app);
/* Turn the tagbar on or off, the preference too (not saved, focus unchanged) */
void preview_tagbar_set_shown(AppData* app, gboolean shown);
/* Turn it on or off in the current workspace only (preference unchanged) /
 * whether it is on there */
void preview_tagbar_set_wanted(AppData* app, gboolean shown);
gboolean preview_tagbar_wanted(AppData* app);
/* Move the handle to the shared tagbar width (editor_tagbar_set_width()) */
void preview_tagbar_place(AppData* app);
/* The list of the tagbar, to focus (Alt+Left / Alt+Right), or NULL */
GtkWidget* preview_tagbar_list(AppData* app);

#endif /* PREVIEW_TAGBAR_H */
