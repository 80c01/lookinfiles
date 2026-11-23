#ifndef TASK_DETECT_H
#define TASK_DETECT_H

/* The build and test tasks of a project, found from its files (F7 / Shift+F7,
 * task_run.h):
 *  - Makefile: make, then make <target> (the .PHONY targets, else the plain
 *    names of the rules);
 *  - Cargo.toml: cargo build, check, test, clippy, run (in the workspace's
 *    folder when the crate is a member);
 *  - package.json: <runner> run <script> for each script, the runner from the
 *    lock file (pnpm, yarn, bun, else npm);
 *  - go.mod: go build / test / vet ./...;
 *  - CMakeLists.txt: cmake --build <dir> (a configured build folder), else
 *    the configuration; meson.build: ninja -C <dir> and meson test, else
 *    meson setup; build.ninja: ninja;
 *  - pyproject.toml, setup.py, pytest.ini: pytest.
 * The first task found is the default one (F7 before any choice).
 *
 * Reads files: call from a worker thread. Pure GLib, unit-tested. */
#include <glib.h>

typedef struct {
    gchar* label;  /* what is run, as shown ("make test", "npm run lint") */
    gchar** argv;  /* the command; argv[0] is a program found in PATH */
    gchar* dir;    /* absolute folder it runs in */
} TaskDef;

TaskDef* task_def_new(const gchar* label, const gchar* const* argv, const gchar* dir);
TaskDef* task_def_copy(const TaskDef* def);
void task_def_free(TaskDef* def);
/* TRUE when a and b run the same command in the same folder */
gboolean task_def_equal(const TaskDef* a, const TaskDef* b);

/* The folder of the project of start (a folder): start or the nearest of its
 * parents holding a build file, not above the home folder; NULL when none */
gchar* task_detect_project_dir(const gchar* start);

/* The tasks of the project in dir (TaskDef*, default first; empty when dir
 * has no build file) */
GPtrArray* task_detect(const gchar* dir);

/* The targets a Makefile offers to run (gchar*, in file order) */
GPtrArray* task_detect_make_targets(const gchar* makefile);

/* The script names of a package.json (gchar*, in file order) */
GPtrArray* task_detect_npm_scripts(const gchar* package_json);

#endif /* TASK_DETECT_H */
