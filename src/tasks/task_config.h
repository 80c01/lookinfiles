#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

/* The tasks a project defines itself, in <project>/.lookinfiles/tasks.toml:
 *
 *   [[task]]
 *   name = "fast tests"                  # required: shown in the lists
 *   command = "./run_tests.sh --fast"    # through the shell (sh -c, cmd /c)
 *   args = ["make", "-C", "build"]       # or the program and its arguments
 *   cwd = "tests"                        # folder, relative to the project
 *   env = { CC = "clang" }               # variables added
 *   matcher = '^(?<file>[^:]+):(?<line>\d+): (?<message>.*)$'
 *   default = true                       # what F7 runs before any choice
 *
 * A matcher is a regular expression tried on each line of the output before
 * the built-in ones (task_parse.h), with the named groups file and line
 * (required), column, severity ("error", "warning", "note"; error when
 * missing) and message (the whole line when missing).
 *
 * The file is a small part of TOML: comments, [[task]] tables, keys with
 * strings ("basic" with escapes, 'literal'), booleans, arrays of strings
 * (also over several lines) and inline tables of strings.
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

#include "tasks/task_def.h"

/* The file of the project in dir ("<dir>/.lookinfiles/tasks.toml") */
gchar* task_config_path(const gchar* dir);

/* The tasks of text (the content of the file of the project in dir), in
 * file order, the one marked default first (TaskDef*). NULL with error set
 * (its line) when text is not a valid tasks file. */
GPtrArray* task_config_parse(const gchar* text, const gchar* dir, GError** error);

#endif /* TASK_CONFIG_H */
