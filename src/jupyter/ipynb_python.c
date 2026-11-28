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

gchar* ipynb_python_venv_of(const gchar* python) {
    if (python == NULL || python[0] == '\0') return NULL;
    gchar* bin = g_path_get_dirname(python);
    gchar* venv = g_path_get_dirname(bin);
    g_free(bin);
    gchar* cfg = g_build_filename(venv, "pyvenv.cfg", NULL);
    gboolean yes = g_file_test(cfg, G_FILE_TEST_IS_REGULAR);
    g_free(cfg);
    if (!yes) g_clear_pointer(&venv, g_free);
    return venv;
}

gchar* ipynb_python_local(const gchar* dir, const gchar* chosen, gchar*** envp) {
    *envp = NULL;
    if (chosen != NULL && chosen[0] != '\0') {
        gchar* python = g_str_has_prefix(chosen, "~/") ? g_build_filename(g_get_home_dir(), chosen + 2, NULL)
                                                       : g_strdup(chosen);
        gchar* venv = ipynb_python_venv_of(python);
        if (venv != NULL) *envp = ipynb_python_venv_environ(venv, NULL);
        g_free(venv);
        return python;
    }
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

#ifdef G_OS_WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

static void add_found(GPtrArray* out, gchar* python) {
    gboolean ok = g_file_test(python, G_FILE_TEST_IS_EXECUTABLE) && !g_file_test(python, G_FILE_TEST_IS_DIR);
    for (guint i = 0; ok && i < out->len; i++) ok = strcmp(g_ptr_array_index(out, i), python) != 0;
    if (ok) {
        g_ptr_array_add(out, python);
    } else {
        g_free(python);
    }
}

static gint compare_newest(gconstpointer a, gconstpointer b) {
    gchar* x = g_utf8_collate_key_for_filename(*(const gchar* const*)a, -1);
    gchar* y = g_utf8_collate_key_for_filename(*(const gchar* const*)b, -1);
    gint c = strcmp(y, x);
    g_free(x);
    g_free(y);
    return c;
}

/* The Python of each folder of parent (sub/bin/name), newest first */
static void add_each(GPtrArray* out, const gchar* parent, const gchar* name) {
    GDir* d = g_dir_open(parent, 0, NULL);
    if (d == NULL) return;
    GPtrArray* subs = g_ptr_array_new_with_free_func(g_free);
    const gchar* sub;
    while ((sub = g_dir_read_name(d)) != NULL) {
        if (sub[0] != '.') g_ptr_array_add(subs, g_strdup(sub));
    }
    g_dir_close(d);
    g_ptr_array_sort(subs, compare_newest);
    for (guint i = 0; i < subs->len; i++) {
        add_found(out, g_build_filename(parent, g_ptr_array_index(subs, i), VENV_BIN, name, NULL));
    }
    g_ptr_array_unref(subs);
}

/* The first program called name in the folders of path */
static void add_in_path(GPtrArray* out, const gchar* path, const gchar* name) {
    gchar** dirs = g_strsplit(path, G_SEARCHPATH_SEPARATOR_S, -1);
    for (gchar** d = dirs; *d != NULL; d++) {
        if (**d == '\0') continue;
        gchar* python = g_build_filename(*d, name, NULL);
        if (g_file_test(python, G_FILE_TEST_IS_EXECUTABLE) && !g_file_test(python, G_FILE_TEST_IS_DIR)) {
            add_found(out, python);
            break;
        }
        g_free(python);
    }
    g_strfreev(dirs);
}

GPtrArray* ipynb_python_found(const gchar* dir, const gchar* home, const gchar* path) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    gchar* venv = ipynb_python_find_venv(dir);
    if (venv != NULL) add_found(out, ipynb_python_venv_python(venv));
    g_free(venv);
    if (home != NULL && home[0] != '\0') {
        gchar* pyenv = g_build_filename(home, ".pyenv", "versions", NULL);
        add_each(out, pyenv, "python" EXE);
        g_free(pyenv);
        static const gchar* const CONDAS[] = { "miniforge3", "miniconda3", "anaconda3", "mambaforge", NULL };
        for (const gchar* const* c = CONDAS; *c != NULL; c++) {
            add_found(out, g_build_filename(home, *c, VENV_BIN, "python" EXE, NULL));
            gchar* envs = g_build_filename(home, *c, "envs", NULL);
            add_each(out, envs, "python" EXE);
            g_free(envs);
        }
        gchar* envs = g_build_filename(home, ".conda", "envs", NULL);
        add_each(out, envs, "python" EXE);
        g_free(envs);
        gchar* uv = g_build_filename(home, ".local", "share", "uv", "python", NULL);
        add_each(out, uv, "python3" EXE);
        g_free(uv);
    }
    if (path != NULL) {
        add_in_path(out, path, "python3" EXE);
        for (gint minor = 20; minor >= 6; minor--) {
            gchar* name = g_strdup_printf("python3.%d" EXE, minor);
            add_in_path(out, path, name);
            g_free(name);
        }
        add_in_path(out, path, "python" EXE);
    }
    return out;
}

/* The shell word for a Python chosen on a remote machine: ~/ is its home */
static gchar* remote_word(const gchar* chosen) {
    if (chosen == NULL || chosen[0] == '\0') return g_strdup("");
    if (!g_str_has_prefix(chosen, "~/")) return g_shell_quote(chosen);
    gchar* rest = g_shell_quote(chosen + 2);
    gchar* word = g_strconcat("\"$HOME\"/", rest, NULL);
    g_free(rest);
    return word;
}

gchar* ipynb_python_remote_command(const gchar* script, const gchar* dir, const gchar* chosen) {
    gchar* quoted = g_shell_quote(script != NULL ? script : "");
    gchar* start = g_shell_quote(dir != NULL ? dir : "");
    gchar* python = remote_word(chosen);
    /* c: the Python chosen; else v: the virtual environment (as
     * ipynb_python_find_venv()), p: its Python when it has Jupyter */
    gchar* line = g_strconcat(
        "c=", python, "; d=", start, "; v=; p=; i=0; "
        "if [ -n \"$c\" ]; then "
        "[ -x \"$c\" ] || "
        "{ printf '{\"ev\": \"error\", \"code\": \"no-python\", \"message\": \"%s not found\"}\\n' \"$c\"; exit 127; }; "
        "p=$c; d=; e=$(dirname \"$(dirname \"$c\")\"); if [ -f \"$e/pyvenv.cfg\" ]; then v=$e; fi; "
        "fi; "
        "while [ -n \"$d\" ] && [ $i -lt 64 ]; do "
        "for n in .venv venv env; do "
        "if [ -f \"$d/$n/pyvenv.cfg\" ] && [ -x \"$d/$n/bin/python\" ]; then v=$d/$n; break 2; fi; "
        "done; "
        "if [ -e \"$d/.git\" ] || [ \"$d\" = \"$HOME\" ] || [ \"$d\" = / ]; then break; fi; "
        "d=$(dirname \"$d\"); i=$((i + 1)); "
        "done; "
        "if [ -n \"$v\" ] && [ -z \"$p\" ]; then for j in \"$v\"/lib/python*/site-packages/jupyter_client; do "
        "[ -d \"$j\" ] && p=$v/bin/python; done; fi; "
        "if [ -n \"$p\" ]; then "
        "if [ -n \"$v\" ]; then VIRTUAL_ENV=$v; PATH=$v/bin:$PATH; export VIRTUAL_ENV PATH; unset PYTHONHOME; fi; "
        "else "
        "if [ -n \"$v\" ]; then " IPYNB_PYTHON_VENV_VAR "=$v; export " IPYNB_PYTHON_VENV_VAR "; fi; "
        "p=$(command -v python3 || command -v python) || "
        "{ echo '{\"ev\": \"error\", \"code\": \"no-python\", \"message\": \"python3 not found\"}'; exit 127; }; "
        "fi; "
        "exec \"$p\" -c ", quoted, NULL);
    g_free(python);
    g_free(start);
    g_free(quoted);
    return line;
}

/* --- The Pythons chosen for folders --- */

#define KEY_PYTHON "python"

static GKeyFile* load_choices(const gchar* file) {
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, file, G_KEY_FILE_KEEP_COMMENTS, NULL); /* missing: none */
    return kf;
}

gchar* ipynb_python_chosen(const gchar* file, const gchar* dir) {
    if (file == NULL || dir == NULL || dir[0] == '\0') return NULL;
    GKeyFile* kf = load_choices(file);
    gchar* d = g_strdup(dir);
    gchar* python = NULL;
    for (guint level = 0; python == NULL && level < MAX_LEVELS; level++) {
        python = g_key_file_get_string(kf, d, KEY_PYTHON, NULL);
        if (python != NULL && python[0] == '\0') g_clear_pointer(&python, g_free);
        gchar* up = g_path_get_dirname(d);
        gboolean top = strcmp(up, d) == 0 || strcmp(up, ".") == 0;
        g_free(d);
        d = up;
        if (top) break;
    }
    g_free(d);
    g_key_file_free(kf);
    return python;
}

gboolean ipynb_python_choose(const gchar* file, const gchar* dir, const gchar* python, GError** error) {
    if (dir == NULL || dir[0] == '\0' || strpbrk(dir, "[]\n\r") != NULL) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE, "%s", dir != NULL ? dir : "");
        return FALSE;
    }
    GKeyFile* kf = load_choices(file);
    if (python != NULL && python[0] != '\0') {
        g_key_file_set_string(kf, dir, KEY_PYTHON, python);
    } else {
        g_key_file_remove_group(kf, dir, NULL);
    }
    gchar* folder = g_path_get_dirname(file);
    g_mkdir_with_parents(folder, 0700);
    g_free(folder);
    gboolean ok = g_key_file_save_to_file(kf, file, error);
    g_key_file_free(kf);
    return ok;
}
