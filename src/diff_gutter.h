#ifndef DIFF_GUTTER_H
#define DIFF_GUTTER_H

#include <gtk/gtk.h>

/* Width in pixels of the change bar column on the left of a text view */
#define DIFF_GUTTER_WIDTH 4

/* Add a gutter column with change bars (git changes since the last commit, as in VS Code:
 * green = added, blue = modified, red triangle = deleted) to the left of
 * text_view. live: the buffer is edited and the marks follow the edits;
 * otherwise (preview) any change of the buffer clears the marks until the
 * next diff_gutter_set_file(). */
void diff_gutter_attach(GtkWidget* text_view, gboolean live);

/* Compare the buffer of text_view with the HEAD version of path (read in a
 * worker thread). NULL clears the marks. No effect if not attached. */
void diff_gutter_set_file(GtkWidget* text_view, const gchar* path);

/* Show line numbers before the change bars: buffer line first_line is
 * numbered 1 and the lines above it get no number (e.g. a header); -1 hides
 * them. In a preview (not live) any change of the buffer hides them again,
 * so set them after the text. The cursor line is highlighted when live. */
void diff_gutter_set_line_numbers(GtkWidget* text_view, gint first_line);

/* TRUE when text_view shows line numbers */
gboolean diff_gutter_get_line_numbers(GtkWidget* text_view);

/* Change marks of text_view (one LineDiffMark set per buffer line), NULL when
 * none; *n_lines receives their number. Owned by the gutter. */
const guint8* diff_gutter_get_marks(GtkWidget* text_view, guint* n_lines);

/* Also redraw widget (e.g. the minimap of text_view) when the marks change */
void diff_gutter_add_companion(GtkWidget* text_view, GtkWidget* widget);

/* Read the HEAD version again for every text view showing a file (after a
 * commit, checkout...) */
void diff_gutter_refresh_all(void);

#endif /* DIFF_GUTTER_H */
