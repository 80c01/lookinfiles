#ifndef PREVIEW_MENU_H
#define PREVIEW_MENU_H

/* The right-click menu of the Preview tab title: Copy Name, Copy Relative
 * Path and Copy Path of the previewed file (path_copy), Copy Value Path (the
 * value path at the cursor, when there is one), Tagbar (only when the
 * preview has headings or symbols) and Minimap toggles, for the current
 * workspace only (not saved: Preferences → Editor sets every workspace) */
#include "core/app_types.h"

/* Connect the menu to the title of the Preview tab (page 0 of app->notebook) */
void preview_menu_attach(AppData* app);

#endif /* PREVIEW_MENU_H */
