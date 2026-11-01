#ifndef PREFERENCES_H
#define PREFERENCES_H

/* The Preferences dialog (Ctrl+, and the top-bar gear button; Alt+T opens
 * it on Appearance): sections listed on the left, Apply keeps every change,
 * Cancel restores them all.
 *
 *   Appearance       themes and fonts (themes.h), previewed live
 *   Editor           mode, line numbers and wrap of new editor tabs
 *   Window           window size at startup
 *   Code navigation  lsp.ini opened in an editor tab
 *
 * The values are kept in settings.ini (settings_store.h); command-line
 * options win over them. */
#include "app_types.h"
#include "settings_store.h"

#define PREFERENCES_APPEARANCE "appearance"

/* Show the dialog on section (PREFERENCES_APPEARANCE...), or on the last
 * section shown when NULL */
void preferences_show(AppData* app, const gchar* section);

/* Editor mode of a settings value */
EditorMode preferences_editor_mode(SettingsEditor editor);

/* At startup, once app exists: the editor settings of new tabs */
void preferences_apply_startup(AppData* app, const Settings* settings);

#endif /* PREFERENCES_H */
