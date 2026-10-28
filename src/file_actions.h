#ifndef FILE_ACTIONS_H
#define FILE_ACTIONS_H

/* Explorer file actions (files view) */
#include "app_types.h"

/* Each action runs its dialog and the filesystem change, then returns the path
 * the explorer should select after refreshing (caller frees), or NULL when
 * nothing changed. The explorer refresh is done by file_list.c. */

/* New file (opened in the editor) / new folder in the selected directory */
gchar* file_action_new_file(AppData* app);
gchar* file_action_new_folder(AppData* app);
/* Rename the selected file or folder (open editor tabs follow) */
gchar* file_action_rename_selected(AppData* app);
/* Move the selected file or folder to the trash, after confirmation */
gchar* file_action_trash_selected(AppData* app);

#endif /* FILE_ACTIONS_H */
