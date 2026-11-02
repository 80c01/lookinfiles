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
 *                                editor_modes_refresh_bars()  (editor_modes.c)
 *   APP_EVENT_LAYOUT_CHANGED  -> workspace_save_current()     (workspace.c)
 *   APP_EVENT_LAYOUT_REBUILT  -> workspace_apply_positions()  (workspace.c)
 *   APP_EVENT_FILES_CHANGED   -> refresh_listing()            (file_list.c)
 *   APP_EVENT_PREVIEW_STALE   -> viewer_reload_preview()      (viewer.c)
 *   APP_EVENT_GIT_STATUS_CHANGED -> refresh_git_status_in_place() (file_list.c),
 *                                refresh_diff_gutters(), editor_modes_refresh_git() (editor_modes.c)
 *   APP_EVENT_FOCUS_NEXT_PANEL / _PREV_PANEL -> focus_next_panel() / focus_prev_panel() (ui.c)
 *   APP_EVENT_CLOSE_TAB       -> close_shown_tab()            (ui.c)
 *   APP_EVENT_SAVE_WORKSPACE_BOOKMARK -> session_bookmark_save() (session_bookmark.c)
 *   APP_EVENT_OPEN_WORKSPACE_BOOKMARK -> session_bookmark_open() (session_bookmark.c)
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
    /* The repository state changed (.git updated): refresh the Git badges in the tree */
    APP_EVENT_GIT_STATUS_CHANGED,
    /* A panel asked for the keyboard focus to go to the next / previous panel
     * (Emacs C-x o, Vim Ctrl+W w / W, Notepad F6 / Shift+F6) */
    APP_EVENT_FOCUS_NEXT_PANEL,
    APP_EVENT_FOCUS_PREV_PANEL,
    /* A panel asked to close the focused editor tab, else the current one if
     * it is on screen (Vim Ctrl+W c from the explorer, viewer or a list page) */
    APP_EVENT_CLOSE_TAB,
    /* The bookmarks view or the workspace map asked to save workspace
     * app->session_save_ws as a bookmark / the bookmarks view to open the
     * saved workspace in app->session_open_target (taken by the handler) */
    APP_EVENT_SAVE_WORKSPACE_BOOKMARK,
    APP_EVENT_OPEN_WORKSPACE_BOOKMARK,
    APP_EVENT_COUNT
} AppEvent;

typedef void (*AppEventHandler)(AppData* app);

/* Register handler for event (main.c only). */
void app_events_subscribe(AppData* app, AppEvent event, AppEventHandler handler);

/* Run the handlers of event. No-op when app is NULL or nothing is subscribed. */
void app_emit(AppData* app, AppEvent event);

/* Show a short message in the status bar for a few seconds (NULL clears it);
 * with undo, an Undo button follows it (the last trash or move, key u) */
void app_notice(AppData* app, const gchar* text, gboolean undo);

#endif /* APP_EVENTS_H */
