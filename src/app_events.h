#ifndef APP_EVENTS_H
#define APP_EVENTS_H

/* Application event bus: lets lower-level modules tell the window chrome that
 * something changed without depending on it (no include cycles).
 *
 * Emit with app_emit(app, APP_EVENT_...). Handlers run synchronously, in
 * subscription order, before app_emit() returns, so emitting is equivalent to
 * calling the subscribers directly. All subscriptions are made in main.c right
 * after AppData is allocated:
 *
 *   APP_EVENT_CHROME_CHANGED  -> update_path_label()          (topbar.c)
 *   APP_EVENT_STATUS_CHANGED  -> update_status_bar()          (status_bar.c)
 *   APP_EVENT_LAYOUT_CHANGED  -> workspace_save_current()     (workspace.c)
 *   APP_EVENT_LAYOUT_REBUILT  -> workspace_apply_positions()  (workspace.c)
 *   APP_EVENT_FILES_CHANGED   -> refresh_listing()            (file_list.c)
 *   APP_EVENT_PREVIEW_STALE   -> viewer_reload_preview()      (viewer.c)
 */
#include "app_types.h"

typedef enum {
    /* State shown in the top bar changed: directory, sort, hidden files,
     * panel visibility, sidebar view, remote connection, editor docking */
    APP_EVENT_CHROME_CHANGED = 0,
    /* Selection, file info or editor mode shown in the status bar changed */
    APP_EVENT_STATUS_CHANGED,
    /* Panel visibility or split fractions changed: snapshot the current workspace */
    APP_EVENT_LAYOUT_CHANGED,
    /* Panes were shown, hidden or rebuilt: re-apply the saved split positions */
    APP_EVENT_LAYOUT_REBUILT,
    /* Files were created, renamed or saved on disk: refresh the explorer listing */
    APP_EVENT_FILES_CHANGED,
    /* The file shown in the preview changed (saved, Org state toggled): re-render it */
    APP_EVENT_PREVIEW_STALE,
    APP_EVENT_COUNT
} AppEvent;

typedef void (*AppEventHandler)(AppData* app);

/* Register handler for event (main.c only). */
void app_events_subscribe(AppData* app, AppEvent event, AppEventHandler handler);

/* Run the handlers of event. No-op when app is NULL or nothing is subscribed. */
void app_emit(AppData* app, AppEvent event);

#endif /* APP_EVENTS_H */
