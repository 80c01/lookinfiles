/* Side-by-side and unified file comparison tabs (Diff tabs):
 * movable notebook pages tracked per workspace, read-only with syntax
 * highlighting, synchronized scrolling, intra-line change spans, real source
 * line numbers, hunk navigation and one-key jump to the editor. */
#ifndef DIFF_TAB_H
#define DIFF_TAB_H

#include "core/app_types.h"

/* Open a Diff tab comparing two files on disk (read and diffed in a worker) */
void diff_tab_open_files(AppData* app, const gchar* left_path, const gchar* right_path);

/* Open a Diff tab comparing a file against Git HEAD (read in a worker via
 * libgit2). When right_text is non-NULL, compares HEAD against that in-memory
 * snapshot (e.g. an unsaved editor buffer); otherwise reads path from disk. */
void diff_tab_open_head(AppData* app, const gchar* path, const gchar* right_text);

/* Open a Diff tab comparing two in-memory texts (e.g. Saved vs Modified Buffer,
 * or Disk vs Modified Buffer). left_path / right_path may be NULL or real paths
 * (used by the Edit button / Enter). */
void diff_tab_open_texts(AppData* app, const gchar* title,
                         const gchar* left_label, const gchar* left_path, const gchar* left_text,
                         const gchar* right_label, const gchar* right_path, const gchar* right_text,
                         const gchar* lang_hint);

/* Remember path for "Select for Compare" / "Compare with <name>" */
void diff_tab_select_for_compare(AppData* app, const gchar* path);
const gchar* diff_tab_selected_for_compare(AppData* app);

/* Close the focused Diff tab, else the one shown in the preview notebook (Ctrl+W) */
gboolean diff_tab_close_shown(AppData* app);

/* Per-workspace lifecycle (preview notebook pages leave/return with their workspace) */
void diff_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void diff_tab_workspace_shown(AppData* app, VirtualLayout* ws);
void diff_tab_workspace_removed(AppData* app, VirtualLayout* ws);

/* Free app-level state (selected compare path) at exit */
void diff_tab_free(AppData* app);

#endif /* DIFF_TAB_H */
