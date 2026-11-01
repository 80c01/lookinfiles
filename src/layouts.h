#ifndef LAYOUTS_H
#define LAYOUTS_H

/* Saved workspace layouts: save the current workspace's panel arrangement
 * under a name and apply a saved one, from the Edit Layouts dialog inside
 * the window (the Alt+L switcher is layout_switcher.h). Data and file
 * format: layout_store.h. */
#include "app_types.h"
#include "layout_store.h"

/* The Edit Layouts dialog: apply, save, delete, reset the default */
void layouts_show_dialog(AppData* app);

/* The current workspace's panels, editor splits and terminals. Caller frees. */
SavedLayout* layouts_capture(AppData* app, const gchar* name);
/* Apply a layout to the current workspace, asking before closing terminals.
 * FALSE if the user cancelled. */
gboolean layouts_apply(AppData* app, const SavedLayout* layout);
/* Show only the panels of a layout (visibility, orientation, split sizes):
 * terminals and editor tabs stay as they are */
void layouts_preview(AppData* app, const SavedLayout* layout);

/* A list row: the title above the layout's summary */
GtkWidget* layouts_row_new(const gchar* title, const SavedLayout* layout);
/* Title of the default layout's row. Caller frees. */
gchar* layouts_default_title(const SavedLayout* def);

#endif /* LAYOUTS_H */
