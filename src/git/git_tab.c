#include "git/git_tab.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "git/git_changes.h"
#include "git/git_history.h"
#include "git/git_stats_view.h"
#include "git/git_utils.h"
#include "widgets/focus_route.h"

/* The views of the Git tab, in the order of their buttons */
typedef enum { GIT_VIEW_CHANGES, GIT_VIEW_HISTORY, GIT_VIEW_STATS, GIT_VIEW_COUNT } GitView;

static const gchar* const VIEW_NAMES[GIT_VIEW_COUNT] = { "changes", "history", "statistics" };

/* Git tab state (app->git), created by git_tab_init(): the notebook page and
 * its views */
typedef struct GitTab GitTab;
struct GitTab {
    AppData* app;
    GtkWidget* page;            /* notebook page of the tab */
    GtkWidget* stack;           /* the views */
    GtkWidget* buttons[GIT_VIEW_COUNT]; /* linked toggle buttons choosing them */
    GitChanges* changes;        /* the Changes view (freed with its widget) */
    GitHistory* history;        /* the History view (freed with its widget) */
    GitStatsView* stats;        /* the Statistics view (freed with its widget) */
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
    /* Files may have changed with each update: the Changes view reads again */
    git_changes_set_root(g->changes, root);
    git_history_set_root(g->history, root, reload);
    git_stats_view_set_root(g->stats, root, reload);
}

void git_tab_reload(AppData* app) {
    if (app == NULL) return;
    update_root(app, app->base_dir, TRUE);
}

void git_tab_update(AppData* app, const gchar* dir) {
    update_root(app, dir, FALSE);
}

/* The Changes button says how many files are changed */
static void on_changes_counted(guint n, gpointer data) {
    GitTab* g = data;
    gchar* text = n > 0 ? g_strdup_printf(_("Changes (%u)"), n) : g_strdup(_("Changes"));
    gtk_button_set_label(GTK_BUTTON(g->buttons[GIT_VIEW_CHANGES]), text);
    g_free(text);
}

static void on_view_toggled(GtkToggleButton* button, gpointer data) {
    GitTab* g = data;
    if (!gtk_toggle_button_get_active(button)) return;
    for (gint i = 0; i < GIT_VIEW_COUNT; i++) {
        if (g->buttons[i] == GTK_WIDGET(button)) {
            gtk_stack_set_visible_child_name(GTK_STACK(g->stack), VIEW_NAMES[i]);
        }
    }
}

static GtkWidget* view_button(GitTab* g, GitView view, const gchar* label, const gchar* tooltip) {
    GtkWidget* b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_focus_on_click(b, FALSE);
    if (view > 0) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(g->buttons[0]));
    g_signal_connect(b, "toggled", G_CALLBACK(on_view_toggled), g);
    g->buttons[view] = b;
    return b;
}

/* v in the tab (not in a text field): the next view, with the focus in it */
static gboolean on_page_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)keycode;
    GitTab* g = data;
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) != 0 || keyval != GDK_KEY_v) return FALSE;
    GtkWidget* page = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    GtkWidget* focus = gtk_root_get_focus(gtk_widget_get_root(page));
    if (focus != NULL && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT_VIEW(focus))) return FALSE;
    const gchar* shown = gtk_stack_get_visible_child_name(GTK_STACK(g->stack));
    gint next = 0;
    for (gint i = 0; i < GIT_VIEW_COUNT; i++) {
        if (g_strcmp0(shown, VIEW_NAMES[i]) == 0) next = (i + 1) % GIT_VIEW_COUNT;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->buttons[next]), TRUE);
    focus_route_focus_page(g->app, g->page);
    return TRUE;
}

void git_tab_init(AppData* app) {
    if (app == NULL || app->notebook == NULL) return;

    GitTab* g = g_new0(GitTab, 1);
    app->git = g;
    g->app = app;
    g->changes = git_changes_new(app, on_changes_counted, g);
    g->history = git_history_new(app);
    g->stats = git_stats_view_new(app);
    theme_add_css(".git-switcher { margin: 4px 8px 2px 8px; }");

    GtkWidget* switcher = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(switcher, "linked");
    gtk_widget_add_css_class(switcher, "git-switcher");
    gtk_widget_set_halign(switcher, GTK_ALIGN_START);
    /* TRANSLATORS: the views of the Git tab; v shows the next one */
    gtk_box_append(GTK_BOX(switcher), view_button(g, GIT_VIEW_CHANGES, _("Changes"),
                                                  _("Uncommitted Changes: Staged, Not Staged, Untracked (v: Next View)")));
    gtk_box_append(GTK_BOX(switcher), view_button(g, GIT_VIEW_HISTORY, _("History"),
                                                  _("The Commits, Newest First (v: Next View)")));
    gtk_box_append(GTK_BOX(switcher), view_button(g, GIT_VIEW_STATS, _("Statistics"),
                                                  _("Commits by Day, Hour, Month and Author (v: Next View)")));

    g->stack = gtk_stack_new();
    gtk_widget_set_vexpand(g->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(g->stack), git_changes_widget(g->changes), VIEW_NAMES[GIT_VIEW_CHANGES]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_history_widget(g->history), VIEW_NAMES[GIT_VIEW_HISTORY]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_stats_view_widget(g->stats), VIEW_NAMES[GIT_VIEW_STATS]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->buttons[GIT_VIEW_CHANGES]), TRUE);

    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(tab_box, TRUE);
    gtk_widget_set_vexpand(tab_box, TRUE);
    gtk_box_append(GTK_BOX(tab_box), switcher);
    gtk_box_append(GTK_BOX(tab_box), g->stack);
    g->page = tab_box;
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_page_key), g);
    gtk_widget_add_controller(tab_box, keys);

    GtkWidget* tab_label = icon_label_new("document-open-recent", _("Git"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab_box, tab_label);

    git_tab_update(app, app->base_dir);
}

/* The Git tab in front of its notebook, on view, with the focus in it */
static void show_view(GitTab* g, GitView view) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->buttons[view]), TRUE);
    GtkWidget* nb = gtk_widget_get_ancestor(g->page, GTK_TYPE_NOTEBOOK);
    if (nb != NULL) {
        gint num = gtk_notebook_page_num(GTK_NOTEBOOK(nb), g->page);
        if (num >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), num);
    }
    focus_route_focus_page(g->app, g->page);
}

void git_tab_show_history(AppData* app, const gchar* path) {
    GitTab* g = app != NULL ? app->git : NULL;
    if (g == NULL || path == NULL) return;
    gchar* full = g_canonicalize_filename(path, NULL);
    gchar* start = g_file_test(full, G_FILE_TEST_IS_DIR) ? g_strdup(full) : g_path_get_dirname(full);
    gchar* root = find_git_root(start);
    gchar* rel = git_relative_path(full);
    if (root == NULL || rel == NULL || !gtk_widget_get_visible(g->page) || g_strcmp0(root, g->current_root) != 0) {
        /* The Git tab shows the repository of the current folder only */
        app_notice(app, _("Not in the repository of the current folder"), FALSE);
    } else {
        git_history_set_path(g->history, g_strcmp0(rel, ".") == 0 ? NULL : rel);
        show_view(g, GIT_VIEW_HISTORY);
    }
    g_free(rel);
    g_free(root);
    g_free(start);
    g_free(full);
}

GtkWidget* git_tab_page(AppData* app) {
    return app != NULL && app->git != NULL ? app->git->page : NULL;
}

void git_tab_cleanup(AppData* app) {
    GitTab* g = app != NULL ? app->git : NULL;
    if (g == NULL) return;
    /* The views stay with their widgets until the window goes; their reads
     * stop now */
    git_changes_cancel(g->changes);
    git_history_cancel(g->history);
    git_stats_view_cancel(g->stats);
    g_free(g->current_root);
    g_clear_pointer(&app->git, g_free);
}
