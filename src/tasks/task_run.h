#ifndef TASK_RUN_H
#define TASK_RUN_H

/* Build and test tasks (make, cargo, npm run...; task_detect.h) run in the
 * project of the focused editor tab, else of the explorer's folder: F7 runs
 * the task chosen last in that project (at first its default one),
 * Shift+F7 chooses one in a list, Ctrl+F7 stops it. The output is read as it
 * comes, its messages (task_parse.h) listed in the Build page (task_page.h);
 * F8 / Shift+F8 go to the next / previous one. One task runs at a time: a
 * new one stops the one running. On a remote machine the task runs there
 * (task_remote.h). */
#include "core/app_types.h"

/* F7: run the task chosen last in the current project, or its default one */
void task_run_again(AppData* app);

/* Shift+F7: choose the task to run among the project's */
void task_run_choose(AppData* app);

/* Ctrl+F7: stop the running task (and what it started). FALSE when none runs. */
gboolean task_run_stop(AppData* app);

/* Stop the running task without a word (at exit) */
void task_run_free(AppData* app);

#endif /* TASK_RUN_H */
