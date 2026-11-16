/* Side of the dedicated editor panel next to the preview (see editor_swap.h) */
#include "editor/editor_swap.h"
#include "core/app_events.h"

static VirtualLayout* current_ws(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) return NULL;
    return &app->workspaces[app->current_ws];
}

gboolean editor_swap_active(AppData* app) {
    VirtualLayout* ws = current_ws(app);
    return ws != NULL && ws->editor_first;
}

void editor_swap_place(AppData* app) {
    if (app == NULL || app->editor_paned == NULL || app->viewer_box == NULL || app->editor_box == NULL) return;
    GtkPaned* paned = GTK_PANED(app->editor_paned);
    GtkWidget* first = editor_swap_active(app) ? app->editor_box : app->viewer_box;
    GtkWidget* second = (first == app->viewer_box) ? app->editor_box : app->viewer_box;
    if (gtk_paned_get_start_child(paned) == first) return;
    /* Both leave the paned before either takes its new side */
    g_object_ref(first);
    g_object_ref(second);
    gtk_paned_set_start_child(paned, NULL);
    gtk_paned_set_end_child(paned, NULL);
    gtk_paned_set_start_child(paned, first);
    gtk_paned_set_end_child(paned, second);
    g_object_unref(first);
    g_object_unref(second);
}

void editor_swap_toggle(AppData* app) {
    VirtualLayout* ws = current_ws(app);
    if (ws == NULL) return;
    app_layout_begin(app);
    ws->editor_first = !ws->editor_first;
    editor_swap_place(app);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
}

EditorMoveDirection editor_swap_preview_side(AppData* app) {
    return editor_swap_active(app) ? EDITOR_MOVE_RIGHT : EDITOR_MOVE_LEFT;
}
