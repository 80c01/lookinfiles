#ifndef TOPBAR_H
#define TOPBAR_H

/* Top bar breadcrumbs and toggle pills */
#include "core/app_types.h"

/* Rebuild the top bar from AppData: single source of truth for directory/sort/hidden state */
void update_path_label(AppData* app);

/* The top bar row: folder history arrows, breadcrumbs, app_notice() message,
 * global mode pill, then ws_minimap, layouts_btn and prefs_btn, then the
 * panel pills and Help. Its widgets stay private to topbar.c. */
GtkWidget* topbar_new(AppData* app, GtkWidget* ws_minimap, GtkWidget* layouts_btn, GtkWidget* prefs_btn);
/* Action button of the preview notebook: move the editor tabs to the
 * dedicated panel, or dock them back */
GtkWidget* topbar_split_button_new(AppData* app);
/* Forget the top bar's widgets (at exit) */
/* Restart the top bar timer with minutes (0 hides it) */
void topbar_set_timer(AppData* app, gint minutes);

void topbar_free(AppData* app);

/* Show / hide the explorer and the preview panels (pills, keys) */
void on_files_pill_clicked(GtkButton* btn, gpointer data);
void on_preview_pill_clicked(GtkButton* btn, gpointer data);

#endif /* TOPBAR_H */
