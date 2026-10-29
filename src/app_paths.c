#include "app_paths.h"

#include <errno.h>
#include <glib/gstdio.h>

gchar* app_config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), APP_DIR_NAME, NULL);
}

gchar* app_cache_dir(void) {
    return g_build_filename(g_get_user_cache_dir(), APP_DIR_NAME, NULL);
}

gboolean app_paths_migrate_legacy_in(const gchar* base_dir) {
    if (base_dir == NULL) {
        return FALSE;
    }
    gchar* old_dir = g_build_filename(base_dir, APP_LEGACY_DIR_NAME, NULL);
    gchar* new_dir = g_build_filename(base_dir, APP_DIR_NAME, NULL);
    gboolean moved = FALSE;
    if (g_file_test(old_dir, G_FILE_TEST_IS_DIR) &&
        !g_file_test(new_dir, G_FILE_TEST_EXISTS)) {
        if (g_rename(old_dir, new_dir) == 0) {
            moved = TRUE;
        } else {
            g_printerr("[ Could not move %s to %s: %s ]\n",
                       old_dir, new_dir, g_strerror(errno));
        }
    }
    g_free(old_dir);
    g_free(new_dir);
    return moved;
}

gboolean app_paths_migrate_legacy(void) {
    return app_paths_migrate_legacy_in(g_get_user_config_dir());
}
