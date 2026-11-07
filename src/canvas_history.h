#ifndef CANVAS_HISTORY_H
#define CANVAS_HISTORY_H

#include <glib.h>

/* Undo / redo history of a Canvas note: page states (page.xml content)
 * recorded before each change. Pure model, no widgets.
 *
 *   canvas_history_push(h, state_before);  then change the page
 *   gchar* s = canvas_history_undo(h, state_now);  restore s, free it */

typedef struct CanvasHistory CanvasHistory;

/* limit: the most undo steps kept (the oldest ones are dropped) */
CanvasHistory* canvas_history_new(guint limit);
void canvas_history_free(CanvasHistory* h);

/* Forget every step (another note was opened) */
void canvas_history_clear(CanvasHistory* h);

/* Record the state before a change; clears the redo steps. A state equal to
 * the last one recorded is not recorded twice. */
void canvas_history_push(CanvasHistory* h, const gchar* state);

/* The page changed in a way the history does not record (text typed):
 * the redo steps no longer apply */
void canvas_history_clear_redo(CanvasHistory* h);

gboolean canvas_history_can_undo(const CanvasHistory* h);
gboolean canvas_history_can_redo(const CanvasHistory* h);

/* The state to restore (free it), current becoming a redo (or undo) step;
 * NULL when there is nothing to undo (or redo) */
gchar* canvas_history_undo(CanvasHistory* h, const gchar* current);
gchar* canvas_history_redo(CanvasHistory* h, const gchar* current);

#endif /* CANVAS_HISTORY_H */
