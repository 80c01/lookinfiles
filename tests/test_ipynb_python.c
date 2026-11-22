/* Unit tests for src/jupyter/ipynb_python.c: virtual environments made in a
 * temporary folder, their Python a shell script. */
#include "jupyter/ipynb_python.h"

#include <glib/gstdio.h>
#include <string.h>

static void write_file(const gchar* path, const gchar* text, gint mode) {
    gchar* dir = g_path_get_dirname(path);
    g_assert_cmpint(g_mkdir_with_parents(dir, 0755), ==, 0);
    g_free(dir);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_assert_cmpint(g_chmod(path, mode), ==, 0);
}

/* A virtual environment in folder venv; its Python prints its
 * VIRTUAL_ENV and arguments */
static void make_venv(const gchar* venv) {
    gchar* cfg = g_build_filename(venv, "pyvenv.cfg", NULL);
    write_file(cfg, "home = /usr/bin\n", 0644);
    g_free(cfg);
    gchar* python = ipynb_python_venv_python(venv);
    write_file(python, "#!/bin/sh\necho \"venv:$VIRTUAL_ENV:$*\"\n", 0755);
    g_free(python);
}

static void add_jupyter(const gchar* venv) {
    gchar* dir = g_build_filename(venv, "lib", "python3.12", "site-packages", "jupyter_client", NULL);
    g_assert_cmpint(g_mkdir_with_parents(dir, 0755), ==, 0);
    g_free(dir);
}

typedef struct {
    gchar* root;  /* the temporary folder */
    gchar* repo;  /* root/repo, with .git */
    gchar* sub;   /* root/repo/a/b: the notebook's folder */
    gchar* venv;  /* root/repo/.venv */
} Tree;

static void tree_init(Tree* t) {
    t->root = g_dir_make_tmp("ipynb-python-XXXXXX", NULL);
    g_assert_nonnull(t->root);
    t->repo = g_build_filename(t->root, "repo", NULL);
    t->sub = g_build_filename(t->repo, "a", "b", NULL);
    t->venv = g_build_filename(t->repo, ".venv", NULL);
    gchar* git = g_build_filename(t->repo, ".git", NULL);
    g_assert_cmpint(g_mkdir_with_parents(git, 0755), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(t->sub, 0755), ==, 0);
    g_free(git);
}

static void tree_clear(Tree* t) {
    gchar* argv[] = { (gchar*)"rm", (gchar*)"-rf", t->root, NULL };
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL));
    g_free(t->root);
    g_free(t->repo);
    g_free(t->sub);
    g_free(t->venv);
}

static void test_find(void) {
    Tree t;
    tree_init(&t);
    g_assert_null(ipynb_python_find_venv(t.sub));
    g_assert_null(ipynb_python_find_venv(NULL));

    /* Above the repository: not looked at */
    gchar* outside = g_build_filename(t.root, "venv", NULL);
    make_venv(outside);
    g_assert_null(ipynb_python_find_venv(t.sub));

    /* A folder without pyvenv.cfg is not one */
    gchar* fake = g_build_filename(t.repo, "a", "env", "bin", NULL);
    g_assert_cmpint(g_mkdir_with_parents(fake, 0755), ==, 0);
    g_free(fake);
    g_assert_null(ipynb_python_find_venv(t.sub));

    make_venv(t.venv);
    gchar* found = ipynb_python_find_venv(t.sub);
    g_assert_cmpstr(found, ==, t.venv);
    g_free(found);
    found = ipynb_python_find_venv(t.repo);
    g_assert_cmpstr(found, ==, t.venv);
    g_free(found);

    /* The nearest one first */
    gchar* near = g_build_filename(t.repo, "a", "venv", NULL);
    make_venv(near);
    found = ipynb_python_find_venv(t.sub);
    g_assert_cmpstr(found, ==, near);
    g_free(found);
    g_free(near);

    g_assert_false(ipynb_python_venv_has_jupyter(t.venv));
    add_jupyter(t.venv);
    g_assert_true(ipynb_python_venv_has_jupyter(t.venv));
    g_free(outside);
    tree_clear(&t);
}

static void test_environ(void) {
    const gchar* envp[] = { "PATH=/usr/bin", "PYTHONHOME=/x", "A=1", NULL };
    gchar** e = ipynb_python_venv_environ("/p/.venv", g_strdupv((gchar**)envp));
    g_assert_cmpstr(g_environ_getenv(e, "VIRTUAL_ENV"), ==, "/p/.venv");
    g_assert_cmpstr(g_environ_getenv(e, "PATH"), ==, "/p/.venv/bin:/usr/bin");
    g_assert_null(g_environ_getenv(e, "PYTHONHOME"));
    g_assert_cmpstr(g_environ_getenv(e, "A"), ==, "1");
    g_strfreev(e);
}

static void test_local(void) {
    Tree t;
    tree_init(&t);
    gchar** envp = NULL;
    gchar* python = ipynb_python_local(t.sub, NULL, &envp);
    g_assert_null(envp); /* no virtual environment: python3 of the PATH, here */
    g_free(python);

    make_venv(t.venv);
    python = ipynb_python_local(t.sub, NULL, &envp);
    g_assert_nonnull(envp); /* without Jupyter: said to the bridge */
    g_assert_cmpstr(g_environ_getenv(envp, IPYNB_PYTHON_VENV_VAR), ==, t.venv);
    g_assert_cmpstr(g_environ_getenv(envp, "PATH"), ==, g_getenv("PATH")); /* not active */
    g_free(python);
    g_strfreev(envp);

    add_jupyter(t.venv);
    python = ipynb_python_local(t.sub, NULL, &envp);
    gchar* expected = ipynb_python_venv_python(t.venv);
    g_assert_cmpstr(python, ==, expected);
    g_assert_cmpstr(g_environ_getenv(envp, "VIRTUAL_ENV"), ==, t.venv);
    g_assert_null(g_environ_getenv(envp, IPYNB_PYTHON_VENV_VAR));
    g_free(expected);
    g_free(python);
    g_strfreev(envp);
    tree_clear(&t);
}

/* The remote line run by a local sh with " --list" after it */
static gchar* run_remote_with(const gchar* script, const gchar* dir, const gchar* chosen,
                             const gchar* const* env) {
    gchar* line = ipynb_python_remote_command(script, dir, chosen);
    gchar* cmd = g_strconcat(line, " --list", NULL);
    const gchar* argv[] = { "/bin/sh", "-c", cmd, NULL };
    gchar* out = NULL;
    gint status = -1;
    g_assert_true(g_spawn_sync(NULL, (gchar**)argv, (gchar**)env, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, NULL,
                               &status, NULL));
    g_free(cmd);
    g_free(line);
    return out;
}

static gchar* run_remote(const gchar* script, const gchar* dir, const gchar* const* env) {
    return run_remote_with(script, dir, NULL, env);
}

static void test_remote_command(void) {
    /* The script gets the arguments that follow */
    gchar* out = run_remote("import sys\nprint('it''s', sys.argv[1:])\n", NULL, NULL);
    g_assert_cmpstr(out, ==, "its ['--list']\n");
    g_free(out);

    const gchar* none[] = { "PATH=/nonexistent", NULL };
    out = run_remote("print(1)", NULL, none);
    g_assert_nonnull(strstr(out, "\"code\": \"no-python\""));
    g_free(out);

    Tree t;
    tree_init(&t);
    const gchar* script = "import os, sys\nprint(os.environ.get('" IPYNB_PYTHON_VENV_VAR "'), sys.argv[1:])\n";
    gchar* path = g_strconcat("PATH=", g_getenv("PATH"), NULL);
    gchar* home = g_strconcat("HOME=", t.root, NULL);
    const gchar* env[] = { path, home, NULL };
    out = run_remote(script, t.sub, env);
    g_assert_cmpstr(out, ==, "None ['--list']\n");
    g_free(out);

    /* A virtual environment without Jupyter: python3, told where it is */
    make_venv(t.venv);
    out = run_remote(script, t.sub, env);
    gchar* expected = g_strdup_printf("%s ['--list']\n", t.venv);
    g_assert_cmpstr(out, ==, expected);
    g_free(expected);
    g_free(out);

    /* With Jupyter: its Python, active */
    add_jupyter(t.venv);
    out = run_remote("S", t.sub, env);
    expected = g_strdup_printf("venv:%s:-c S --list\n", t.venv);
    g_assert_cmpstr(out, ==, expected);
    g_free(expected);
    g_free(out);

    /* The search stops at the home folder */
    g_free(home);
    home = g_strconcat("HOME=", t.sub, NULL);
    env[1] = home;
    out = run_remote(script, t.sub, env);
    g_assert_cmpstr(out, ==, "None ['--list']\n");
    g_free(out);

    /* A Python chosen: it, whatever the folder; active when in a venv */
    gchar* chosen = ipynb_python_venv_python(t.venv);
    out = run_remote_with("S", NULL, chosen, env);
    expected = g_strdup_printf("venv:%s:-c S --list\n", t.venv);
    g_assert_cmpstr(out, ==, expected);
    g_free(expected);
    g_free(out);
    g_free(chosen);
    /* ~/ is the home folder of the machine */
    gchar* other = g_build_filename(t.sub, "py", NULL);
    write_file(other, "#!/bin/sh\necho \"other:$VIRTUAL_ENV:$1\"\n", 0755);
    g_free(other);
    out = run_remote_with("S", t.sub, "~/py", env);
    g_assert_cmpstr(out, ==, "other::-c\n");
    g_free(out);
    out = run_remote_with("S", t.sub, "/missing/python", env);
    g_assert_nonnull(strstr(out, "\"code\": \"no-python\", \"message\": \"/missing/python not found\""));
    g_free(out);
    g_free(home);
    g_free(path);
    tree_clear(&t);
}

static void test_local_chosen(void) {
    Tree t;
    tree_init(&t);
    make_venv(t.venv); /* without Jupyter: the Python chosen wins anyway */
    gchar* chosen = ipynb_python_venv_python(t.venv);
    gchar** envp = NULL;
    gchar* python = ipynb_python_local(t.root, chosen, &envp);
    g_assert_cmpstr(python, ==, chosen);
    g_assert_cmpstr(g_environ_getenv(envp, "VIRTUAL_ENV"), ==, t.venv);
    g_free(python);
    g_strfreev(envp);
    gchar* venv = ipynb_python_venv_of(chosen);
    g_assert_cmpstr(venv, ==, t.venv);
    g_free(venv);
    g_assert_null(ipynb_python_venv_of("/usr/bin/python3"));

    python = ipynb_python_local(t.sub, "/usr/bin/python3", &envp);
    g_assert_cmpstr(python, ==, "/usr/bin/python3");
    g_assert_null(envp);
    g_free(python);
    python = ipynb_python_local(t.sub, "~/bin/python", &envp);
    gchar* expected = g_build_filename(g_get_home_dir(), "bin", "python", NULL);
    g_assert_cmpstr(python, ==, expected);
    g_free(expected);
    g_free(python);
    g_strfreev(envp);
    g_free(chosen);
    tree_clear(&t);
}

static void test_choices(void) {
    Tree t;
    tree_init(&t);
    gchar* file = g_build_filename(t.root, "config", "jupyter.ini", NULL);
    g_assert_null(ipynb_python_chosen(file, t.sub));
    g_assert_true(ipynb_python_choose(file, t.repo, "/opt/py/bin/python", NULL));
    gchar* python = ipynb_python_chosen(file, t.sub); /* from its parent */
    g_assert_cmpstr(python, ==, "/opt/py/bin/python");
    g_free(python);
    g_assert_null(ipynb_python_chosen(file, t.root));
    g_assert_true(ipynb_python_choose(file, t.sub, "/other/python", NULL));
    python = ipynb_python_chosen(file, t.sub); /* the nearest */
    g_assert_cmpstr(python, ==, "/other/python");
    g_free(python);
    g_assert_true(ipynb_python_choose(file, t.sub, NULL, NULL));
    python = ipynb_python_chosen(file, t.sub);
    g_assert_cmpstr(python, ==, "/opt/py/bin/python");
    g_free(python);
    GError* error = NULL;
    g_assert_false(ipynb_python_choose(file, "/a[b]", "/x", &error));
    g_assert_nonnull(error);
    g_error_free(error);
    g_free(file);
    tree_clear(&t);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ipynb_python/find", test_find);
    g_test_add_func("/ipynb_python/environ", test_environ);
    g_test_add_func("/ipynb_python/local", test_local);
    g_test_add_func("/ipynb_python/remote_command", test_remote_command);
    g_test_add_func("/ipynb_python/local_chosen", test_local_chosen);
    g_test_add_func("/ipynb_python/choices", test_choices);
    return g_test_run();
}
