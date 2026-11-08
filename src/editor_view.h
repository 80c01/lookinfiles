#ifndef EDITOR_VIEW_H
#define EDITOR_VIEW_H

/* The text view of an editor tab: view, scroll, minimap, mode lines and the
 * view's controllers, around a buffer that the views of one file share (a
 * split view, editor_split_tab()) */
#include "app_types.h"

/* Build tab's widgets (tab->view, scroll, container, minimap, mode state)
 * showing buffer. tab->path, name and mode are set; buffer is not owned. */
void editor_view_build(AppData* app, EditorTab* tab, GtkTextBuffer* buffer);
/* For the first frames tab's view is shown, put it back in line with its
 * scrollbar when GtkTextView loses track of it (done for every new view);
 * follow_cursor also scrolls an off-screen cursor back (a restored session) */
void editor_view_watch_scroll(EditorTab* tab, gboolean follow_cursor);
/* Disconnect from tab->buffer what tab and its view connected to it: the
 * buffer outlives a view while another one shows it (which gets the cursor
 * back when tab had it). Call before freeing tab. */
void editor_view_release(EditorTab* tab);
/* Give tab's view the buffer's cursor and selection (each view of a split
 * file keeps its own; done when the view is focused or clicked) */
void editor_view_activate(EditorTab* tab);
/* The view of tab's buffer that has the cursor (tab itself without a split) */
EditorTab* editor_view_active(EditorTab* tab);
/* Another open tab of app showing tab's buffer, or NULL */
EditorTab* editor_view_twin(AppData* app, EditorTab* tab);

#endif /* EDITOR_VIEW_H */
