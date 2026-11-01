#ifndef LAYOUT_SWITCHER_H
#define LAYOUT_SWITCHER_H

/* Alt+L layout switcher: a popover under the top-bar layouts button that
 * lists the current arrangement, the default layout and the saved ones.
 * Moving through the list shows each layout in full (layouts_preview_*:
 * nothing closes meanwhile); Enter applies it, Escape puts everything back
 * exactly. Edit Layouts… opens the dialog of layouts.h. */
#include "app_types.h"

/* Open the switcher; when it is already open, select the next layout */
void layout_switcher_show(AppData* app);

#endif /* LAYOUT_SWITCHER_H */
