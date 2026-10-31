#ifndef EXPLORER_KEYS_H
#define EXPLORER_KEYS_H

/* Keys of the explorer panel: navigation of the global mode (nav_keys.h),
 * then the view actions (the same in every mode) */
#include "app_types.h"

gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data);

#endif /* EXPLORER_KEYS_H */
