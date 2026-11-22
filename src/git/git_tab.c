#include "git/git_tab.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "git/git_changes.h"
#include "git/git_history.h"
#include "git/git_mirror.h"
#include "git/git_remote.h"
#include "core/vfs.h"
#include "git/git_repo_view.h"
#include "git/git_stash_view.h"
#include "git/git_stats_view.h"
#include "git/git_utils.h"
#include "widgets/focus_route.h"

#include <string.h>

/* The views of the Git tab, in the order of their buttons */
typedef enum { GIT_VIEW_CHANGES, GIT_VIEW_HISTORY, GIT_VIEW_STATS, GIT_VIEW_REPO, GIT_VIEW_STASH_TAGS, GIT_VIEW_COUNT } GitView;

static const gchar* const VIEW_NAMES[GIT_VIEW_COUNT] = { "changes", "history", "statistics", "repository",
                                                         "stashes-tags" };

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
    GitRepoView* repo;          /* the Repository view (freed with its widget) */
    GitStashView* stash_tags;   /* the Stashes & Tags view (freed with its widget) */
    gchar* current_root;        /* repository shown, or NULL */
    gchar* remote_dir;          /* a remote machine's folder shown (its .git copied), or NULL */
    gchar* remote_top;          /* the top of its work tree (as seen here), or NULL */
    guint remote_serial;        /* the last sync asked: older ones are dropped */
};

static void set_views(GitTab* g, const gchar* root, gboolean reload);

/* A remote machine's repository: its top found and its .git copied
 * (git_mirror.h) off the interface thread */
typedef struct {
    AppData* app;
    guint serial;
    gchar* dir;
    gchar* top;    /* found by the worker */
    gchar* mirror; /* the copy, once synced */
    guint copied;
    gchar* error;
} RemoteSync;

static void remote_sync_free(gpointer data) {
    RemoteSync* r = data;
    g_free(r->dir);
    g_free(r->top);
    g_free(r->mirror);
    g_free(r->error);
    g_free(r);
}

static void remote_sync_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)task;
    (void)source;
    RemoteSync* r = data;
    r->top = git_remote_top(r->dir, cancel);
    if (r->top == NULL) return; /* in no repository */
    GError* error = NULL;
    if (git_mirror_sync(r->top, cancel, &r->copied, &error)) {
        r->mirror = git_mirror_root(r->top);
    } else {
        r->error = g_strdup(error->message);
        g_error_free(error);
    }
}

static void on_remote_synced(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    RemoteSync* r = g_task_get_task_data(G_TASK(res));
    GitTab* g = r->app->git;
    if (g == NULL || r->serial != g->remote_serial) return; /* another folder since */
    if (r->error != NULL) {
        gchar* msg = g_strdup_printf(_("The Git history of the machine could not be read: %s"), r->error);
        app_notice(r->app, msg, FALSE);
        g_free(msg);
    }
    g_free(g->remote_top);
    g->remote_top = g_strdup(r->mirror != NULL ? r->top : NULL);
    gtk_widget_set_visible(g->page, r->mirror != NULL);
    set_views(g, r->mirror, TRUE);
}

/* Show the repository of dir, a remote machine's folder: its .git copied
 * first (only what changed after the first time) */
static void update_remote(GitTab* g, const gchar* dir, gboolean reload) {
    if (!reload && g_strcmp0(g->remote_dir, dir) == 0) { /* the same: the history read again on reload only */
        if (g->remote_top != NULL) git_changes_set_root(g->changes, g->remote_top); /* the changes, each time */
        return;
    }
    g_free(g->remote_dir);
    g->remote_dir = g_strdup(dir);
    RemoteSync* r = g_new0(RemoteSync, 1);
    r->app = g->app;
    r->serial = ++g->remote_serial;
    r->dir = g_strdup(dir);
    GTask* task = g_task_new(NULL, NULL, on_remote_synced, NULL);
    g_task_set_task_data(task, r, remote_sync_free);
    g_task_run_in_thread(task, remote_sync_thread);
    g_object_unref(task);
}

/* The views on the repository at root (NULL: none); for a remote machine,
 * root is the copy of its .git (git_mirror.h) */
static void set_views(GitTab* g, const gchar* root, gboolean reload) {
    g_free(g->current_root);
    g->current_root = g_strdup(root);
    /* Files may have changed with each update: the Changes view reads again
     * (on a remote machine, the work tree there: git run on it) */
    git_changes_set_root(g->changes, g->remote_dir != NULL ? (root != NULL ? g->remote_top : NULL) : root);
    git_history_set_root(g->history, root, reload);
    git_stats_view_set_root(g->stats, root, reload);
    git_repo_view_set_root(g->repo, root, reload);
    git_stash_view_set_root(g->stash_tags, root);
}

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

    if (dir != NULL && vfs_is_remote(dir)) {
        update_remote(g, dir, reload);
        return;
    }
    g_clear_pointer(&g->remote_dir, g_free);
    g_clear_pointer(&g->remote_top, g_free);
    g->remote_serial++; /* a remote sync running is for another folder */
    gchar* root = find_git_root(dir);
    gtk_widget_set_visible(g->page, root != NULL);
    set_views(g, root, reload);
    g_free(root);
}

void git_tab_reload(AppData* app) {
    if (app == NULL) return;
    update_root(app, app->base_dir, TRUE);
}

void git_tab_update(AppData* app, const gchar* dir) {
    update_root(app, dir, FALSE);
}

/* The Changes button says how many files are changed, when known */
static void on_changes_counted(guint n, gpointer data) {
    GitTab* g = data;
    gchar* text = n > 0 && n != G_MAXUINT ? g_strdup_printf(_("Changes (%u)"), n) : g_strdup(_("Changes"));
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
    g->repo = git_repo_view_new(app);
    g->stash_tags = git_stash_view_new(app);
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
    gtk_box_append(GTK_BOX(switcher), view_button(g, GIT_VIEW_REPO, _("Repository"),
                                                  _("Identity, Remotes, Branches and Configuration (v: Next View)")));
    gtk_box_append(GTK_BOX(switcher), view_button(g, GIT_VIEW_STASH_TAGS, _("Stashes & Tags"),
                                                  _("The Stashes and the Tags, Newest First (v: Next View)")));

    g->stack = gtk_stack_new();
    gtk_widget_set_vexpand(g->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(g->stack), git_changes_widget(g->changes), VIEW_NAMES[GIT_VIEW_CHANGES]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_history_widget(g->history), VIEW_NAMES[GIT_VIEW_HISTORY]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_stats_view_widget(g->stats), VIEW_NAMES[GIT_VIEW_STATS]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_repo_view_widget(g->repo), VIEW_NAMES[GIT_VIEW_REPO]);
    gtk_stack_add_named(GTK_STACK(g->stack), git_stash_view_widget(g->stash_tags), VIEW_NAMES[GIT_VIEW_STASH_TAGS]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->buttons[GIT_VIEW_HISTORY]), TRUE); /* the view shown first */

    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(tab_box, TRUE);
    gtk_widget_set_vexpand(tab_box, TRUE);
    /* The buttons scroll sideways in a narrow panel instead of widening it */
    GtkWidget* switcher_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(switcher_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(switcher_scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(switcher_scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(switcher_scroll), switcher);
    gtk_box_append(GTK_BOX(tab_box), switcher_scroll);
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
    if (g->remote_dir != NULL) { /* a remote machine's repository: its path below the top shown */
        gsize n = g->remote_top != NULL ? strlen(g->remote_top) : 0;
        if (n == 0 || !gtk_widget_get_visible(g->page) || strncmp(full, g->remote_top, n) != 0 ||
            (full[n] != '\0' && full[n] != '/')) {
            app_notice(app, _("Not in the repository of the current folder"), FALSE);
        } else {
            git_history_set_path(g->history, full[n] == '/' ? full + n + 1 : NULL);
            show_view(g, GIT_VIEW_HISTORY);
        }
        g_free(full);
        return;
    }
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
    git_repo_view_cancel(g->repo);
    git_stash_view_cancel(g->stash_tags);
    g_free(g->current_root);
    g_free(g->remote_dir);
    g_free(g->remote_top);
    g_clear_pointer(&app->git, g_free);
}
