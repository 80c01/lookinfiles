#ifndef LAYOUTS_H
#define LAYOUTS_H

/* Saved workspace layouts (Alt+L): save the current workspace's panel
 * arrangement under a name and apply a saved one from a list, in a dialog
 * inside the window. Data and file format: layout_store.h. */
#include "app_types.h"

void layouts_show_dialog(AppData* app);

#endif /* LAYOUTS_H */
