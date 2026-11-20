#ifndef CODE_PROBLEMS_H
#define CODE_PROBLEMS_H

/* Problems panel (LSP diagnostics across open/analyzed files) and next /
 * previous diagnostic stepping in an editor tab (docs/code-nav.md). */
#include "core/app_types.h"
#include "code/code_nav.h"

/* Open and focus the Problems tab in the preview notebook */
void code_problems_show(AppData* app, EditorTab* tab);

/* Refresh the Problems tab if it is open (subscribed to
 * APP_EVENT_DIAGNOSTICS_CHANGED in main.c) */
void code_problems_refresh(AppData* app);

/* Rebuild the Problems tab if the workspace folder changed since it was
 * built (subscribed to APP_EVENT_CHROME_CHANGED in main.c): it lists the
 * diagnostics of the files under that folder or open in the workspace */
void code_problems_folder_changed(AppData* app);

/* Each workspace has its own Problems tab: set it aside in ws when ws is
 * hidden, put it back (refreshed) when ws is shown, drop it with ws */
void code_problems_workspace_hidden(AppData* app, VirtualLayout* ws);
void code_problems_workspace_shown(AppData* app, VirtualLayout* ws);
void code_problems_workspace_removed(AppData* app, VirtualLayout* ws);

/* Close the Problems tab if it is the current page of app->notebook (Ctrl+W).
 * Returns TRUE when closed. */
gboolean code_problems_close_shown(AppData* app);

/* Jump to the next (forward == TRUE) or previous diagnostic in tab and report
 * its message via message(). Returns TRUE if a diagnostic was jumped to. */
gboolean code_problems_step(AppData* app, EditorTab* tab, gboolean forward, CodeNavMessageFunc message);

#endif /* CODE_PROBLEMS_H */
