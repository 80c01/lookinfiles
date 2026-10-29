#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <glib.h>

/* Application name and the per-user directories it keeps its data in. */

#define APP_NAME     "Lookin"
#define APP_TAGLINE  "look in your files"
#define APP_DIR_NAME "lookin"

/* ~/.config/lookin ($XDG_CONFIG_HOME honoured). Not created. Caller frees. */
gchar* app_config_dir(void);

/* ~/.cache/lookin ($XDG_CACHE_HOME honoured). Not created. Caller frees. */
gchar* app_cache_dir(void);

#endif
