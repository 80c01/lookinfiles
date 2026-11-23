#ifndef TASK_REMOTE_H
#define TASK_REMOTE_H

/* A build or test task run on a remote machine (its folder is a mount of
 * vfs.h): the command goes through the machine's connection (vfs_spawn()),
 * in the task's folder there, its standard error merged into its output,
 * read on a thread of its own and handed to the main thread as it comes.
 * The paths the tools write (folders of the machine) are turned into the
 * folders they are seen at here (task_remote_local_path()). Stop asks the
 * machine to end the command and what it started (SIGTERM, then SIGKILL),
 * by the process number the command line prints first.
 *
 * GIO and vfs, no widgets; the command line and the paths are unit-tested. */
#include <glib.h>

#include "tasks/task_def.h"

typedef struct TaskRemote TaskRemote;

/* Called on the main thread, in this order, until task_remote_free() */
typedef struct {
    /* The command started in remote_dir (the task's folder on the machine) */
    void (*ready)(gpointer data, const gchar* remote_dir);
    /* Output read (any cut, not NUL-terminated) */
    void (*output)(gpointer data, const gchar* bytes, gsize len);
    /* The last call: the exit status (-1: unknown), or error when the
     * command could not run (then no ready nor output may have come) */
    void (*done)(gpointer data, gint status, const gchar* error);
} TaskRemoteFuncs;

/* Run def (its dir on a remote machine) */
TaskRemote* task_remote_start(const TaskDef* def, const TaskRemoteFuncs* funcs, gpointer data);

/* Ask the machine to stop the command (kill_now: SIGKILL, else SIGTERM) */
void task_remote_stop(TaskRemote* t, gboolean kill_now);

/* No more calls; the command is stopped if it still runs */
void task_remote_free(TaskRemote* t);

/* The folder here of path (a file of the machine, as the tools write it),
 * NULL when it is not under the folder the machine is seen at; valid once
 * ready was called. Caller frees. */
gchar* task_remote_local_path(TaskRemote* t, const gchar* path);

/* --- Parts, unit-tested --- */

/* The line for the machine's shell: errors merged, the shell's process
 * number printed first ("@@lookinfiles-task <pid>"), then the command (its
 * argv quoted, or a shell task's line through sh -c) in its place, with
 * def's variables */
gchar* task_remote_command_line(const TaskDef* def);

/* The process number of the first line of output, or 0 when it is not one */
gint task_remote_parse_pid(const gchar* line);

/* path (absolute on the machine) seen here: under mount when it is under
 * remote_root (the absolute folder of the machine mount shows), else NULL */
gchar* task_remote_map_path(const gchar* remote_root, const gchar* mount, const gchar* path);

#endif /* TASK_REMOTE_H */
