#include "git/git_tab.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "git/git_history.h"
#include "git/git_utils.h"

/* Git tab state (app->git), created by git_tab_init(): the notebook page and
 * its views */
typedef struct GitTab GitTab;
struct GitTab {
    GtkWidget* page;            /* notebook page of the tab */
    GitHistory* history;        /* the History view (freed with its widget) */
    gchar* current_root;        /* repository shown, or NULL */
};

/* Show the repository holding dir; reload: read it again even when the
 * repository is the same */
static void update_root(AppData* app, const gchar* dir, gboolean reload) {
    GitTab* g = app != NULL ? app->git : NULL;
    if (g == NULL) return;

    /* No Git tab in the Bookmarks, Notes or Remote view */
    if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->sidebar_view == SIDEBAR_VIEW_NOTES ||
        app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        gtk_widget_set_visible(g->page, FALSE);
        return;
    }

    gchar* root = find_git_root(dir);
    gtk_widget_set_visible(g->page, root != NULL);
    g_free(g->current_root);
    g->current_root = root;
    git_history_set_root(g->history, root, reload);
}

void git_tab_reload(AppData* app) {
    if (app == NULL) return;
    update_root(app, app->base_dir, TRUE);
}

void git_tab_update(AppData* app, const gchar* dir) {
    update_root(app, dir, FALSE);
}

void git_tab_init(AppData* app) {
    if (app == NULL || app->notebook == NULL) return;

    GitTab* g = g_new0(GitTab, 1);
    app->git = g;
    g->history = git_history_new(app);

    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(tab_box, TRUE);
    gtk_widget_set_vexpand(tab_box, TRUE);
    gtk_box_append(GTK_BOX(tab_box), git_history_widget(g->history));
    g->page = tab_box;

    GtkWidget* tab_label = icon_label_new("document-open-recent", _("Git Commits"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab_box, tab_label);

    git_tab_update(app, app->base_dir);
}

GtkWidget* git_tab_page(AppData* app) {
    return app != NULL && app->git != NULL ? app->git->page : NULL;
}

void git_tab_cleanup(AppData* app) {
    GitTab* g = app != NULL ? app->git : NULL;
    if (g == NULL) return;
    /* The views stay with their widgets until the window goes; their reads
     * stop now */
    git_history_cancel(g->history);
    g_free(g->current_root);
    g_clear_pointer(&app->git, g_free);
}
