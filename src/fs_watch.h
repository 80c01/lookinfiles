#ifndef FS_WATCH_H
#define FS_WATCH_H

/* Filesystem watching for the explorer and the preview. Changes are reported
 * after a 150 ms debounce as APP_EVENT_FILES_CHANGED (directory contents),
 * APP_EVENT_GIT_STATUS_CHANGED (.git) and APP_EVENT_PREVIEW_STALE (viewed file). */
#include "app_types.h"

/* Watch base_dir and every expanded directory (app->expanded_paths) */
void sync_directory_monitors(AppData* app);
void monitor_directory(AppData* app);
/* Stop every monitor and the pending refresh (at exit) */
void fs_watch_free(AppData* app);

/* Watch the file shown in the preview (NULL stops watching) */
void monitor_file(AppData* app, const char* path);

/* Run the debounced refresh of pending dir/file/git changes */
void schedule_refresh(AppData* app);
/* Rescan the git status soon, as if .git had changed (window focused again) */
void fs_watch_git_changed(AppData* app);
/* Drop a pending directory event (the explorer reloads the listing anyway) */
void fs_watch_forget_dir_changes(AppData* app);

#endif /* FS_WATCH_H */
