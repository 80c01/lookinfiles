#ifndef TASK_DEF_H
#define TASK_DEF_H

/* A build or test task: what runs, where, and how its output is read
 * (task_detect.h finds them, task_config.h reads those of a project).
 * Pure GLib. */
#include <glib.h>

typedef struct {
    gchar* label;   /* what is run, as shown ("make -j8 test", a task's name) */
    gchar* detail;  /* the command of a named task, shown beside it, or NULL */
    gchar** argv;   /* the command; argv[0] is a program found in PATH */
    gchar* dir;     /* absolute folder it runs in */
    gchar** env;    /* "NAME=value" added to the environment, or NULL */
    gchar* matcher; /* regular expression read before the built-in ones, or NULL */
} TaskDef;

TaskDef* task_def_new(const gchar* label, const gchar* const* argv, const gchar* dir);
TaskDef* task_def_copy(const TaskDef* def);
void task_def_free(TaskDef* def);
/* TRUE when a and b run the same command in the same folder and environment */
gboolean task_def_equal(const TaskDef* a, const TaskDef* b);

#endif /* TASK_DEF_H */
