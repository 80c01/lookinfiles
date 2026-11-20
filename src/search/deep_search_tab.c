/* The Deep Search tab (see deep_search_tab.h) */
#include "search/deep_search_tab.h"
#include "search/deep_search.h"
#include "search/deep_search_charts.h"
#include "search/deep_search_date.h"
#include "search/deep_search_list.h"
#include "search/deep_search_preview.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "editor/editor_swap.h"
#include "explorer/file_list.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"


#define PAGE_KEY "lookin-deep-search-tab"
#define TICK_MS 200 /* results and counters shown again while the search runs */

/* The option toggles of the content, and their keys (with Alt), as in
 * Search in Files */
enum { OPT_CASE, OPT_WORD, OPT_REGEX, OPT_FUZZY, OPT_COUNT };
static const guint OPTION_KEYS[OPT_COUNT] = {GDK_KEY_m, GDK_KEY_w, GDK_KEY_r, GDK_KEY_l};

/* One search: it lives as long as its worker */
typedef struct {
    DeepSearch* search; /* owned */
} SearchJob;

typedef struct {
    AppData* app;
    GtkWidget* page;  /* the notebook page (app->deep_search_page) */
    GtkWidget* label; /* tab label (owned ref: kept while the page is detached) */

    /* The form */
    GtkWidget* folder;
    GtkWidget* name;
    GtkWidget* extensions;
    GtkWidget* family;    /* GtkDropDown: any, then the FileFamily values */
    GtkWidget* content;
    GtkWidget* options[OPT_COUNT];
    GtkWidget* kind;      /* GtkDropDown: DeepKind */
    GtkWidget* min_size;
    GtkWidget* max_size;
    GtkWidget* after;
    GtkWidget* before;
    GtkWidget* exclude;
    GtkWidget* depth;
    GtkWidget* hidden;
    GtkWidget* one_fs;
    GtkWidget* search;    /* buttons */
    GtkWidget* stop;
    GtkWidget* beside;    /* in the preview notebook: the preview to the right */
    GtkWidget* error;     /* why the criteria cannot run */

    GtkWidget* status;    /* counters, then the summary */
    GtkWidget* current;   /* folder being read */
    GtkWidget* list;      /* deep_search_list */
    GtkWidget* charts;    /* deep_search_charts, under the list */
    GtkWidget* split;     /* GtkPaned of the list and the charts */
    gboolean split_set;   /* its first position given */
    gchar* status_text;   /* the status without the rows listed */
    gboolean ranged;      /* the charts limit the rows listed */

    DeepSearch* running;  /* owned by its job, else NULL */
    GCancellable* cancel;
    guint timer;
    gint64 started;       /* monotonic time the search started */

    DeepSearchPreview* preview;
} DeepSearchTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".deep-search { background-color: @bg_view; }"
                  ".deep-search .ds-header { padding: 4px 10px; color: @fg; }"
                  ".deep-search .ds-form { padding: 4px 12px 6px 12px; color: @fg_text; }"
                  ".deep-search .ds-field { color: @fg_subtle; }"
                  ".deep-search .ds-error { color: @error; padding: 0 12px 4px 12px; }"
                  ".deep-search .ds-status { color: @fg_text; padding: 2px 12px 0 12px; }"
                  ".deep-search .ds-current { color: @fg_subtle; font-size: @size_small; padding: 0 12px 6px 12px; }"
                  ".deep-search .ds-off { opacity: 0.45; }"
                  ".deep-search .ds-num { font-feature-settings: \"tnum\"; }"
                  ".deep-search .ds-code { font-family: @font_code; }"
                  ".deep-search .ds-charts { padding: 4px 12px 8px 12px; }"
                  ".deep-search .ds-chart-title { color: @fg_strong; font-weight: bold; }"
                  ".deep-search .ds-chart-hint { color: @fg_subtle; font-size: @size_small; }"
                  ".deep-search .histogram { font-size: @size_small; }");
}

static DeepSearchTab* tab_state(AppData* app) {
    return app != NULL && app->deep_search_page != NULL
               ? g_object_get_data(G_OBJECT(app->deep_search_page), PAGE_KEY)
               : NULL;
}

static void set_label(GtkWidget* label, gchar* text) {
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(label)), text) != 0) gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

static const gchar* field_text(GtkWidget* entry) {
    return gtk_editable_get_text(GTK_EDITABLE(entry));
}

static gboolean option_on(DeepSearchTab* dt, gint opt) {
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dt->options[opt]));
}

/* "1.2 s", "2 min 05 s" */
static gchar* format_duration(gint64 us) {
    gdouble s = (gdouble)us / G_USEC_PER_SEC;
    if (s < 60.0) return g_strdup_printf(_("%.1f s"), s);
    guint total = (guint)s;
    return g_strdup_printf(_("%u min %02u s"), total / 60, total % 60);
}

/* fmt (from ngettext(), one %s) with n */
static gchar* with_count(const gchar* fmt, guint64 n) {
    gchar* num = g_strdup_printf("%" G_GUINT64_FORMAT, n);
    gchar* text = g_strdup_printf(fmt, num);
    g_free(num);
    return text;
}

#define PLURAL_N(n) ((gulong)MIN((n), G_MAXULONG))

/* "87 found · 56789 entries in 1234 folders · 120 MB read" */
static gchar* counters_text(const DeepSearchProgress* p, gboolean content) {
    /* TRANSLATORS: %s is a number of entries found */
    gchar* found = with_count(ngettext("%s found", "%s found", PLURAL_N(p->found)), p->found);
    /* TRANSLATORS: %s is a number of files and folders tested */
    gchar* entries = with_count(ngettext("%s entry", "%s entries", PLURAL_N(p->entries)), p->entries);
    /* TRANSLATORS: %s is a number of folders read */
    gchar* dirs = with_count(ngettext("%s folder", "%s folders", PLURAL_N(p->dirs)), p->dirs);
    GString* s = g_string_new(NULL);
    /* TRANSLATORS: entries found · entries tested in folders read */
    g_string_append_printf(s, _("%1$s · %2$s in %3$s"), found, entries, dirs);
    if (content) {
        gchar* size = g_format_size(p->bytes);
        g_string_append(s, " · ");
        /* TRANSLATORS: %s is a size, as "120 MB" */
        g_string_append_printf(s, _("%s read"), size);
        g_free(size);
    }
    if (p->unreadable > 0) {
        /* TRANSLATORS: %s is a number of files or folders that could not be read */
        gchar* n = with_count(ngettext("%s unreadable", "%s unreadable", PLURAL_N(p->unreadable)), p->unreadable);
        g_string_append_printf(s, " · %s", n);
        g_free(n);
    }
    if (content && p->binary > 0) {
        /* TRANSLATORS: %s is a number of files */
        gchar* n = with_count(ngettext("%s binary file skipped", "%s binary files skipped", PLURAL_N(p->binary)),
                              p->binary);
        g_string_append_printf(s, " · %s", n);
        g_free(n);
    }
    g_free(found);
    g_free(entries);
    g_free(dirs);
    return g_string_free(s, FALSE);
}

/* The status: text (taken, NULL: the one kept), then the rows listed when
 * the charts limit them */
static void set_status(DeepSearchTab* dt, gchar* text) {
    if (text != NULL) {
        g_free(dt->status_text);
        dt->status_text = text;
    }
    if (!dt->ranged) {
        gtk_label_set_text(GTK_LABEL(dt->status), dt->status_text != NULL ? dt->status_text : "");
        return;
    }
    guint shown = deep_search_list_shown(dt->list);
    /* TRANSLATORS: %s is a number of results, those of the bars chosen in the charts */
    gchar* listed = with_count(ngettext("%s listed", "%s listed", shown), shown);
    gchar* full = g_strdup_printf("%s · %s", dt->status_text != NULL ? dt->status_text : "", listed);
    gtk_label_set_text(GTK_LABEL(dt->status), full);
    g_free(full);
    g_free(listed);
}

static void sync_buttons(DeepSearchTab* dt) {
    gtk_widget_set_visible(dt->stop, dt->running != NULL);
    gtk_widget_set_visible(dt->search, dt->running == NULL);
}

/* The results found since the last time, and the counters */
static void show_progress(DeepSearchTab* dt, DeepSearch* search, gboolean done) {
    DeepSearchProgress p;
    GPtrArray* hits = deep_search_take(search, &p);
    deep_search_charts_add(dt->charts, hits);
    deep_search_list_add(dt->list, hits);
    deep_search_charts_refresh(dt->charts);
    gboolean content = field_text(dt->content)[0] != '\0';
    gchar* counters = counters_text(&p, content);
    gchar* time = format_duration(g_get_monotonic_time() - dt->started);
    GString* s = g_string_new(counters);
    g_string_append_printf(s, " · %s", time);
    switch (done ? p.state : DEEP_SEARCH_RUNNING) {
    case DEEP_SEARCH_STOPPED: g_string_append_printf(s, " · %s", _("stopped before the end")); break;
    case DEEP_SEARCH_FULL:
        g_string_append(s, " · ");
        /* TRANSLATORS: %u is the most results kept */
        g_string_append_printf(s, _("stopped at %u results"), (guint)DEEP_SEARCH_MAX_HITS);
        break;
    case DEEP_SEARCH_NO_FOLDER: g_string_assign(s, _("The folder cannot be read")); break;
    case DEEP_SEARCH_RUNNING:
    case DEEP_SEARCH_DONE: break;
    }
    set_status(dt, g_string_free(s, FALSE));
    set_label(dt->current, g_strdup(!done && p.current != NULL ? p.current : ""));
    gtk_widget_set_visible(dt->current, !done);
    g_free(counters);
    g_free(time);
    deep_search_progress_clear(&p);
}

static gboolean on_tick(gpointer data) {
    DeepSearchTab* dt = data;
    if (dt->running == NULL) {
        dt->timer = 0;
        return G_SOURCE_REMOVE;
    }
    show_progress(dt, dt->running, FALSE);
    return G_SOURCE_CONTINUE;
}

static void job_free(gpointer data) {
    SearchJob* job = data;
    deep_search_free(job->search);
    g_free(job);
}

static void search_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    SearchJob* job = data;
    deep_search_run(job->search, cancel);
    g_task_return_boolean(task, TRUE);
}

static void on_search_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)data;
    DeepSearchTab* dt = g_object_get_data(source, PAGE_KEY);
    SearchJob* job = g_task_get_task_data(G_TASK(res));
    g_task_propagate_boolean(G_TASK(res), NULL);
    if (dt == NULL || job->search != dt->running) return; /* replaced or closed */
    dt->running = NULL;
    if (dt->timer != 0) {
        g_source_remove(dt->timer);
        dt->timer = 0;
    }
    show_progress(dt, job->search, TRUE);
    sync_buttons(dt);
}

static void stop_search(DeepSearchTab* dt) {
    if (dt->running != NULL) g_cancellable_cancel(dt->cancel);
}

/* The form */

static void show_error(DeepSearchTab* dt, GtkWidget* field, gchar* text) {
    gtk_widget_set_visible(dt->error, text != NULL);
    set_label(dt->error, text != NULL ? text : g_strdup(""));
    if (field != NULL) {
        gtk_widget_add_css_class(field, "error");
        gtk_widget_grab_focus(field);
    }
}

static void clear_errors(DeepSearchTab* dt) {
    GtkWidget* fields[] = {dt->folder,   dt->content, dt->min_size, dt->max_size,
                           dt->after,    dt->before,  dt->depth};
    for (gsize i = 0; i < G_N_ELEMENTS(fields); i++) gtk_widget_remove_css_class(fields[i], "error");
    show_error(dt, NULL, NULL);
}

static gboolean read_size(DeepSearchTab* dt, GtkWidget* field, gint64* out) {
    if (deep_query_parse_size(field_text(field), out)) return TRUE;
    /* TRANSLATORS: %s is the size typed */
    show_error(dt, field, g_strdup_printf(_("Not a size: %s (sizes look like 42, 500k, 10M, 2G)"), field_text(field)));
    return FALSE;
}

static gboolean read_date(DeepSearchTab* dt, GtkWidget* field, GDateTime* now, gboolean end_of_day, gint64* out) {
    if (deep_query_parse_date(field_text(field), now, end_of_day, out)) return TRUE;
    /* TRANSLATORS: %s is the date typed; keep today, yesterday and the units in English, they are what is read */
    show_error(dt, field, g_strdup_printf(_("Not a date: %s (dates look like 2026-11-01, today, yesterday, 7d, 3h, "
                                            "2w, 6m, 1y)"),
                                          field_text(field)));
    return FALSE;
}

/* The criteria of the form into q; FALSE (the error shown) when one is
 * wrong */
static gboolean read_form(DeepSearchTab* dt, DeepQuery* q) {
    deep_query_init(q);
    q->name = g_strdup(field_text(dt->name));
    q->extensions = g_strdup(field_text(dt->extensions));
    q->family = (gint)gtk_drop_down_get_selected(GTK_DROP_DOWN(dt->family)) - 1; /* 0: any */
    q->exclude = g_strdup(field_text(dt->exclude));
    if (field_text(dt->content)[0] != '\0') q->content = g_strdup(field_text(dt->content));
    q->content_opts.match_case = option_on(dt, OPT_CASE);
    q->content_opts.whole_word = option_on(dt, OPT_WORD);
    q->content_opts.regex = option_on(dt, OPT_REGEX);
    q->content_opts.fuzzy = option_on(dt, OPT_FUZZY);
    q->kind = (DeepKind)gtk_drop_down_get_selected(GTK_DROP_DOWN(dt->kind));
    q->hidden = gtk_check_button_get_active(GTK_CHECK_BUTTON(dt->hidden));
    q->one_filesystem = gtk_check_button_get_active(GTK_CHECK_BUTTON(dt->one_fs));
    if (!read_size(dt, dt->min_size, &q->min_size) || !read_size(dt, dt->max_size, &q->max_size)) return FALSE;
    GDateTime* now = g_date_time_new_now_local();
    gboolean dates = read_date(dt, dt->after, now, FALSE, &q->after) && read_date(dt, dt->before, now, TRUE, &q->before);
    g_date_time_unref(now);
    if (!dates) return FALSE;
    gchar* depth = g_strstrip(g_strdup(field_text(dt->depth)));
    gchar* end = NULL;
    gint64 levels = depth[0] != '\0' ? g_ascii_strtoll(depth, &end, 10) : 0;
    gboolean depth_ok = depth[0] == '\0' || (end != NULL && *end == '\0' && levels >= 1 && levels <= 10000);
    g_free(depth);
    if (!depth_ok) {
        show_error(dt, dt->depth, g_strdup(_("The depth is a number of levels: 1 for the folder's own entries")));
        return FALSE;
    }
    q->max_depth = (gint)levels;
    if (q->content != NULL) {
        gchar* why = text_search_check(q->content, &q->content_opts);
        if (why != NULL) {
            /* TRANSLATORS: %s is the error of the regular expression */
            show_error(dt, dt->content, g_strdup_printf(_("Invalid regular expression: %s"), why));
            g_free(why);
            return FALSE;
        }
    }
    gchar* why = deep_query_check(q); /* the sizes or the dates in the wrong order */
    if (why != NULL) {
        show_error(dt, NULL, why);
        return FALSE;
    }
    return TRUE;
}

/* Search with the criteria of the form, from the start */
static void start_search(DeepSearchTab* dt) {
    clear_errors(dt);
    gchar* root = g_strstrip(g_strdup(field_text(dt->folder)));
    if (root[0] == '~') {
        gchar* home = g_build_filename(g_get_home_dir(), root + 1, NULL);
        g_free(root);
        root = home;
    }
    if (root[0] == '\0') {
        show_error(dt, dt->folder, g_strdup(_("Type the folder to search")));
        g_free(root);
        return;
    }
    if (!g_file_test(root, G_FILE_TEST_IS_DIR)) {
        /* TRANSLATORS: %s is a folder path */
        show_error(dt, dt->folder, g_strdup_printf(_("No such folder: %s"), root));
        g_free(root);
        return;
    }
    DeepQuery q;
    if (!read_form(dt, &q)) {
        deep_query_clear(&q);
        g_free(root);
        return;
    }
    if (dt->cancel != NULL) {
        g_cancellable_cancel(dt->cancel);
        g_object_unref(dt->cancel);
    }
    dt->cancel = g_cancellable_new();
    SearchJob* job = g_new0(SearchJob, 1);
    job->search = deep_search_new(root, &q);
    deep_search_list_reset(dt->list, deep_search_root(job->search), q.content != NULL);
    deep_search_charts_reset(dt->charts);
    dt->ranged = FALSE;
    deep_query_clear(&q);
    g_free(root);
    dt->running = job->search;
    dt->started = g_get_monotonic_time();
    GTask* task = g_task_new(dt->page, dt->cancel, on_search_done, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, search_thread);
    g_object_unref(task);
    set_status(dt, g_strdup(_("Searching…")));
    gtk_widget_set_visible(dt->current, TRUE);
    if (dt->timer == 0) dt->timer = g_timeout_add(TICK_MS, on_tick, dt);
    sync_buttons(dt);
}

/* Results */

static void on_reveal(gpointer data, const gchar* path) {
    DeepSearchTab* dt = data;
    reveal_path_in_tree(dt->app, path);
    if (dt->app->tree_view != NULL) gtk_widget_grab_focus(dt->app->tree_view);
}

static void on_open(gpointer data, const gchar* path, gint line, gint column) {
    DeepSearchTab* dt = data;
    if (editor_open_at(dt->app, path, line, column) == NULL) on_reveal(dt, path);
}

static void on_preview(gpointer data, const gchar* path, gint line, gboolean asked) {
    deep_search_preview_result(((DeepSearchTab*)data)->preview, path, line, asked);
}

/* The bars chosen in the charts: the rows listed */
static void on_charts_changed(gpointer data, const DeepSearchRange* range) {
    DeepSearchTab* dt = data;
    dt->ranged = range != NULL;
    deep_search_list_set_range(dt->list, range);
    set_status(dt, NULL);
}

/* The charts at their natural height (twice as much one over the other) */
static void charts_natural_height(DeepSearchTab* dt) {
    gint width = gtk_widget_get_width(dt->split), natural = 0, max = 0;
    if (width <= 0) return;
    gtk_widget_measure(dt->charts, GTK_ORIENTATION_VERTICAL, width, NULL, &natural, NULL, NULL);
    g_object_get(dt->split, "max-position", &max, NULL);
    gtk_paned_set_position(GTK_PANED(dt->split), MAX(max - natural, 0));
}

/* The charts went side by side or one over the other: their height again,
 * whatever the pane was dragged to */
static void on_charts_layout(gpointer data, gboolean side_by_side) {
    (void)side_by_side;
    charts_natural_height(data);
}

/* The first time the list and the charts have a height */
static void on_split_sized(GObject* split, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    DeepSearchTab* dt = data;
    gint max = 0;
    g_object_get(split, "max-position", &max, NULL);
    if (dt->split_set || max < 100) return;
    dt->split_set = TRUE;
    charts_natural_height(dt);
}

/* Buttons and keys */

#define READY_TEXT _("Fill in the criteria, then press Enter")

static void set_entry(GtkWidget* e, const gchar* text) {
    gtk_editable_set_text(GTK_EDITABLE(e), text != NULL ? text : "");
}

static gchar* dup_nonempty(const gchar* text, gboolean strip) {
    if (text == NULL || text[0] == '\0') return NULL;
    gchar* s = g_strdup(text);
    if (strip) g_strstrip(s);
    if (s[0] == '\0') {
        g_free(s);
        return NULL;
    }
    return s;
}

/* Fill the criteria widgets from s (without touching the folder) */
static void apply_form(DeepSearchTab* dt, const SavedDeepSearch* s) {
    set_entry(dt->name, s->name);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dt->family),
                               s->family >= 0 && s->family < FILE_FAMILY_OTHER ? (guint)(s->family + 1) : 0);
    set_entry(dt->extensions, s->family >= 0 ? "" : s->extensions);
    set_entry(dt->content, s->content);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_CASE]), s->content_opts.match_case);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_WORD]), s->content_opts.whole_word);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_REGEX]), s->content_opts.regex);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_FUZZY]),
                                 !s->content_opts.regex && s->content_opts.fuzzy);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dt->kind), (guint)s->kind);
    set_entry(dt->min_size, s->min_size);
    set_entry(dt->max_size, s->max_size);
    set_entry(dt->after, s->after);
    set_entry(dt->before, s->before);
    set_entry(dt->exclude, s->exclude);
    set_entry(dt->depth, s->depth);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dt->hidden), s->hidden);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dt->one_fs), s->one_filesystem);
}

/* Stop any running search and clear the previous results */
static void clear_results(DeepSearchTab* dt) {
    stop_search(dt);
    dt->running = NULL;
    if (dt->timer != 0) {
        g_source_remove(dt->timer);
        dt->timer = 0;
    }
    sync_buttons(dt);
    clear_errors(dt);
    deep_search_list_reset(dt->list, NULL, FALSE);
    deep_search_charts_reset(dt->charts);
    dt->ranged = FALSE;
    set_status(dt, g_strdup(READY_TEXT));
    set_label(dt->current, g_strdup(""));
}

/* Every criterion back to its default (the folder stays), no result */
static void reset_search(DeepSearchTab* dt) {
    clear_results(dt);
    SavedDeepSearch empty;
    saved_deep_search_init(&empty);
    apply_form(dt, &empty);
    gtk_widget_grab_focus(dt->name);
}

static void on_reset_clicked(GtkButton* button, gpointer data) {
    (void)button;
    reset_search(data);
}

static void on_search_clicked(GtkButton* button, gpointer data) {
    (void)button;
    start_search(data);
}

static void on_entry_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    start_search(data);
}

static void on_stop_clicked(GtkButton* button, gpointer data) {
    (void)button;
    stop_search(data);
}

static void on_bookmark_clicked(GtkButton* button, gpointer data) {
    (void)button;
    app_emit(((DeepSearchTab*)data)->app, APP_EVENT_SAVE_SEARCH_BOOKMARK);
}

static void on_parent_clicked(GtkButton* button, gpointer data) {
    (void)button;
    DeepSearchTab* dt = data;
    gchar* path = g_strstrip(g_strdup(field_text(dt->folder)));
    gchar* parent = g_path_get_dirname(path);
    gtk_editable_set_text(GTK_EDITABLE(dt->folder), parent);
    g_free(parent);
    g_free(path);
}

/* Regex and letters in order exclude each other */
static void on_option_toggled(GtkToggleButton* button, gpointer data) {
    DeepSearchTab* dt = data;
    if (gtk_toggle_button_get_active(button)) {
        if (GTK_WIDGET(button) == dt->options[OPT_REGEX]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_FUZZY]), FALSE);
        } else if (GTK_WIDGET(button) == dt->options[OPT_FUZZY]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[OPT_REGEX]), FALSE);
        }
    }
    gtk_widget_set_sensitive(dt->options[OPT_WORD], !option_on(dt, OPT_FUZZY));
}

/* Extensions or a category, not both: a category empties and dims the
 * extensions; a click in them (or typing) sets "Any category" back */
static void on_family_changed(GObject* drop_down, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    DeepSearchTab* dt = data;
    gboolean category = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop_down)) > 0;
    if (category) {
        gtk_editable_set_text(GTK_EDITABLE(dt->extensions), "");
        gtk_widget_add_css_class(dt->extensions, "ds-off");
    } else {
        gtk_widget_remove_css_class(dt->extensions, "ds-off");
    }
}

static void clear_family(DeepSearchTab* dt) {
    if (gtk_drop_down_get_selected(GTK_DROP_DOWN(dt->family)) > 0) gtk_drop_down_set_selected(GTK_DROP_DOWN(dt->family), 0);
}

static void on_extensions_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    clear_family(data);
}

static void on_extensions_changed(GtkEditable* editable, gpointer data) {
    if (gtk_editable_get_text(editable)[0] != '\0') clear_family(data);
}

static gboolean is_field(DeepSearchTab* dt, GtkWidget* w) {
    GtkWidget* fields[] = {dt->folder, dt->name,     dt->extensions, dt->content, dt->min_size, dt->max_size,
                           dt->after,  dt->before,   dt->exclude,    dt->depth};
    for (gsize i = 0; i < G_N_ELEMENTS(fields); i++) {
        if (w == fields[i] || (w != NULL && gtk_widget_is_ancestor(w, fields[i]))) return TRUE;
    }
    return FALSE;
}

static gboolean on_page_key(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)keycode;
    DeepSearchTab* dt = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    /* Alt+M / W / R / L: the content options */
    if (mods == GDK_ALT_MASK) {
        guint key = gdk_keyval_to_lower(keyval);
        for (gint i = 0; i < OPT_COUNT; i++) {
            if (key != OPTION_KEYS[i]) continue;
            if (gtk_widget_get_sensitive(dt->options[i])) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->options[i]), !option_on(dt, i));
            }
            return TRUE;
        }
        return FALSE;
    }
    if (mods != 0) return FALSE;
    if (keyval == GDK_KEY_F5) {
        start_search(dt);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape && dt->running != NULL) {
        stop_search(dt);
        return TRUE;
    }
    /* Down in a field: to the results */
    GtkWidget* focus = gtk_root_get_focus(gtk_widget_get_root(dt->page));
    if ((keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) && is_field(dt, focus) &&
        deep_search_list_count(dt->list) > 0) {
        (void)controller;
        gtk_widget_grab_focus(deep_search_list_view(dt->list));
        return TRUE;
    }
    return FALSE;
}

/* The page */

static void tab_free(gpointer data) {
    DeepSearchTab* dt = data;
    deep_search_preview_free(dt->preview);
    if (dt->timer != 0) g_source_remove(dt->timer);
    if (dt->cancel != NULL) {
        g_cancellable_cancel(dt->cancel);
        g_object_unref(dt->cancel);
    }
    g_clear_object(&dt->label);
    g_free(dt->status_text);
    g_free(dt);
}

/* Take the page out of the notebook holding it: the preview notebook, or
 * a notebook of the editor panel (left empty, it goes) */
static void remove_page(AppData* app, GtkWidget* page) {
    GtkNotebook* nb = deep_search_page_notebook(page);
    if (nb == NULL) return;
    if (GTK_WIDGET(nb) == app->notebook) {
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    } else {
        editor_remove_page(app, page);
    }
}

static void close_page(AppData* app) {
    if (app->deep_search_page == NULL || app->notebook == NULL) return;
    GtkWidget* closed = app->deep_search_page;
    DeepSearchTab* dt = tab_state(app);
    if (dt != NULL) {
        stop_search(dt);
        deep_search_preview_cancel(dt->preview);
    }
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    app->deep_search_page = NULL;
    remove_page(app, closed);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees the state, once a search still running is done */
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* The focus in the results (the name field without any), once the panels
 * are placed. data: a ref on the page */
static gboolean focus_after_move(gpointer data) {
    GtkWidget* page = data;
    DeepSearchTab* dt = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (dt != NULL && gtk_widget_get_mapped(page)) {
        gtk_widget_grab_focus(deep_search_list_count(dt->list) > 0 ? deep_search_list_view(dt->list) : dt->name);
    }
    g_object_unref(page);
    return G_SOURCE_REMOVE;
}

/* Out of the preview notebook into the editor panel, put left of the
 * preview: the results on the left, the result selected previewed on the
 * right */
static void on_beside_clicked(GtkButton* button, gpointer data) {
    (void)button;
    DeepSearchTab* dt = data;
    AppData* app = dt->app;
    GtkNotebook* nb = deep_search_page_notebook(dt->page);
    if (nb == NULL || GTK_WIDGET(nb) != app->notebook) return;
    EditorMoveDirection away = editor_swap_preview_side(app) == EDITOR_MOVE_LEFT ? EDITOR_MOVE_RIGHT : EDITOR_MOVE_LEFT;
    if (!editor_move_page_direction(app, dt->page, away)) return;
    if (!editor_swap_active(app)) editor_swap_toggle(app);
    g_idle_add(focus_after_move, g_object_ref(dt->page));
}

/* The button only in the preview notebook (beside it, the preview already shows) */
static void on_page_map(GtkWidget* page, gpointer data) {
    DeepSearchTab* dt = data;
    GtkNotebook* nb = deep_search_page_notebook(page);
    gtk_widget_set_visible(dt->beside, nb != NULL && GTK_WIDGET(nb) == dt->app->notebook);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((DeepSearchTab*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((DeepSearchTab*)data)->app);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)child;
    close_page(data);
}

static gboolean bookmark_from_menu_idle(gpointer data) {
    app_emit((AppData*)data, APP_EVENT_SAVE_SEARCH_BOOKMARK);
    return G_SOURCE_REMOVE;
}

static void on_menu_bookmark(GtkWidget* child, gpointer data) {
    (void)child;
    g_idle_add(bookmark_from_menu_idle, data);
}

/* The tab menu, as for an editor tab: save as bookmark, move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    DeepSearchTab* dt = data;
    editor_page_show_context_menu_extra(dt->app, dt->page, dt->label, on_menu_close, dt->app,
                                        _("Save as Bookmark…"), on_menu_bookmark);
}

/* Put the page in the preview notebook: after the result pages and the
 * tool tabs, else at the end; it moves from there like an editor tab */
static void insert_page(AppData* app, DeepSearchTab* dt) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* after_pages[] = {app->refs_page, app->search_page, app->help_page, app->sysmon_page,
                                app->disk_usage_page};
    gint after = -1;
    for (gsize i = 0; i < G_N_ELEMENTS(after_pages); i++) {
        if (after_pages[i] != NULL) after = MAX(after, gtk_notebook_page_num(nb, after_pages[i]));
    }
    gtk_notebook_insert_page(nb, dt->page, dt->label, after >= 0 ? after + 1 : -1);
    gtk_notebook_set_tab_reorderable(nb, dt->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, dt->page, TRUE);
    app->deep_search_page = dt->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* The page of the workspace left, an owned ref (or NULL): taken out of the
 * preview notebook with its tab; in the editor panel or a terminal pane, it
 * already left with them (editor_detach_tabs()) */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->deep_search_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = deep_search_page_notebook(page);
    app->deep_search_page = NULL;
    if (nb != NULL && GTK_WIDGET(nb) == app->notebook) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    return page;
}

static GtkWidget* small_label(const gchar* css) {
    GtkWidget* l = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(l, css);
    return l;
}

static GtkWidget* field_new(DeepSearchTab* dt, const gchar* placeholder, const gchar* tooltip, gint chars) {
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_widget_set_tooltip_text(e, tooltip);
    /* A small minimum: the form fits a narrow preview panel */
    gtk_editable_set_width_chars(GTK_EDITABLE(e), chars > 0 ? chars : 6);
    if (chars <= 0) gtk_widget_set_hexpand(e, TRUE);
    g_signal_connect(e, "activate", G_CALLBACK(on_entry_activate), dt);
    return e;
}

/* A label of the form, its fields on its right (a box) */
static GtkWidget* form_row(GtkGrid* grid, gint row, gint col, const gchar* label, gint width) {
    GtkWidget* l = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l), 1.0f);
    gtk_widget_add_css_class(l, "ds-field");
    gtk_grid_attach(grid, l, col, row, 1, 1);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_grid_attach(grid, box, col + 1, row, width, 1);
    return box;
}

static GtkWidget* option_new(DeepSearchTab* dt, gint opt, const gchar* label, const gchar* tooltip) {
    GtkWidget* b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_focus_on_click(b, FALSE);
    g_signal_connect(b, "toggled", G_CALLBACK(on_option_toggled), dt);
    dt->options[opt] = b;
    return b;
}

static GtkWidget* between_label(const gchar* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "ds-field");
    return l;
}

#define GLOBS_TOOLTIP                                                                                         \
    _("Patterns separated by commas: node_modules (a name at any depth), src/build (from the folder searched), " \
      "build/ (folders only); the folders left out are not entered")

static void build_form(DeepSearchTab* dt, GtkWidget* page) {
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "ds-form");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    GtkGrid* g = GTK_GRID(grid);

    GtkWidget* box = form_row(g, 0, 0, _("Folder"), 3);
    dt->folder = field_new(dt, _("Folder to search"), _("The folder searched, with everything below it"), 0);
    gtk_box_append(GTK_BOX(box), dt->folder);
    GtkWidget* parent = icon_button_new("pan-up", _("Parent Folder"));
    g_signal_connect(parent, "clicked", G_CALLBACK(on_parent_clicked), dt);
    gtk_box_append(GTK_BOX(box), parent);

    box = form_row(g, 1, 0, _("Name"), 1);
    dt->name = field_new(dt, _("Part of the name, or *.jpg"),
                         _("A part of the name, any case; with * or ? a pattern of the whole name (IMG_????.jpg)"), 0);
    gtk_box_append(GTK_BOX(box), dt->name);
    box = form_row(g, 1, 2, _("Extensions"), 1);
    dt->extensions = field_new(dt, _("c, h, md"), _("Extensions separated by commas, any case"), 0);
    GtkGesture* press = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(press), GTK_PHASE_CAPTURE);
    g_signal_connect(press, "pressed", G_CALLBACK(on_extensions_pressed), dt);
    gtk_widget_add_controller(dt->extensions, GTK_EVENT_CONTROLLER(press));
    g_signal_connect(dt->extensions, "changed", G_CALLBACK(on_extensions_changed), dt);
    gtk_box_append(GTK_BOX(box), dt->extensions);
    /* TRANSLATORS: between the extensions and the category of file */
    gtk_box_append(GTK_BOX(box), between_label(_("or")));
    const gchar* families[FILE_FAMILY_OTHER + 2];
    families[0] = _("Any category");
    for (gint f = 0; f < FILE_FAMILY_OTHER; f++) families[f + 1] = file_family_name((FileFamily)f);
    families[FILE_FAMILY_OTHER + 1] = NULL;
    dt->family = gtk_drop_down_new_from_strings(families);
    gtk_widget_set_tooltip_text(dt->family, _("A category of files, from their extension, instead of extensions"));
    g_signal_connect(dt->family, "notify::selected", G_CALLBACK(on_family_changed), dt);
    gtk_box_append(GTK_BOX(box), dt->family);

    box = form_row(g, 2, 0, _("Contains"), 1);
    dt->content = field_new(dt, _("Text in the file"), _("A text the file holds (files only; binary files are skipped)"),
                            0);
    gtk_box_append(GTK_BOX(box), dt->content);
    GtkWidget* toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(toggles, "linked");
    gtk_box_append(GTK_BOX(toggles), option_new(dt, OPT_CASE, "Aa", _("Match Case (Alt+M)")));
    gtk_box_append(GTK_BOX(toggles), option_new(dt, OPT_WORD, "ab", _("Whole Word (Alt+W)")));
    gtk_box_append(GTK_BOX(toggles), option_new(dt, OPT_REGEX, ".*", _("Regular Expression, PCRE Syntax (Alt+R)")));
    gtk_box_append(GTK_BOX(toggles),
                   option_new(dt, OPT_FUZZY, "a…z", _("Letters in Order, Not Next to Each Other: edvim Finds editor_vim (Alt+L)")));
    gtk_box_append(GTK_BOX(box), toggles);
    box = form_row(g, 2, 2, _("Type"), 1);
    const gchar* kinds[] = {_("Files and folders"), _("Files"), _("Folders"), NULL};
    dt->kind = gtk_drop_down_new_from_strings(kinds);
    gtk_widget_set_tooltip_text(dt->kind, _("The kind of entries found (links count as files)"));
    gtk_box_append(GTK_BOX(box), dt->kind);

    const gchar* size_tip = _("In bytes, or with k, M, G (powers of 1024); both ends included; files only");
    box = form_row(g, 3, 0, _("Size"), 1);
    dt->min_size = field_new(dt, _("any"), size_tip, 8);
    gtk_box_append(GTK_BOX(box), dt->min_size);
    gtk_box_append(GTK_BOX(box), between_label(_("to")));
    dt->max_size = field_new(dt, _("any"), size_tip, 8);
    gtk_box_append(GTK_BOX(box), dt->max_size);
    const gchar* date_tip = _("A day (2026-11-01, 2026-11-01 14:30, today, yesterday) or a time ago (30min, 3h, 7d, 2w, "
                              "6m, 1y); a day given as the second date is included");
    box = form_row(g, 3, 2, _("Last change"), 1);
    dt->after = field_new(dt, _("any"), date_tip, 10);
    gtk_box_append(GTK_BOX(box), dt->after);
    gtk_box_append(GTK_BOX(box), deep_search_calendar_button(dt->after));
    gtk_box_append(GTK_BOX(box), between_label(_("to")));
    dt->before = field_new(dt, _("any"), date_tip, 10);
    gtk_box_append(GTK_BOX(box), dt->before);
    gtk_box_append(GTK_BOX(box), deep_search_calendar_button(dt->before));

    box = form_row(g, 4, 0, _("Exclude"), 1);
    dt->exclude = field_new(dt, _("node_modules, .git, build/"), GLOBS_TOOLTIP, 0);
    gtk_box_append(GTK_BOX(box), dt->exclude);
    box = form_row(g, 4, 2, _("Depth"), 1);
    dt->depth = field_new(dt, _("any"), _("Levels below the folder: 1 for its own entries only"), 5);
    gtk_box_append(GTK_BOX(box), dt->depth);
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_grid_attach(g, box, 1, 5, 3, 1);
    dt->hidden = gtk_check_button_new_with_label(_("Hidden entries"));
    gtk_widget_set_tooltip_text(dt->hidden, _("Also the names starting with a dot"));
    gtk_box_append(GTK_BOX(box), dt->hidden);
    dt->one_fs = gtk_check_button_new_with_label(_("This filesystem only"));
    gtk_widget_set_tooltip_text(dt->one_fs, _("Do not enter the other filesystems mounted below the folder"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dt->one_fs), TRUE);
    gtk_box_append(GTK_BOX(box), dt->one_fs);
    /* Scrolled sideways in a panel narrower than the form */
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(page), scroll);

    dt->error = small_label("ds-error");
    gtk_label_set_wrap(GTK_LABEL(dt->error), TRUE);
    gtk_widget_set_visible(dt->error, FALSE);
    gtk_box_append(GTK_BOX(page), dt->error);
}

static DeepSearchTab* ensure_page(AppData* app) {
    DeepSearchTab* dt = tab_state(app);
    if (dt != NULL || app->notebook == NULL) return dt;
    ensure_css();
    dt = g_new0(DeepSearchTab, 1);
    dt->app = app;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "deep-search");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    dt->page = page;

    /* Header: title, buttons */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "ds-header");
    GtkWidget* title = icon_label_new("edit-find", _("Deep Search"));
    /* Cut in a narrow panel rather than widen it */
    GtkWidget* title_text = gtk_widget_get_last_child(title);
    if (GTK_IS_LABEL(title_text)) gtk_label_set_ellipsize(GTK_LABEL(title_text), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bar), title);
    GtkWidget* reset = icon_button_new("edit-clear", _("Reset the Criteria and the Results"));
    icon_button_set_content(reset, "edit-clear", _("Reset"));
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_clicked), dt);
    gtk_box_append(GTK_BOX(bar), reset);
    dt->search = icon_button_new("edit-find", _("Search (Enter / F5)"));
    icon_button_set_content(dt->search, "edit-find", C_("action", "Search"));
    g_signal_connect(dt->search, "clicked", G_CALLBACK(on_search_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->search);
    dt->stop = icon_button_new("process-stop", _("Stop the Search (Escape)"));
    icon_button_set_content(dt->stop, "process-stop", _("Stop"));
    g_signal_connect(dt->stop, "clicked", G_CALLBACK(on_stop_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->stop);
    GtkWidget* bookmark = icon_button_new("bookmark-new", _("Save Search as Bookmark…"));
    g_signal_connect(bookmark, "clicked", G_CALLBACK(on_bookmark_clicked), dt);
    gtk_box_append(GTK_BOX(bar), bookmark);
    dt->beside = icon_button_new("view-dual", _("Show the Preview on the Right"));
    g_signal_connect(dt->beside, "clicked", G_CALLBACK(on_beside_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->beside);
    GtkWidget* close = icon_button_new("window-close", _("Close Deep Search"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), dt);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    build_form(dt, page);

    dt->status = small_label("ds-status");
    gtk_label_set_ellipsize(GTK_LABEL(dt->status), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(dt->status, "ds-num");
    set_status(dt, g_strdup(READY_TEXT));
    gtk_box_append(GTK_BOX(page), dt->status);
    dt->current = small_label("ds-current");
    gtk_box_append(GTK_BOX(page), dt->current);

    DeepSearchListHandlers handlers = {on_reveal, on_open, on_preview, dt};
    dt->list = deep_search_list_new(app, &handlers);
    dt->preview = deep_search_preview_new(app, page, dt->list);
    dt->charts = deep_search_charts_new(on_charts_changed, dt);
    /* The results over their charts, which keep their height (dragged to
     * nothing when not wanted) */
    deep_search_charts_on_layout(dt->charts, on_charts_layout, dt);
    GtkWidget* split = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    dt->split = split;
    paned_use_thin_handle(split);
    gtk_widget_set_vexpand(split, TRUE);
    gtk_paned_set_start_child(GTK_PANED(split), dt->list);
    gtk_paned_set_resize_start_child(GTK_PANED(split), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(split), FALSE);
    gtk_paned_set_end_child(GTK_PANED(split), dt->charts);
    gtk_paned_set_resize_end_child(GTK_PANED(split), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(split), TRUE);
    g_signal_connect(split, "notify::max-position", G_CALLBACK(on_split_sized), dt);
    gtk_box_append(GTK_BOX(page), split);

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_page_key), dt);
    gtk_widget_add_controller(page, keys);
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, dt, tab_free);
    g_signal_connect(page, "map", G_CALLBACK(on_page_map), dt);
    editor_page_set_movable(page);

    dt->label = g_object_ref(icon_label_new("edit-find", _("Deep Search")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), dt);
    gtk_widget_add_controller(dt->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), dt);
    gtk_widget_add_controller(dt->label, GTK_EVENT_CONTROLLER(rclick));

    sync_buttons(dt);
    insert_page(app, dt);
    return dt;
}

void deep_search_tab_show(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL) return;
    DeepSearchTab* dt = ensure_page(app);
    if (dt == NULL) return;
    gtk_editable_set_text(GTK_EDITABLE(dt->folder), path);
    GtkNotebook* nb = deep_search_page_notebook(dt->page);
    if (nb == NULL) return;
    /* The preview panel holding the tab shows when hidden */
    if (GTK_WIDGET(nb) == app->notebook) deep_search_show_preview_panel(app);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, dt->page));
    gtk_widget_grab_focus(dt->name);
}

gboolean deep_search_tab_read_saved(AppData* app, SavedDeepSearch* out) {
    DeepSearchTab* dt = tab_state(app);
    if (dt == NULL || out == NULL) return FALSE;
    clear_errors(dt);
    DeepQuery q;
    gboolean ok = read_form(dt, &q);
    deep_query_clear(&q);
    if (!ok) return FALSE;
    saved_deep_search_init(out);
    gchar* folder = dup_nonempty(field_text(dt->folder), TRUE);
    if (folder != NULL && folder[0] == '~' && (folder[1] == '\0' || folder[1] == '/')) {
        gchar* home = g_build_filename(g_get_home_dir(), folder + 1, NULL);
        g_free(folder);
        folder = home;
    }
    out->folder = folder;
    out->use_current_folder = (folder == NULL);
    out->name = dup_nonempty(field_text(dt->name), FALSE);
    out->family = (gint)gtk_drop_down_get_selected(GTK_DROP_DOWN(dt->family)) - 1;
    if (out->family < 0) out->extensions = dup_nonempty(field_text(dt->extensions), TRUE);
    out->content = dup_nonempty(field_text(dt->content), FALSE);
    out->content_opts.match_case = option_on(dt, OPT_CASE);
    out->content_opts.whole_word = option_on(dt, OPT_WORD);
    out->content_opts.regex = option_on(dt, OPT_REGEX);
    out->content_opts.fuzzy = !out->content_opts.regex && option_on(dt, OPT_FUZZY);
    out->kind = (DeepKind)gtk_drop_down_get_selected(GTK_DROP_DOWN(dt->kind));
    out->min_size = dup_nonempty(field_text(dt->min_size), TRUE);
    out->max_size = dup_nonempty(field_text(dt->max_size), TRUE);
    out->after = dup_nonempty(field_text(dt->after), TRUE);
    out->before = dup_nonempty(field_text(dt->before), TRUE);
    out->exclude = dup_nonempty(field_text(dt->exclude), TRUE);
    out->depth = dup_nonempty(field_text(dt->depth), TRUE);
    out->hidden = gtk_check_button_get_active(GTK_CHECK_BUTTON(dt->hidden));
    out->one_filesystem = gtk_check_button_get_active(GTK_CHECK_BUTTON(dt->one_fs));
    return TRUE;
}

void deep_search_tab_run_saved(AppData* app, const SavedDeepSearch* saved) {
    if (app == NULL || saved == NULL) return;
    DeepSearchTab* dt = ensure_page(app);
    if (dt == NULL) return;
    clear_results(dt);
    const gchar* folder = (!saved->use_current_folder && saved->folder != NULL && saved->folder[0] != '\0')
                              ? saved->folder
                              : (app->base_dir != NULL ? app->base_dir : "");
    set_entry(dt->folder, folder);
    apply_form(dt, saved);
    GtkNotebook* nb = deep_search_page_notebook(dt->page);
    if (nb != NULL) {
        if (GTK_WIDGET(nb) == app->notebook) deep_search_show_preview_panel(app);
        gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, dt->page));
    }
    start_search(dt);
}

gboolean deep_search_tab_close_shown(AppData* app) {
    if (app == NULL || app->deep_search_page == NULL || app->notebook == NULL) return FALSE;
    GtkWidget* page = app->deep_search_page;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    gboolean focused = focus != NULL && (focus == page || gtk_widget_is_ancestor(focus, page));
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (!focused && gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, page)) return FALSE;
    if (!gtk_widget_get_mapped(page)) return FALSE;
    close_page(app);
    return TRUE;
}

void deep_search_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->deep_search_page != NULL) g_object_unref(ws->deep_search_page);
    ws->deep_search_page = page;
}

void deep_search_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->deep_search_page == NULL) return;
    GtkWidget* page = ws->deep_search_page;
    ws->deep_search_page = NULL;
    DeepSearchTab* dt = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (dt != NULL && app->notebook != NULL && app->deep_search_page == NULL) {
        if (deep_search_page_notebook(page) != NULL) {
            /* Back with the editor panel or the terminal panes */
            app->deep_search_page = page;
            app_emit(app, APP_EVENT_CHROME_CHANGED);
        } else {
            insert_page(app, dt);
        }
    }
    g_object_unref(page);
}

void deep_search_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            DeepSearchTab* dt = g_object_get_data(G_OBJECT(page), PAGE_KEY);
            if (dt != NULL) stop_search(dt);
            remove_page(app, page); /* still in the editor panel shown */
            g_object_unref(page);
        }
    }
    if (ws->deep_search_page != NULL) {
        DeepSearchTab* dt = g_object_get_data(G_OBJECT(ws->deep_search_page), PAGE_KEY);
        if (dt != NULL) stop_search(dt);
        g_clear_object(&ws->deep_search_page);
    }
}
