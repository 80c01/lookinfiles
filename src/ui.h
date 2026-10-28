#ifndef UI_H
#define UI_H

#include "app_types.h"

gboolean window_is_fullscreen(GtkWindow* win);
void toggle_fullscreen(GtkWindow* window);

void update_active_panel_highlight(AppData* app);
void wire_focus_tracking(AppData* app);
void focus_adjacent_panel(gint step, AppData* app);

/* Usable width of the workspace area (falls back to window / main paned width) */
gint get_workspace_total_width(AppData* app);
gboolean on_window_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

void update_pane_dims(GtkWidget* paned_widget, AppData* app);
gboolean pane_dims_idle(gpointer data);
void on_paned_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);
void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data);
void on_paned_released(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);
void wire_paned_controllers(GtkWidget* paned, AppData* app);

void layout_save_frac(AppData* app);
gboolean layout_position_idle(gpointer data);
void layout_build(AppData* app, GtkOrientation orientation);
void layout_toggle(AppData* app);

#endif /* UI_H */
