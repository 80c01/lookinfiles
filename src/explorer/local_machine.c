#include "explorer/local_machine.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/remote_path.h"
#include "explorer/file_list.h"
#include "explorer/tree_nav.h"
#include "terminal/terminal.h"
#include "theme/palette.h"

void local_machine_append_row(ExplorerRow* parent, AppData* app) {
    if (parent == NULL) {
        return;
    }
    const gchar* host = g_get_host_name();
    ExplorerRow* row = explorer_row_append(parent, LOCAL_MACHINE_ROW, host, host, "");
    explorer_row_set_glyph(row, "computer");
    gchar* badge = NULL;
    if (local_machine_is_current(app)) {
        badge = g_markup_printf_escaped("<span foreground=\"%s\" weight=\"bold\">● %s</span>  <span foreground=\"%s\">(%s)</span>",
                                        palette_color(PAL_SUCCESS), _("This computer"),
                                        palette_color(PAL_FG_SUBTLE), g_get_user_name());
    } else {
        badge = g_markup_printf_escaped("<span foreground=\"%s\">%s (%s)</span>", palette_color(PAL_FG_SUBTLE),
                                        _("This computer"), g_get_user_name());
    }
    explorer_row_set_badge(row, badge);
    g_free(badge);
}

gboolean local_machine_selected(AppData* app) {
    return app != NULL && explorer_row_is(tree_nav_selected(app), LOCAL_MACHINE_ROW);
}

gboolean local_machine_is_current(AppData* app) {
    return app == NULL || app->current_remote_id == NULL;
}

/* The current workspace has no remote machine any more */
static void make_local(AppData* app) {
    g_clear_pointer(&app->current_remote_id, g_free);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        g_clear_pointer(&app->workspaces[app->current_ws].remote_id, g_free);
    }
}

void local_machine_open(AppData* app) {
    if (app == NULL) {
        return;
    }
    make_local(app);
    /* The folder shown stays when it is local (back from the Machines view) */
    gchar* dir = NULL;
    if (app->base_dir != NULL && !remote_path_locate(app, app->base_dir, NULL, NULL, NULL)) {
        dir = g_strdup(app->base_dir);
    } else {
        dir = g_strdup(g_get_home_dir());
    }
    explorer_set_view(app, SIDEBAR_VIEW_FILES);
    reload_directory(app, dir);
    g_free(dir);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void local_machine_open_terminal(AppData* app) {
    if (app == NULL) {
        return;
    }
    make_local(app);
    if (app->term_scroll != NULL && !gtk_widget_get_visible(app->term_scroll)) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
        if (app->right_paned != NULL) {
            gtk_widget_set_visible(app->right_paned, TRUE);
        }
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
#ifdef HAVE_VTE
    term_spawn_pane(app, FALSE);
#endif
}
