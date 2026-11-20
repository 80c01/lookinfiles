#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

/* The shape of the dedicated editor panel in a saved layout (layouts.c):
 * its splits, their sizes, which panes hold files and the terminal tabs of
 * each pane, without the files themselves (text format: editor_splits in
 * layout_store.h). */
#include "core/app_types.h"

/* Shape of the current workspace's editor panel; "" when it is not shown
 * or holds no tab. Caller frees. */
gchar* editor_layout_capture(AppData* app);
/* Open file tabs and canvases, in the editor panel or the preview notebook */
guint editor_layout_file_count(AppData* app);
/* Terminal tabs in the current workspace's editor panel */
guint editor_layout_terminal_count(AppData* app);
/* Terminal tabs the shape text asks for */
guint editor_layout_wanted_terminals(const gchar* text);
/* Rebuild the editor panel (already dedicated) as text describes. The open
 * file tabs go to the panes that held files, each source pane to its own
 * pane in order, an empty files pane taking a tab from the fullest one;
 * running terminals fill the terminal tabs in order, missing ones get a new
 * shell, those beyond are stacked after the last terminal tab. With no terminal in
 * text, the running ones stay in the first pane. Panes left without a tab
 * are removed. */
void editor_layout_apply(AppData* app, const gchar* text);
/* The same for layout previews (no view closed): two views of a file may
 * share a pane, terminals of spare (out of any notebook, referenced) fill the
 * terminal tabs after the running ones, before new shells. tools (may be
 * NULL), layout_store.h tool_tabs text, puts the tool pages it lists in
 * their pane or the preview notebook. */
void editor_layout_apply_spare(AppData* app, const gchar* text, GPtrArray* spare, const gchar* tools);

/* Where the tool pages are (editor_page_layout_key()), as layout_store.h
 * tool_tabs text: in the preview notebook, or the panes of the editor panel
 * numbered as editor_layout_capture() lists them. Caller frees. */
gchar* editor_layout_capture_tools(AppData* app);
/* The tool pages of the editor panel that tools puts in the preview go there
 * (the panes are placed by editor_layout_apply_spare() with tools) */
void editor_layout_place_tools(AppData* app, const gchar* tools);

/* Exact arrangement of the editor tabs: the panel's splits, which tab is in
 * which notebook and where, the current pages, the preview notebook's tabs */
typedef struct EditorSnapshot EditorSnapshot;
EditorSnapshot* editor_layout_snapshot(AppData* app);
/* Put the tabs back as taken (terminals out of any notebook come from
 * spare); terminals of the panel not in the snapshot go to spare, or without
 * spare stay in the panel (nothing closes) */
void editor_layout_restore(AppData* app, const EditorSnapshot* snap, GPtrArray* spare);
/* TRUE when every still-live page present in both snapshots is in the same pane */
gboolean editor_snapshot_same_panes(const EditorSnapshot* a, const EditorSnapshot* b);
void editor_snapshot_free(EditorSnapshot* snap);

#endif /* EDITOR_LAYOUT_H */
