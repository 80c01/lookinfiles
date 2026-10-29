#ifndef TEXT_STYLE_H
#define TEXT_STYLE_H

/* Text view styling shared by the viewer, editor and preview renderers */
#include "app_types.h"

/* Monospace font toggle for a GtkTextView */
void set_view_monospace(GtkWidget* view, gboolean monospace);

/* Standard text padding (PREVIEW_TEXT_PADDING_X / _Y) of the preview text view */
void set_view_text_padding(GtkWidget* view);

/* TRUE if the name of a text file looks like source code (extension list, then GtkSourceView guess) */
gboolean is_code_file(const gchar* name);

/* Set the syntax highlighting language of the right-pane buffer (app->buffer)
 * from a file name and, if known, its content type (detects scripts without
 * extension from their #! line). NULL name: no highlighting.
 * Returns TRUE if a language was found. */
gboolean apply_viewer_language(AppData* app, const gchar* name, const gchar* content_type);

/* Preview panel background (#121314) and text padding */
void apply_viewer_background(GtkWidget* view);

#ifdef HAVE_GSV
/* Install the VS Code Dark+ style scheme on a source buffer */
void apply_vscode_scheme(GtkTextBuffer* buffer, GtkWidget* view);
#endif

#endif /* TEXT_STYLE_H */
