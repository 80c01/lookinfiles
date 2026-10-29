#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <glib.h>

/* Application name and the per-user directories it keeps its data in. */

#define APP_NAME     "Lookin"
#define APP_TAGLINE  "look in your files"
#define APP_DIR_NAME "lookin"

/* Directory name used before the application was renamed to Lookin */
#define APP_LEGACY_DIR_NAME "explorateur_fichiers"

/* ~/.config/lookin ($XDG_CONFIG_HOME honoured). Not created. Caller frees. */
gchar* app_config_dir(void);

/* ~/.cache/lookin ($XDG_CACHE_HOME honoured). Not created. Caller frees. */
gchar* app_cache_dir(void);

/* Move <base_dir>/explorateur_fichiers to <base_dir>/lookin when only the
 * old directory exists. Returns TRUE if it was moved. */
gboolean app_paths_migrate_legacy_in(const gchar* base_dir);

/* app_paths_migrate_legacy_in() on the user config directory. Call once at
 * startup, before anything reads or creates the config directory. */
gboolean app_paths_migrate_legacy(void);

#endif
