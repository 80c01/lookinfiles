#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

/* The shape of the dedicated editor panel in a saved layout (layouts.c):
 * its splits, their sizes, which panes hold files and the terminal tabs of
 * each pane, without the files themselves (text format: editor_splits in
 * layout_store.h). */
#include "app_types.h"

/* Shape of the current workspace's editor panel; "" when it is not shown
 * or holds no tab. Caller frees. */
gchar* editor_layout_capture(AppData* app);
/* Terminal tabs in the current workspace's editor panel */
guint editor_layout_terminal_count(AppData* app);
/* Terminal tabs the shape text asks for */
guint editor_layout_wanted_terminals(const gchar* text);
/* Rebuild the editor panel (already dedicated) as text describes. The open
 * file tabs go to the panes that held files, each source pane to its own
 * pane in order, an empty files pane taking a tab from the fullest one;
 * running terminals fill the terminal tabs in order, missing ones get a new
 * shell, those beyond close (the caller confirmed it). With no terminal in
 * text, the running ones stay in the first pane. Panes left without a tab
 * are removed. */
void editor_layout_apply(AppData* app, const gchar* text);

#endif /* EDITOR_LAYOUT_H */
