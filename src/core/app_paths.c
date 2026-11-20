#include "core/app_paths.h"

#include <glib/gstdio.h>
#include <errno.h>

#ifdef G_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

gchar* app_config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), APP_DIR_NAME, NULL);
}

gchar* app_cache_dir(void) {
    return g_build_filename(g_get_user_cache_dir(), APP_DIR_NAME, NULL);
}

#ifdef G_OS_UNIX
/* dir exists (created 0700 if missing), is a real directory, ours, and
 * closed to the other users */
static gboolean private_dir(const gchar* dir) {
    if (g_mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return FALSE;
    }
    GStatBuf st;
    return g_lstat(dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid() &&
           (st.st_mode & 077) == 0;
}
#endif

gchar* app_runtime_dir_for(const gchar* xdg_runtime_dir, const gchar* tmp_dir) {
#ifdef G_OS_UNIX
    gchar* dir = NULL;
    if (xdg_runtime_dir != NULL && g_path_is_absolute(xdg_runtime_dir) &&
        g_file_test(xdg_runtime_dir, G_FILE_TEST_IS_DIR)) {
        dir = g_build_filename(xdg_runtime_dir, APP_DIR_NAME, NULL);
    } else if (tmp_dir != NULL && g_path_is_absolute(tmp_dir)) {
        /* Not g_get_user_runtime_dir(): its fallback is the cache directory */
        gchar* name = g_strdup_printf("%s-%u", APP_DIR_NAME, (guint)getuid());
        dir = g_build_filename(tmp_dir, name, NULL);
        g_free(name);
    }
    if (dir != NULL && !private_dir(dir)) {
        g_clear_pointer(&dir, g_free);
    }
    return dir;
#else
    (void)xdg_runtime_dir;
    (void)tmp_dir;
    gchar* dir = g_build_filename(g_get_user_runtime_dir(), APP_DIR_NAME, NULL);
    g_mkdir_with_parents(dir, 0700);
    return dir;
#endif
}

gchar* app_runtime_dir(void) {
    return app_runtime_dir_for(g_getenv("XDG_RUNTIME_DIR"), g_get_tmp_dir());
}
