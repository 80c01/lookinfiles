#ifndef PANELS_H
#define PANELS_H

/* Panel helpers shared by features and window chrome */
#include "core/app_types.h"
#include "explorer/nav_keys.h"

/* Navigation keys of the explorer and viewer panels (nav_keys.h): those of the
 * global mode (status bar mode menu with no editor tab focused, -e / --editor) */
NavStyle app_nav_style(AppData* app);

gboolean window_is_fullscreen(GtkWindow* win);
void toggle_fullscreen(GtkWindow* window);

/* Move the "panel-focused" CSS highlight to the panel owning keyboard focus;
 * TRUE when that panel changed */
gboolean update_active_panel_highlight(AppData* app);

/* Usable width of the workspace area (falls back to window / main paned width) */
gint get_workspace_total_width(AppData* app);

/* Split handles: size popup while dragging, and split fractions recorded into the
 * current workspace (emits APP_EVENT_LAYOUT_CHANGED). Call wire_paned_controllers()
 * on every GtkPaned of the layout. */
void wire_paned_controllers(GtkWidget* paned, AppData* app);
/* Limit the separator grab zone to a few pixels (done by wire_paned_controllers) */
void paned_use_thin_handle(GtkWidget* paned);
/* Add the size popup shown while a split handle is dragged over overlay
 * (once, when the window is built) */
void panels_size_popup_attach(AppData* app, GtkOverlay* overlay);
/* Free the focused-panel and split-handle drag state (at exit) */
void panels_free(AppData* app);

#endif /* PANELS_H */
