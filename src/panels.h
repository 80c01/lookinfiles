#ifndef PANELS_H
#define PANELS_H

/* Panel helpers shared by features and window chrome */
#include "app_types.h"

gboolean window_is_fullscreen(GtkWindow* win);
void toggle_fullscreen(GtkWindow* window);

/* Move the "panel-focused" CSS highlight to the panel owning keyboard focus */
void update_active_panel_highlight(AppData* app);

/* Usable width of the workspace area (falls back to window / main paned width) */
gint get_workspace_total_width(AppData* app);

/* Split handles: size popup while dragging, and split fractions recorded into the
 * current workspace (emits APP_EVENT_LAYOUT_CHANGED). Call wire_paned_controllers()
 * on every GtkPaned of the layout. */
void wire_paned_controllers(GtkWidget* paned, AppData* app);
/* Limit the separator grab zone to a few pixels (done by wire_paned_controllers) */
void paned_use_thin_handle(GtkWidget* paned);
void update_pane_dims(GtkWidget* paned_widget, AppData* app);
gboolean pane_dims_idle(gpointer data);
void on_paned_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);
void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data);
void on_paned_released(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);

#endif /* PANELS_H */
