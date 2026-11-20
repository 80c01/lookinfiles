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

/* Close the Problems tab if it is the current page of app->notebook (Ctrl+W).
 * Returns TRUE when closed. */
gboolean code_problems_close_shown(AppData* app);

/* Jump to the next (forward == TRUE) or previous diagnostic in tab and report
 * its message via message(). Returns TRUE if a diagnostic was jumped to. */
gboolean code_problems_step(AppData* app, EditorTab* tab, gboolean forward, CodeNavMessageFunc message);

#endif /* CODE_PROBLEMS_H */
