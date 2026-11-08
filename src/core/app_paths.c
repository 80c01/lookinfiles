#include "core/app_paths.h"

gchar* app_config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), APP_DIR_NAME, NULL);
}

gchar* app_cache_dir(void) {
    return g_build_filename(g_get_user_cache_dir(), APP_DIR_NAME, NULL);
}
