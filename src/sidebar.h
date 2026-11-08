#ifndef SIDEBAR_H
#define SIDEBAR_H

/* Explorer panel header and per-view action toolbars */
#include "app_types.h"

GtkWidget* create_sidebar_header(AppData* app);
/* Show the current view in the header (title, active switch, its action bar,
 * hidden files toggle, folder section); FALSE before create_sidebar_header() */
gboolean sidebar_header_update(AppData* app);
/* Forget the header's widgets (at exit) */
void sidebar_free(AppData* app);

#endif /* SIDEBAR_H */
