/* Where the optional modules loaded at run time are (the WebKit browser
 * engine, web_engine.c): a module is a shared library the application opens
 * only when a feature needs it, so its libraries cost nothing before. */
#ifndef MODULE_PATH_H
#define MODULE_PATH_H

#include <glib.h>

/* Install directory of the modules, set by the Makefile from PREFIX */
#ifndef MODULEDIR
#define MODULEDIR "/usr/local/lib/lookinfiles"
#endif

/* The path of the module file, the first that exists of:
 * $LOOKINFILES_MODULEDIR/file, <exe_dir>/file (next to the program),
 * <exe_dir>/build/file (source tree), <exe_dir>/../lib/lookinfiles/file
 * (relocated install, AppImage), <fallback_dir>/file. exe_dir and
 * fallback_dir may be NULL. NULL when none exists. Caller frees. */
gchar* module_path_find(const gchar* exe_dir, const gchar* fallback_dir, const gchar* file);

/* Directory of the running program (/proc/self/exe), NULL when unknown.
 * Caller frees. */
gchar* module_path_exe_dir(void);

#endif
