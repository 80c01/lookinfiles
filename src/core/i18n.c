#include "core/i18n.h"

#include <locale.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* Install directory of the catalogs, set by the Makefile from PREFIX */
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

gchar* i18n_locale_dir(const char* exe_dir, const char* fallback) {
    const char* env = g_getenv("LOOKINFILES_LOCALEDIR");
    if (env != NULL && *env != '\0') {
        return g_strdup(env);
    }
    if (exe_dir != NULL) {
        gchar* candidates[] = {
            g_build_filename(exe_dir, "build", "locale", NULL),
            g_build_filename(exe_dir, "share", "locale", NULL),
            g_build_filename(exe_dir, "..", "share", "locale", NULL),
        };
        gchar* found = NULL;
        for (gsize i = 0; i < G_N_ELEMENTS(candidates); i++) {
            if (found == NULL && g_file_test(candidates[i], G_FILE_TEST_IS_DIR)) {
                found = candidates[i];
            } else {
                g_free(candidates[i]);
            }
        }
        if (found != NULL) {
            return found;
        }
    }
    return g_strdup(fallback);
}

/* Directory of the running executable, or NULL if it cannot be found */
static gchar* executable_dir(const char* argv0) {
    gchar* exe = NULL;
#ifdef G_OS_WIN32
    wchar_t wpath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, wpath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        exe = g_utf16_to_utf8((const gunichar2*)wpath, n, NULL, NULL, NULL);
    }
#else
    exe = g_file_read_link("/proc/self/exe", NULL);
#endif
    if (exe == NULL && argv0 != NULL) {
        exe = g_find_program_in_path(argv0);
    }
    if (exe == NULL) {
        return NULL;
    }
    gchar* dir = g_path_get_dirname(exe);
    g_free(exe);
    return dir;
}

void i18n_init(const char* argv0) {
    setlocale(LC_ALL, "");
    gchar* exe_dir = executable_dir(argv0);
    gchar* dir = i18n_locale_dir(exe_dir, LOCALEDIR);
    bindtextdomain(GETTEXT_PACKAGE, dir);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_free(dir);
    g_free(exe_dir);
}
