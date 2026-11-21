/* The Statistics view of the Git tab (see git_stats_view.h) */
#include "git/git_stats_view.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "git/git_stats.h"
#include "git/git_stats_charts.h"
#include "widgets/focus_route.h"

/* Authors listed at most (the others are counted in a last line) */
#define AUTHORS_SHOWN 20
/* Data of the view's widget: the GitStatsView, freed with it */
#define STATS_VIEW_KEY "lookin-git-stats-view"

struct GitStatsView {
    AppData* app;
    GtkWidget* widget;       /* the view (owns this struct) */
    GtkWidget* summary;      /* commits, authors, from when to when */
    GtkWidget* charts[3];    /* GitChartKind order */
    GtkWidget* authors;      /* GtkGrid of the authors */
    GitStats* stats;         /* shown, or NULL */
    gchar* root;
    GCancellable* cancellable;
    gboolean stale;          /* read when the view is shown */
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".git-stats { padding: 6px 14px 14px 14px; }"
                  ".git-stats .git-stats-title { color: @fg_strong; font-weight: 600; margin-top: 14px; }"
                  ".git-stats .git-stats-summary { color: @fg_text; }"
                  ".git-stats .git-chart { font-size: @size_small; }"
                  ".git-stats .git-author { color: @fg_text; }"
                  ".git-stats .git-author-dim { color: @fg_subtle; font-feature-settings: \"tnum\"; }");
}

static void cancel_read(GitStatsView* v) {
    if (v->cancellable != NULL) {
        g_cancellable_cancel(v->cancellable);
        g_clear_object(&v->cancellable);
    }
}

static gchar* month_of(gint64 time) {
    GDateTime* t = g_date_time_new_from_unix_local(time);
    gchar* text = t != NULL ? g_date_time_format(t, "%Y-%m") : NULL;
    if (t != NULL) g_date_time_unref(t);
    return text != NULL ? text : g_strdup("");
}

static GtkWidget* cell(GtkWidget* grid, const gchar* text, const gchar* css_class, gint col, gint row,
                       gboolean right) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), right ? 1.0f : 0.0f);
    gtk_widget_add_css_class(label, css_class);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
    return label;
}

/* One row per author, most commits first: name, commits and share, the
 * months of the first and the last commit */
static void show_authors(GitStatsView* v) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(v->authors)) != NULL) gtk_grid_remove(GTK_GRID(v->authors), child);
    const GitStats* s = v->stats;
    if (s == NULL) return;
    guint shown = MIN(s->authors->len, (guint)AUTHORS_SHOWN);
    for (guint i = 0; i < shown; i++) {
        const GitAuthorStats* a = g_ptr_array_index(s->authors, i);
        GtkWidget* name = cell(v->authors, a->name, "git-author", 0, (gint)i, FALSE);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(name), 32);
        gchar* count = g_strdup_printf("%u", a->commits);
        cell(v->authors, count, "git-author", 1, (gint)i, TRUE);
        g_free(count);
        gchar* share = g_strdup_printf("%.0f %%", 100.0 * a->commits / MAX(s->commits, 1u));
        cell(v->authors, share, "git-author-dim", 2, (gint)i, TRUE);
        g_free(share);
        gchar* first = month_of(a->first);
        gchar* last = month_of(a->last);
        gchar* span = g_strcmp0(first, last) == 0 ? g_strdup(first) : g_strdup_printf("%s – %s", first, last);
        cell(v->authors, span, "git-author-dim", 3, (gint)i, FALSE);
        g_free(span);
        g_free(last);
        g_free(first);
    }
    if (s->authors->len > shown) {
        guint rest = s->authors->len - shown;
        gchar* more = g_strdup_printf(ngettext("and %u more author", "and %u more authors", rest), rest);
        cell(v->authors, more, "git-author-dim", 0, (gint)shown, FALSE);
        g_free(more);
    }
}

/* Show stats (taken), the previous ones freed after the charts let go */
static void show_stats(GitStatsView* v, GitStats* stats) {
    for (gint i = 0; i < 3; i++) git_stats_chart_set(v->charts[i], stats);
    git_stats_free(v->stats);
    v->stats = stats;
    show_authors(v);
    if (stats == NULL) return;
    if (stats->commits == 0) {
        gtk_label_set_text(GTK_LABEL(v->summary), _("No commits in this repository."));
        return;
    }
    gchar* first = month_of(stats->first);
    gchar* last = month_of(stats->last);
    gchar* commits = g_strdup_printf(ngettext("%u commit", "%u commits", stats->commits), stats->commits);
    gchar* authors = g_strdup_printf(ngettext("%u author", "%u authors", stats->authors->len), stats->authors->len);
    /* TRANSLATORS: summary of the Statistics view: "120 commits", "3 authors", first and last months */
    gchar* text = g_strdup_printf(_("%s by %s, from %s to %s"), commits, authors, first, last);
    gtk_label_set_text(GTK_LABEL(v->summary), text);
    g_free(text);
    g_free(authors);
    g_free(commits);
    g_free(last);
    g_free(first);
}

typedef struct {
    gchar* root;
    GitStats* stats;
} ReadTask;

static void read_task_free(gpointer data) {
    ReadTask* td = data;
    g_free(td->root);
    git_stats_free(td->stats);
    g_free(td);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    ReadTask* td = data;
    td->stats = git_stats_read(td->root, cancellable);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

/* The task reaches its view through its callback data: it is cancelled
 * before the view is freed, and a cancelled task's callback returns first */
static void on_read_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read, or exit */
    GitStatsView* v = data;
    ReadTask* td = g_task_get_task_data(task);
    if (td->stats == NULL) {
        show_stats(v, NULL);
        gtk_label_set_text(GTK_LABEL(v->summary), _("The history of this repository cannot be read."));
        return;
    }
    show_stats(v, g_steal_pointer(&td->stats));
}

static void start_read(GitStatsView* v) {
    cancel_read(v);
    v->stale = FALSE;
    v->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(v->root);
    if (v->stats == NULL) gtk_label_set_text(GTK_LABEL(v->summary), _("Reading the history…"));
    GTask* task = g_task_new(NULL, v->cancellable, on_read_ready, v);
    g_task_set_task_data(task, td, read_task_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

void git_stats_view_set_root(GitStatsView* v, const gchar* root, gboolean reload) {
    if (v == NULL) return;
    if (g_strcmp0(v->root, root) != 0) {
        cancel_read(v);
        g_free(v->root);
        v->root = g_strdup(root);
        show_stats(v, NULL);
        gtk_label_set_text(GTK_LABEL(v->summary), "");
    } else if (!reload) {
        return;
    }
    if (v->root == NULL) return;
    v->stale = !gtk_widget_get_mapped(v->widget);
    if (!v->stale) start_read(v);
}

static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitStatsView* v = data;
    if (v->stale && v->root != NULL) start_read(v);
}

void git_stats_view_cancel(GitStatsView* v) {
    if (v != NULL) cancel_read(v);
}

/* With the view's widget (the charts are gone first: they borrow stats) */
static void git_stats_view_free(gpointer data) {
    GitStatsView* v = data;
    cancel_read(v);
    git_stats_free(v->stats);
    g_free(v->root);
    g_free(v);
}

static void add_section(GtkWidget* box, const gchar* title, GtkWidget* content) {
    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "git-stats-title");
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), content);
}

GitStatsView* git_stats_view_new(AppData* app) {
    ensure_css();
    GitStatsView* v = g_new0(GitStatsView, 1);
    v->app = app;

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "git-stats");
    v->summary = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->summary), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(v->summary), TRUE);
    gtk_widget_add_css_class(v->summary, "git-stats-summary");
    gtk_box_append(GTK_BOX(box), v->summary);

    v->charts[GIT_CHART_CALENDAR] = git_stats_chart_new(GIT_CHART_CALENDAR);
    v->charts[GIT_CHART_PUNCH] = git_stats_chart_new(GIT_CHART_PUNCH);
    v->charts[GIT_CHART_MONTHS] = git_stats_chart_new(GIT_CHART_MONTHS);
    add_section(box, _("The Last Twelve Months"), v->charts[GIT_CHART_CALENDAR]);
    add_section(box, _("By Day of the Week and Hour (Author's Time)"), v->charts[GIT_CHART_PUNCH]);
    add_section(box, _("By Month"), v->charts[GIT_CHART_MONTHS]);

    v->authors = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(v->authors), 16);
    gtk_grid_set_row_spacing(GTK_GRID(v->authors), 2);
    add_section(box, _("Authors"), v->authors);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    focus_route_mark_focus(scroll);

    v->widget = scroll;
    g_object_set_data_full(G_OBJECT(scroll), STATS_VIEW_KEY, v, git_stats_view_free);
    g_signal_connect(scroll, "map", G_CALLBACK(on_view_map), v);
    return v;
}

GtkWidget* git_stats_view_widget(GitStatsView* v) {
    return v != NULL ? v->widget : NULL;
}
