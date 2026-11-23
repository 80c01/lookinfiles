#ifndef TASK_CHOICE_H
#define TASK_CHOICE_H

/* The task chosen last in each project (Shift+F7), kept from one session
 * to the next in tasks.ini of the configuration folder: a group per project
 * folder with the task's label, folder, argv and env (string lists). A
 * group that cannot be read is skipped. Pure GLib, unit-tested. */
#include <glib.h>

#include "tasks/task_def.h"

/* The file ("<config>/tasks.ini") */
gchar* task_choice_path(void);

/* The choices in path: project folder -> TaskDef* (a new table; empty when
 * the file is missing or unreadable) */
GHashTable* task_choice_load(const gchar* path);

/* Write the choices (project folder -> TaskDef*) to path */
gboolean task_choice_save(const gchar* path, GHashTable* chosen, GError** error);

#endif /* TASK_CHOICE_H */
