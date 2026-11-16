/* Where the optional modules are (see module_path.h) */
#include "core/module_path.h"

gchar* module_path_find(const gchar* exe_dir, const gchar* fallback_dir, const gchar* file) {
    if (file == NULL || file[0] == '\0') {
        return NULL;
    }
    const gchar* env = g_getenv("LOOKINFILES_MODULEDIR");
    gchar* candidates[5] = {NULL};
    gsize n = 0;
    if (env != NULL && env[0] != '\0') {
        candidates[n++] = g_build_filename(env, file, NULL);
    }
    if (exe_dir != NULL) {
        candidates[n++] = g_build_filename(exe_dir, file, NULL);
        candidates[n++] = g_build_filename(exe_dir, "build", file, NULL);
        candidates[n++] = g_build_filename(exe_dir, "..", "lib", "lookinfiles", file, NULL);
    }
    if (fallback_dir != NULL) {
        candidates[n++] = g_build_filename(fallback_dir, file, NULL);
    }
    gchar* found = NULL;
    for (gsize i = 0; i < n; i++) {
        if (found == NULL && g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
            found = candidates[i];
        } else {
            g_free(candidates[i]);
        }
    }
    return found;
}

gchar* module_path_exe_dir(void) {
    gchar* exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe == NULL) {
        return NULL;
    }
    gchar* dir = g_path_get_dirname(exe);
    g_free(exe);
    return dir;
}
