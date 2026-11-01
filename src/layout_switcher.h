#ifndef LAYOUT_SWITCHER_H
#define LAYOUT_SWITCHER_H

/* Alt+L layout switcher: a popover under the top-bar layouts button that
 * lists the current panels, the default layout and the saved ones. Moving
 * through the list previews each layout's panels at once (terminals and
 * editor tabs are left alone); Enter applies the layout in full, Escape
 * puts the panels back. Edit Layouts… opens the dialog of layouts.h. */
#include "app_types.h"

/* Open the switcher; when it is already open, select the next layout */
void layout_switcher_show(AppData* app);

#endif /* LAYOUT_SWITCHER_H */
