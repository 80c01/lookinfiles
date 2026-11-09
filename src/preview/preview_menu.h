#ifndef PREVIEW_MENU_H
#define PREVIEW_MENU_H

/* The right-click menu of the preview: after the items already there (Copy
 * Image), Show / Hide Tagbar (only when the preview has headings or
 * symbols) and Show / Hide Minimap, for the current workspace only (not
 * saved: Preferences → Editor sets every workspace) */
#include "core/app_types.h"

/* Add the items to the extra menu of text_view (the preview's text view) */
void preview_menu_attach(AppData* app, GtkWidget* text_view);

#endif /* PREVIEW_MENU_H */
