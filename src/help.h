#ifndef HELP_H
#define HELP_H

/* Floating key-binding cheat sheet */
#include "app_types.h"

void create_help_panel(GtkWindow* parent, AppData* app);
void toggle_help_panel(AppData* app);
gboolean on_help_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

#endif /* HELP_H */
