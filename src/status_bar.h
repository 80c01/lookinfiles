#ifndef STATUS_BAR_H
#define STATUS_BAR_H

/* Status bars: explorer bar (Git branch, messages) and bottom bar */
#include "app_types.h"

/* Bar at the bottom of the explorer panel: Git branch of the current
 * directory (files view) and the app_notice() message with its Undo */
GtkWidget* create_explorer_bar(AppData* app);
/* Where the app_notice() message goes in the top bar while the explorer
 * panel is hidden */
GtkWidget* create_topbar_notice(AppData* app);
GtkWidget* create_status_bar(AppData* app);
void update_status_bar(AppData* app);

#endif /* STATUS_BAR_H */
