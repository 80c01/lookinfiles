#ifndef EDITOR_VIEW_H
#define EDITOR_VIEW_H

/* The text view of an editor tab: view, scroll, minimap, mode lines and the
 * view's controllers, around a buffer that the views of one file share (a
 * split view, editor_split_tab()) */
#include "app_types.h"

/* Build tab's widgets (tab->view, scroll, container, minimap, mode state)
 * showing buffer. tab->path, name and mode are set; buffer is not owned. */
void editor_view_build(AppData* app, EditorTab* tab, GtkTextBuffer* buffer);
/* Disconnect from tab->buffer what tab and its view connected to it: the
 * buffer outlives a view while another one shows it. Call before freeing tab. */
void editor_view_release(EditorTab* tab);
/* Another open tab of app showing tab's buffer, or NULL */
EditorTab* editor_view_twin(AppData* app, EditorTab* tab);

#endif /* EDITOR_VIEW_H */
