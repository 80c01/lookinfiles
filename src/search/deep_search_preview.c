/* The preview of the Deep Search results (see deep_search_preview.h) */
#include "search/deep_search_preview.h"
#include "search/deep_search_list.h"
#include "core/app_events.h"
#include "widgets/after_frame.h"
#include "widgets/focus_route.h"

#define PREVIEW_WAIT_MS 50 /* the result moved to is previewed after the list's next frame */

struct DeepSearchPreview {
    AppData* app;
    GtkWidget* page;
    GtkWidget* list;
    GtkNotebook* shown_in; /* notebook the page was last shown in (not a ref) */
    AfterFrame* wait;      /* the result moved to, previewed after the next frame */
    gchar* path;
    gint line;
};

GtkNotebook* deep_search_page_notebook(GtkWidget* page) {
    GtkWidget* parent = page != NULL ? gtk_widget_get_parent(page) : NULL;
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

void deep_search_show_preview_panel(AppData* app) {
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
}

/* Out of the preview notebook: a preview does not hide the results */
static gboolean beside_preview(DeepSearchPreview* dp) {
    GtkNotebook* nb = deep_search_page_notebook(dp->page);
    return nb != NULL && GTK_WIDGET(nb) != dp->app->notebook;
}

/* Through the event bus: the preview module is not a dependency of search */
static void preview_now(DeepSearchPreview* dp, const gchar* path, gint line) {
    g_free(dp->app->preview_request_path);
    dp->app->preview_request_path = g_strdup(path);
    dp->app->preview_request_line = line;
    app_emit(dp->app, APP_EVENT_PREVIEW_PATH);
    g_clear_pointer(&dp->app->preview_request_path, g_free); /* not taken: nothing subscribed */
}

static gboolean run_pending(gpointer data) {
    DeepSearchPreview* dp = data;
    dp->wait = NULL; /* freed after the run */
    gchar* path = g_steal_pointer(&dp->path);
    if (path != NULL && beside_preview(dp)) preview_now(dp, path, dp->line);
    g_free(path);
    return G_SOURCE_REMOVE;
}

void deep_search_preview_cancel(DeepSearchPreview* dp) {
    if (dp == NULL) return;
    if (dp->wait != NULL) after_frame_cancel(g_steal_pointer(&dp->wait));
    g_clear_pointer(&dp->path, g_free);
}

void deep_search_preview_result(DeepSearchPreview* dp, const gchar* path, gint line, gboolean asked) {
    if (dp == NULL || path == NULL) return;
    AppData* app = dp->app;
    if (asked) {
        deep_search_preview_cancel(dp);
        deep_search_show_preview_panel(app);
        preview_now(dp, path, line);
        if (!beside_preview(dp)) focus_route_preview_from(app, deep_search_list_view(dp->list));
        return;
    }
    GtkWidget* view = deep_search_list_view(dp->list);
    GtkRoot* root = gtk_widget_get_root(view);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (!beside_preview(dp) || app->viewer_box == NULL || !gtk_widget_get_visible(app->viewer_box) ||
        focus == NULL || (focus != view && !gtk_widget_is_ancestor(focus, view))) {
        return; /* found while the search runs, or the focus elsewhere */
    }
    g_free(dp->path);
    dp->path = g_strdup(path);
    dp->line = line;
    if (dp->wait != NULL) return; /* already waiting: the latest result is previewed */
    dp->wait = after_frame_new(view, PREVIEW_WAIT_MS, run_pending, dp, NULL);
    if (dp->wait == NULL) run_pending(dp);
}

/* After the move (editor_move_page_direction() focuses the page): the
 * focus back in the results. data: a ref on the list */
static gboolean focus_results_idle(gpointer data) {
    GtkWidget* list = data;
    if (gtk_widget_get_mapped(list) && deep_search_list_count(list) > 0) {
        gtk_widget_grab_focus(deep_search_list_view(list));
    }
    g_object_unref(list);
    return G_SOURCE_REMOVE;
}

/* Moved beside the preview: the result selected is previewed there */
static void on_page_map(GtkWidget* page, gpointer data) {
    DeepSearchPreview* dp = data;
    GtkNotebook* nb = deep_search_page_notebook(page);
    if (nb == dp->shown_in) return; /* back to the tab, or with its workspace */
    gboolean moved = dp->shown_in != NULL;
    dp->shown_in = nb;
    if (!moved || !beside_preview(dp) || deep_search_list_count(dp->list) == 0) return;
    deep_search_list_preview_selected(dp->list);
    g_idle_add(focus_results_idle, g_object_ref(dp->list));
}

DeepSearchPreview* deep_search_preview_new(AppData* app, GtkWidget* page, GtkWidget* list) {
    DeepSearchPreview* dp = g_new0(DeepSearchPreview, 1);
    dp->app = app;
    dp->page = page;
    dp->list = list;
    g_signal_connect(page, "map", G_CALLBACK(on_page_map), dp);
    return dp;
}

void deep_search_preview_free(DeepSearchPreview* dp) {
    if (dp == NULL) return;
    deep_search_preview_cancel(dp);
    g_free(dp);
}
