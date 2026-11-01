#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <glib.h>

/* Application name and the per-user directories it keeps its data in. */

#define APP_NAME     "LookinFiles"
#define APP_TAGLINE  "look in your files"
#define APP_DIR_NAME "lookinfiles"

/* Application ID: names the .desktop file, the metainfo and the icon (data/) */
#define APP_ID       "io.github._80c01.LookinFiles"

/* Release version (Semantic Versioning); the Makefile reads it from here */
#define APP_VERSION  "0.1.0"

/* ~/.config/lookinfiles ($XDG_CONFIG_HOME honoured). Not created. Caller frees. */
gchar* app_config_dir(void);

/* ~/.cache/lookinfiles ($XDG_CACHE_HOME honoured). Not created. Caller frees. */
gchar* app_cache_dir(void);

#endif
