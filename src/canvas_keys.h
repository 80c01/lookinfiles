#ifndef CANVAS_KEYS_H
#define CANVAS_KEYS_H

#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

/* The keys of the Canvas page (see canvas_view.h): a key controller on the
 * page root, in the capture phase */
void canvas_keys_attach(CanvasPage* p, GtkWidget* root);

/* Ctrl+= / Ctrl+- / Ctrl+0: 1 / -1 / 0 (zoom in / out / back to 100 %);
 * G_MININT for another key */
gint canvas_keys_zoom_step(guint keyval, GdkModifierType state);

#endif /* CANVAS_KEYS_H */
