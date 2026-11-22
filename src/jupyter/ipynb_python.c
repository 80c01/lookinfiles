/* The Python of a notebook's Jupyter bridge (see ipynb_python.h). */
#include "jupyter/ipynb_python.h"

#include <string.h>

#define MAX_LEVELS 64 /* folders looked at from the notebook's up */

static const gchar* const VENV_NAMES[] = { ".venv", "venv", "env", NULL };

#ifdef G_OS_WIN32
#define VENV_BIN "Scripts"
#define VENV_PYTHON "python.exe"
#else
#define VENV_BIN "bin"
#define VENV_PYTHON "python"
#endif

gchar* ipynb_python_venv_python(const gchar* venv) {
    return g_build_filename(venv, VENV_BIN, VENV_PYTHON, NULL);
}

/* A virtual environment: its pyvenv.cfg and its Python */
static gboolean is_venv(const gchar* path) {
    gchar* cfg = g_build_filename(path, "pyvenv.cfg", NULL);
    gchar* python = ipynb_python_venv_python(path);
    gboolean yes = g_file_test(cfg, G_FILE_TEST_IS_REGULAR) && g_file_test(python, G_FILE_TEST_IS_EXECUTABLE);
    g_free(cfg);
    g_free(python);
    return yes;
}

/* The search stops after dir: the top of a repository, home or the root */
static gboolean is_last(const gchar* dir, const gchar* home) {
    gchar* git = g_build_filename(dir, ".git", NULL);
    gboolean top = g_file_test(git, G_FILE_TEST_EXISTS);
    g_free(git);
    return top || g_strcmp0(dir, home) == 0;
}

gchar* ipynb_python_find_venv(const gchar* dir) {
    if (dir == NULL || dir[0] == '\0') return NULL;
    gchar* home = g_canonicalize_filename(g_get_home_dir(), NULL);
    gchar* d = g_canonicalize_filename(dir, NULL);
    gchar* found = NULL;
    for (guint level = 0; found == NULL && level < MAX_LEVELS; level++) {
        for (const gchar* const* n = VENV_NAMES; *n != NULL && found == NULL; n++) {
            gchar* path = g_build_filename(d, *n, NULL);
            if (is_venv(path)) {
                found = path;
            } else {
                g_free(path);
            }
        }
        if (found != NULL || is_last(d, home)) break;
        gchar* up = g_path_get_dirname(d);
        gboolean root = strcmp(up, d) == 0;
        g_free(d);
        d = up;
        if (root) break; /* the root was the last one looked at */
    }
    g_free(d);
    g_free(home);
    return found;
}

static gboolean has_jupyter_in(const gchar* site) {
    gchar* path = g_build_filename(site, "jupyter_client", NULL);
    gboolean yes = g_file_test(path, G_FILE_TEST_IS_DIR);
    g_free(path);
    return yes;
}

gboolean ipynb_python_venv_has_jupyter(const gchar* venv) {
    /* Windows: Lib\site-packages; elsewhere lib/pythonX.Y/site-packages */
    gchar* site = g_build_filename(venv, "Lib", "site-packages", NULL);
    gboolean yes = has_jupyter_in(site);
    g_free(site);
    gchar* lib = g_build_filename(venv, "lib", NULL);
    GDir* d = yes ? NULL : g_dir_open(lib, 0, NULL);
    const gchar* name;
    while (d != NULL && !yes && (name = g_dir_read_name(d)) != NULL) {
        if (!g_str_has_prefix(name, "python")) continue;
        site = g_build_filename(lib, name, "site-packages", NULL);
        yes = has_jupyter_in(site);
        g_free(site);
    }
    if (d != NULL) g_dir_close(d);
    g_free(lib);
    return yes;
}

gchar** ipynb_python_venv_environ(const gchar* venv, gchar** envp) {
    if (envp == NULL) envp = g_get_environ();
    gchar* bin = g_build_filename(venv, VENV_BIN, NULL);
    const gchar* path = g_environ_getenv(envp, "PATH");
    gchar* new_path = path != NULL && path[0] != '\0' ? g_strjoin(G_SEARCHPATH_SEPARATOR_S, bin, path, NULL)
                                                      : g_strdup(bin);
    envp = g_environ_setenv(envp, "VIRTUAL_ENV", venv, TRUE);
    envp = g_environ_setenv(envp, "PATH", new_path, TRUE);
    envp = g_environ_unsetenv(envp, "PYTHONHOME");
    g_free(new_path);
    g_free(bin);
    return envp;
}

gchar* ipynb_python_local(const gchar* dir, gchar*** envp) {
    *envp = NULL;
    gchar* venv = ipynb_python_find_venv(dir);
    if (venv != NULL && ipynb_python_venv_has_jupyter(venv)) {
        *envp = ipynb_python_venv_environ(venv, NULL);
        gchar* python = ipynb_python_venv_python(venv);
        g_free(venv);
        return python;
    }
    if (venv != NULL) *envp = g_environ_setenv(g_get_environ(), IPYNB_PYTHON_VENV_VAR, venv, TRUE);
    g_free(venv);
    gchar* python = g_find_program_in_path("python3");
    if (python == NULL) python = g_find_program_in_path("python");
    return python;
}

gchar* ipynb_python_remote_command(const gchar* script, const gchar* dir) {
    gchar* quoted = g_shell_quote(script != NULL ? script : "");
    gchar* start = g_shell_quote(dir != NULL ? dir : "");
    /* v: the virtual environment (as ipynb_python_find_venv()), p: its
     * Python when it has Jupyter */
    gchar* line = g_strconcat(
        "d=", start, "; v=; p=; i=0; "
        "while [ -n \"$d\" ] && [ $i -lt 64 ]; do "
        "for n in .venv venv env; do "
        "if [ -f \"$d/$n/pyvenv.cfg\" ] && [ -x \"$d/$n/bin/python\" ]; then v=$d/$n; break 2; fi; "
        "done; "
        "if [ -e \"$d/.git\" ] || [ \"$d\" = \"$HOME\" ] || [ \"$d\" = / ]; then break; fi; "
        "d=$(dirname \"$d\"); i=$((i + 1)); "
        "done; "
        "if [ -n \"$v\" ]; then for j in \"$v\"/lib/python*/site-packages/jupyter_client; do "
        "[ -d \"$j\" ] && p=$v/bin/python; done; fi; "
        "if [ -n \"$p\" ]; then "
        "VIRTUAL_ENV=$v; PATH=$v/bin:$PATH; export VIRTUAL_ENV PATH; unset PYTHONHOME; "
        "else "
        "if [ -n \"$v\" ]; then " IPYNB_PYTHON_VENV_VAR "=$v; export " IPYNB_PYTHON_VENV_VAR "; fi; "
        "p=$(command -v python3 || command -v python) || "
        "{ echo '{\"ev\": \"error\", \"code\": \"no-python\", \"message\": \"python3 not found\"}'; exit 127; }; "
        "fi; "
        "exec \"$p\" -c ", quoted, NULL);
    g_free(start);
    g_free(quoted);
    return line;
}
