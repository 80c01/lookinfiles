/* Preview of a path that is not the explorer's selection (see preview_show.h) */
#include "preview/preview_show.h"
#include "core/app_events.h"
#include "explorer/fs_watch.h"
#include "media/audio.h"
#include "media/video.h"
#include "preview/dir_preview.h"
#include "preview/preview_state.h"
#include "preview/preview_sync.h"
#include "preview/viewer.h"

void preview_show_path(AppData* app, const gchar* path, gint line) {
    if (app == NULL || path == NULL || app->buffer == NULL) return;
    gchar* name = g_path_get_basename(path);
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        /* Leave the player page before stopping it (its controls must not be
         * redrawn unallocated) */
        if (app->preview_stack != NULL) gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        audio_preview_stop(app);
        video_preview_stop(app);
        dir_preview_show(app, name, path);
        preview_state_set_file(app, NULL, NULL);
        monitor_file(app, NULL);
    } else {
        if (g_strcmp0(name, preview_state_name(app)) != 0) {
            preview_state_set_raw(app, FALSE);
            preview_state_set_zoom(app, 1.0);
        }
        app->file_dirty = FALSE;
        preview_sync_goto_line(app, path, line);
        load_file_content(app, name, path);
        preview_state_set_file(app, path, name);
        monitor_file(app, path);
    }
    /* Not the explorer's selection: it comes back when the explorer has the
     * focus again (viewer_explorer_focused()) */
    preview_state_set_from_editor(app);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app_emit(app, APP_EVENT_STATUS_CHANGED);
    g_free(name);
}

void preview_show_requested(AppData* app) {
    if (app == NULL) return;
    gchar* path = g_steal_pointer(&app->preview_request_path);
    if (path == NULL) return;
    preview_show_path(app, path, app->preview_request_line);
    viewer_show_preview_page(app);
    g_free(path);
}
