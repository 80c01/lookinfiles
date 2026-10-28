#ifndef STATUS_BAR_H
#define STATUS_BAR_H

/* Bottom status bar */
#include "app_types.h"

GtkWidget* create_status_bar(AppData* app);
void update_status_bar(AppData* app);

#endif /* STATUS_BAR_H */
