#ifndef LAYOUT_HISTORY_H
#define LAYOUT_HISTORY_H

/* Per-workspace layout history (back / forward buttons beside the top-bar
 * layout button, Alt+Shift+Left / Alt+Shift+Right). Records panel visibility,
 * orientation, swaps, split sizes, editor/terminal splits, tab pane moves and
 * tagbar changes once settled; stepping back or forward restores the exact
 * arrangement without closing running terminals. */
#include "core/app_types.h"

/* Enable recording after startup finishes and seed the current workspace */
void layout_history_init(AppData* app);
/* Stop any pending timer and free every workspace's history (at exit) */
void layout_history_free(AppData* app);

/* APP_EVENT_LAYOUT_CHANGED / APP_EVENT_LAYOUT_REBUILT subscriber */
void layout_history_on_change(AppData* app);

/* Step back / forward in the current workspace's layout history */
void layout_history_back(AppData* app);
void layout_history_forward(AppData* app);
/* Whether the current workspace can step back (back == TRUE) or forward */
gboolean layout_history_can_go(AppData* app, gboolean back);

/* Flush any pending change before leaving the current workspace */
void layout_history_flush(AppData* app);
/* Another workspace was shown: seed its history if empty and refresh buttons */
void layout_history_workspace_changed(AppData* app);
/* Reset the current workspace's history to a single initial entry (new workspace) */
void layout_history_reset_current(AppData* app);
/* Free the history of a workspace being removed */
void layout_history_workspace_removed(VirtualLayout* ws);

#endif /* LAYOUT_HISTORY_H */
