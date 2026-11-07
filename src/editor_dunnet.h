#ifndef EDITOR_DUNNET_H
#define EDITOR_DUNNET_H

#include "app_types.h"

/* Open (or focus / restart) the Emacs *dungeon* buffer tab running Dunnet. */
EditorTab* editor_dunnet_open(AppData* app);

/* TRUE when tab is a *dungeon* buffer with an active Dunnet session. */
gboolean editor_dunnet_is_dungeon(const EditorTab* tab);

/* Handle a key pressed in a *dungeon* buffer tab; TRUE when consumed. */
gboolean editor_dunnet_handle_key(EditorTab* tab, guint keyval, GdkModifierType state);

#endif /* EDITOR_DUNNET_H */
