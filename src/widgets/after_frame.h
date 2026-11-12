#ifndef AFTER_FRAME_H
#define AFTER_FRAME_H

/* Run a function once a widget's next frame is on screen: what was just
 * changed (a selected row, a notice) is drawn before work that keeps the
 * main loop busy. */
#include <gtk/gtk.h>

typedef struct AfterFrame AfterFrame;

/* fn(data) runs once from the main loop (idle) after the next frame of
 * widget is painted, or after max_wait_ms if none comes (window hidden);
 * destroy(data) follows. Returns NULL without running fn when widget has
 * no frame clock (not realized). The handle is freed after the run: forget
 * it in fn. */
AfterFrame* after_frame_new(GtkWidget* widget, guint max_wait_ms, GSourceFunc fn, gpointer data,
                            GDestroyNotify destroy);
/* Not run any more; destroy(data) is called */
void after_frame_cancel(AfterFrame* af);

#endif /* AFTER_FRAME_H */
