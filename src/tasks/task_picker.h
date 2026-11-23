#ifndef TASK_PICKER_H
#define TASK_PICKER_H

/* Shift+F7: the tasks of a project in an AppDialog, filtered as you type
 * (label and command), Up / Down / Page Up / Page Down from the field,
 * Enter runs the selected one, Escape closes. */
#include "core/app_types.h"
#include "tasks/task_def.h"

/* What the chosen task does: def among the tasks of project */
typedef void (*TaskPickerRun)(AppData* app, const TaskDef* def, const gchar* project);

/* The list of tasks (TaskDef*, copied) of project, chosen (may be NULL)
 * selected first; run is called with the one chosen */
void task_picker_show(AppData* app, const gchar* project, const GPtrArray* tasks, const TaskDef* chosen,
                      TaskPickerRun run);

#endif /* TASK_PICKER_H */
