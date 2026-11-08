#ifndef UI_H
#define UI_H

#include "core/app_types.h"

void wire_focus_tracking(AppData* app);
void focus_adjacent_panel(gint step, AppData* app);
/* APP_EVENT_FOCUS_NEXT_PANEL / APP_EVENT_FOCUS_PREV_PANEL subscribers */
void focus_next_panel(AppData* app);
void focus_prev_panel(AppData* app);
/* Close the focused editor tab, else the current one if it is on screen
 * (never a tab hidden behind Git Commits or References) */
void close_shown_tab(AppData* app);

/* Before quitting (Ctrl+Q, window closed): confirm unsaved changes of every
 * workspace, then save canvases and the session. FALSE when the user cancels. */
gboolean ui_confirm_quit(AppData* app);

gboolean on_window_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

gboolean layout_position_idle(gpointer data);
void layout_build(AppData* app, GtkOrientation orientation);
void layout_toggle(AppData* app);

#endif /* UI_H */
