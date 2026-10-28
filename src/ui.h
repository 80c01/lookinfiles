#ifndef UI_H
#define UI_H

#include "app_types.h"

void wire_focus_tracking(AppData* app);
void focus_adjacent_panel(gint step, AppData* app);

gboolean on_window_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

void layout_save_frac(AppData* app);
gboolean layout_position_idle(gpointer data);
void layout_build(AppData* app, GtkOrientation orientation);
void layout_toggle(AppData* app);

#endif /* UI_H */
