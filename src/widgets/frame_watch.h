/* Frame timings for the performance probe (profile.h): with LOOKIN_PROFILE
 * set, each frame of the window is timed from its frame clock's
 * "before-paint" to its "after-paint" (update, layout and drawing: the work
 * the main thread did for it; painted when its "paint" phase ran), the first one is marked "first frame", and
 * the first idle moment after it ends the start-up (profile_startup_done()).
 * Does nothing without the variable. */
#ifndef FRAME_WATCH_H
#define FRAME_WATCH_H

#include <gtk/gtk.h>

/* Watch the frames of window (call before it is shown) */
void frame_watch_attach(GtkWidget* window);

#endif
