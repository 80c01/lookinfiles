#ifndef DIFF_GUTTER_H
#define DIFF_GUTTER_H

#include <gtk/gtk.h>

/* Width in pixels of the change bar column on the left of a text view */
#define DIFF_GUTTER_WIDTH 4

/* Add a change bar column (git changes since the last commit, as in VS Code:
 * green = added, blue = modified, red triangle = deleted) to the left of
 * text_view. live: the buffer is edited and the marks follow the edits;
 * otherwise (preview) any change of the buffer clears the marks until the
 * next diff_gutter_set_file(). */
void diff_gutter_attach(GtkWidget* text_view, gboolean live);

/* Compare the buffer of text_view with the HEAD version of path (read in a
 * worker thread). NULL clears the marks. No effect if not attached. */
void diff_gutter_set_file(GtkWidget* text_view, const gchar* path);

/* Read the HEAD version again for every text view showing a file (after a
 * commit, checkout...) */
void diff_gutter_refresh_all(void);

#endif /* DIFF_GUTTER_H */
