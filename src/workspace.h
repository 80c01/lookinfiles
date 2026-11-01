#ifndef WORKSPACE_H
#define WORKSPACE_H

/* Virtual workspaces and top-bar workspace minimap */
#include "app_types.h"

void workspaces_init(AppData* app);
void workspace_save_current(AppData* app);
void workspace_apply_positions(AppData* app);
void workspace_switch(AppData* app, gint idx);
/* Show the panels the current workspace records (visibility, orientation,
 * split sizes) without switching it: no rescan, no focus change */
void workspace_show_panels(AppData* app);
void workspace_add(AppData* app);
/* Startup: give the current workspace the panels of the default layout
 * (layouts.ini) when it has been customized */
void workspace_apply_default_layout(AppData* app);
void workspace_remove(AppData* app, gint idx);
/* Session restore: give workspace idx this folder, expanded folders,
 * selection, hidden files and sort, and show it */
void workspace_restore_state(AppData* app, gint idx, const gchar* dir, const gchar* const* expanded,
                             const gchar* selected, gboolean show_hidden, gint sort_mode);
GtkWidget* create_workspace_minimap(AppData* app);

#endif /* WORKSPACE_H */
