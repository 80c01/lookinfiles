#ifndef PREVIEW_BUSY_H
#define PREVIEW_BUSY_H

/* "Rendering…" notice of the preview: when the preview of a newly selected
 * file is not ready after a short delay, the previous preview is dimmed and
 * a spinner with the file name is shown above it, until the new preview is
 * shown or its rendering is cancelled. */
#include "core/app_types.h"

/* Put the notice above content (the Preview page); returns the widget to
 * put in its place */
GtkWidget* preview_busy_wrap(GtkWidget* content);
/* The notice of wrap (from preview_busy_wrap()) is app's preview notice */
void preview_busy_attach(AppData* app, GtkWidget* wrap);
/* The preview of name is being rendered under cancellable: show the notice
 * if it is still rendering after the delay (cancelling it ends the notice) */
void preview_busy_start(AppData* app, const gchar* name, GCancellable* cancellable);
/* The preview is shown (or another entry is selected): hide the notice */
void preview_busy_stop(AppData* app);

/* What a display puts in the preview, for its expected cost */
typedef enum {
    PREVIEW_DISPLAY_TEXT,     /* plain text or code */
    PREVIEW_DISPLAY_DOC,      /* a preview_formats document (text and spans) */
    PREVIEW_DISPLAY_RENDERED, /* Markdown, HTML or Org rendered into the buffer */
} PreviewDisplay;

/* TRUE if displaying bytes of this kind is expected to freeze the window a
 * while (from the cost of the displays measured so far) */
gboolean preview_busy_expect_slow(PreviewDisplay kind, gsize bytes);
/* A display of bytes of this kind started at start_us (monotonic) has just
 * filled the preview: its cost, up to its frame on screen (the text view's
 * layout freezes the window too), is learnt once that frame is painted */
void preview_busy_learn(AppData* app, PreviewDisplay kind, gsize bytes, gint64 start_us);
/* A display expected to be slow: the notice for name is shown at once and
 * then(data) runs once it is on screen (the display then freezes the window
 * with the notice visible). Not run if cancellable is cancelled first or
 * another display or stop comes; destroy(data) is called in every case. */
void preview_busy_show_then(AppData* app, const gchar* name, GCancellable* cancellable,
                            GSourceFunc then, gpointer data, GDestroyNotify destroy);

#endif /* PREVIEW_BUSY_H */
