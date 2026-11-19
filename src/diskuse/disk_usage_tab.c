/* The Disk Usage tab: the room a folder takes (see disk_usage_tab.h) */
#include "diskuse/disk_usage_tab.h"
#include "diskuse/disk_usage_list.h"
#include "diskuse/disk_usage_map.h"
#include "diskuse/disk_usage_stats.h"
#include "core/app_events.h"
#include "core/disk_scan.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "explorer/file_list.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"

#define PAGE_KEY "lookin-disk-usage-tab"
#define TICK_MS 200 /* progress shown again while the tree is read */

/* One reading of the tree: the scan lives as long as its worker */
typedef struct {
    DiskScan* scan; /* owned until the tab adopts it */
} ScanJob;

typedef struct {
    AppData* app;
    GtkWidget* page;     /* the notebook page (app->disk_usage_page) */
    GtkWidget* label;    /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* title;    /* header: the folder analysed */
    GtkWidget* up;       /* header buttons */
    GtkWidget* refresh;
    GtkWidget* stop;
    GtkWidget* include;  /* the excluded entries back in */
    GtkWidget* progress; /* GtkProgressBar, while the tree is read */
    GtkWidget* status;   /* counters, then the summary */
    GtkWidget* current;  /* folder being read, then the folder shown */
    GtkWidget* list;     /* disk_usage_list */
    GtkWidget* stats;    /* disk_usage_stats */
    GtkWidget* map;      /* disk_usage_map, under the list */
    GtkWidget* split;    /* GtkPaned of the list and the map */
    gboolean split_set;  /* its first position given */

    gchar* root;         /* folder analysed */
    DiskScan* scan;      /* the tree read (owned), NULL while it is read */
    DiskScan* running;   /* the scan being read (owned by its job), else NULL */
    gboolean quiet;      /* the reading running keeps the tree read shown */
    GPtrArray* excluded; /* full paths left out of the calculation */
    GCancellable* cancel;
    DiskNode* shown;     /* folder of the breakdown, in scan's tree */
    guint timer;
    gint64 started;      /* monotonic time the reading started */
    gint64 elapsed;      /* how long it took (microseconds) */
} DiskUsageTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".disk-usage { background-color: @bg_view; }"
                  ".disk-usage .du-header { padding: 4px 10px; color: @fg; }"
                  ".disk-usage .du-title { color: @fg_strong; }"
                  ".disk-usage .du-status { color: @fg_text; padding: 2px 12px 0 12px; }"
                  ".disk-usage .du-current { color: @fg_subtle; font-size: @size_small; padding: 0 12px 6px 12px; }"
                  ".disk-usage .du-muted { color: @fg_subtle; }"
                  ".disk-usage .du-num { font-feature-settings: \"tnum\"; }"
                  ".disk-usage progressbar { padding: 4px 12px; }"
                  ".disk-usage progressbar trough { background-color: @bg_input; border: none; margin: 0; min-height: 6px; }"
                  ".disk-usage progressbar progress { background-color: @accent; border: none; margin: 0;"
                  "  min-width: 0; min-height: 6px; }"
                  ".disk-usage progressbar text { color: @fg_subtle; font-size: @size_small; }"
                  ".disk-usage levelbar trough { background-color: @bg_input; border: none; border-radius: 3px;"
                  "  min-height: 6px; padding: 0; }"
                  ".disk-usage levelbar block { min-height: 6px; border: none; border-radius: 3px; }"
                  ".disk-usage levelbar block.filled { background-color: @accent; }"
                  ".disk-usage levelbar block.empty { background-color: transparent; }"
                  ".disk-usage .du-stats { padding: 0 12px 12px 12px; }"
                  ".disk-usage .du-section { color: @fg_strong; font-weight: bold; padding-top: 10px; }"
                  ".disk-usage .du-grid { color: @fg_text; }"
                  ".disk-usage .du-file { padding: 0 2px; min-height: 0; }"
                  ".disk-usage .du-map { font-size: @size_small; }"
                  ".disk-usage .du-histogram { font-size: @size_small; margin-top: 4px; }");
}

static DiskUsageTab* tab_state(AppData* app) {
    return app != NULL && app->disk_usage_page != NULL
               ? g_object_get_data(G_OBJECT(app->disk_usage_page), PAGE_KEY)
               : NULL;
}

static gchar* format_count(guint64 n) {
    return g_strdup_printf("%" G_GUINT64_FORMAT, n);
}

/* "1.2 s", "2 min 05 s" */
static gchar* format_duration(gint64 us) {
    gdouble s = (gdouble)us / G_USEC_PER_SEC;
    if (s < 60.0) return g_strdup_printf(_("%.1f s"), s);
    guint total = (guint)s;
    return g_strdup_printf(_("%u min %02u s"), total / 60, total % 60);
}

/* "12840 files · 1022 folders · 3.4 GB" */
static gchar* counters_text(guint64 files, guint64 dirs, guint64 size) {
    gchar* f = disk_format_files(files);
    gchar* n = format_count(dirs);
    /* TRANSLATORS: %s is a number of folders */
    gchar* d = g_strdup_printf(ngettext("%s folder", "%s folders", (gulong)MIN(dirs, G_MAXULONG)), n);
    gchar* s = g_format_size(size);
    gchar* text = g_strdup_printf("%s · %s · %s", f, d, s);
    g_free(f);
    g_free(n);
    g_free(d);
    g_free(s);
    return text;
}

static void set_label(GtkWidget* label, gchar* text) {
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(label)), text) != 0) gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

/* The header buttons for what can be done now */
static void sync_buttons(DiskUsageTab* dt) {
    gtk_widget_set_sensitive(dt->up, dt->shown != NULL && dt->shown->parent != NULL);
    gtk_widget_set_visible(dt->stop, dt->running != NULL);
    gtk_widget_set_visible(dt->refresh, dt->running == NULL);
    guint n = dt->excluded->len;
    gtk_widget_set_visible(dt->include, n > 0);
    if (n > 0) {
        gchar* count = format_count(n);
        /* TRANSLATORS: %s is a number of files and folders excluded from the calculation */
        gchar* text = g_strdup_printf(ngettext("%s excluded", "%s excluded", n), count);
        icon_button_set_content(dt->include, "edit-undo", text);
        GString* tip = g_string_new(_("Include the Excluded Entries Again"));
        for (guint i = 0; i < n; i++) {
            g_string_append_c(tip, '\n');
            g_string_append(tip, g_ptr_array_index(dt->excluded, i));
        }
        gtk_widget_set_tooltip_text(dt->include, tip->str);
        g_string_free(tip, TRUE);
        g_free(text);
        g_free(count);
    }
}

/* While the tree is read: the bar, the counters, the folders done so far */
static void show_progress(DiskUsageTab* dt) {
    DiskScanSnapshot snap;
    disk_scan_snapshot(dt->running, &snap);
    GtkProgressBar* bar = GTK_PROGRESS_BAR(dt->progress);
    if (snap.fs_used > 0) {
        /* The root of a filesystem: its used space is where the reading ends */
        gtk_progress_bar_set_fraction(bar, CLAMP((gdouble)snap.disk / (gdouble)snap.fs_used, 0.0, 1.0));
        gchar* done = g_format_size(snap.disk);
        gchar* used = g_format_size(snap.fs_used);
        /* TRANSLATORS: bytes read so far, of the bytes used on the filesystem */
        gchar* text = g_strdup_printf(_("%1$s of %2$s used on this filesystem"), done, used);
        gtk_progress_bar_set_text(bar, text);
        g_free(text);
        g_free(done);
        g_free(used);
    } else if (snap.top_total > 0) {
        gtk_progress_bar_set_fraction(bar, (gdouble)snap.top_done / (gdouble)snap.top_total);
        /* TRANSLATORS: folders of the analysed folder read in full, of all of them */
        gchar* text = g_strdup_printf(_("%1$u of %2$u folders"), snap.top_done, snap.top_total);
        gtk_progress_bar_set_text(bar, text);
        g_free(text);
    } else {
        gtk_progress_bar_pulse(bar);
        gtk_progress_bar_set_text(bar, NULL);
    }
    if (dt->quiet) { /* the tree read stays shown */
        disk_scan_snapshot_clear(&snap);
        return;
    }
    gchar* counters = counters_text(snap.files, snap.dirs, snap.size);
    gchar* time = format_duration(g_get_monotonic_time() - dt->started);
    set_label(dt->status, g_strdup_printf("%s · %s", counters, time));
    g_free(counters);
    g_free(time);
    set_label(dt->current, g_strdup(snap.current != NULL ? snap.current : ""));
    disk_usage_list_show_progress(dt->list, dt->root, &snap);
    disk_scan_snapshot_clear(&snap);
}

static gboolean on_tick(gpointer data) {
    DiskUsageTab* dt = data;
    if (dt->running == NULL) {
        dt->timer = 0;
        return G_SOURCE_REMOVE;
    }
    show_progress(dt);
    return G_SOURCE_CONTINUE;
}

/* The summary of the tree read, under the header */
static void show_summary(DiskUsageTab* dt) {
    const DiskStats* st = disk_scan_stats(dt->scan);
    GString* text = g_string_new(NULL);
    gchar* counters = counters_text(st->n_files, st->n_dirs, st->size);
    gchar* disk = g_format_size(st->disk);
    gchar* time = format_duration(dt->elapsed);
    /* TRANSLATORS: counts and size, the room taken on disk, the time of the reading */
    g_string_append_printf(text, _("%1$s (%2$s on disk) · read in %3$s"), counters, disk, time);
    g_free(counters);
    g_free(disk);
    g_free(time);
    if (st->n_errors > 0) {
        gchar* n = format_count(st->n_errors);
        g_string_append(text, " · ");
        /* TRANSLATORS: %s is a number of entries that could not be read */
        g_string_append_printf(text, ngettext("%s unreadable", "%s unreadable", (gulong)MIN(st->n_errors, G_MAXULONG)), n);
        g_free(n);
    }
    if (st->n_excluded > 0) {
        gchar* n = format_count(st->n_excluded);
        g_string_append(text, " · ");
        /* TRANSLATORS: %s is a number of entries excluded from the calculation */
        g_string_append_printf(text, ngettext("%s excluded", "%s excluded", (gulong)MIN(st->n_excluded, G_MAXULONG)), n);
        g_free(n);
    }
    if (disk_scan_cancelled(dt->scan)) {
        g_string_append(text, " · ");
        g_string_append(text, _("stopped before the end"));
    }
    set_label(dt->status, g_string_free(text, FALSE));
}

/* Show the folder node of the tree read, the row named select selected */
static void show_folder(DiskUsageTab* dt, DiskNode* node, const gchar* select) {
    dt->shown = node;
    disk_usage_list_show_node(dt->list, node, select);
    disk_usage_map_show(dt->map, node);
    gchar* selected = disk_usage_list_selected_name(dt->list);
    disk_usage_map_select(dt->map, selected);
    g_free(selected);
    set_label(dt->current, disk_node_path(node));
    sync_buttons(dt);
}

static void job_free(gpointer data) {
    ScanJob* job = data;
    disk_scan_free(job->scan);
    g_free(job);
}

static void scan_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    ScanJob* job = data;
    disk_scan_run(job->scan, cancel);
    g_task_return_boolean(task, TRUE);
}

static void on_scan_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)data;
    DiskUsageTab* dt = g_object_get_data(source, PAGE_KEY);
    ScanJob* job = g_task_get_task_data(G_TASK(res));
    g_task_propagate_boolean(G_TASK(res), NULL);
    if (dt == NULL || job->scan == NULL || job->scan != dt->running) return; /* replaced or closed */
    dt->running = NULL;
    if (dt->timer != 0) {
        g_source_remove(dt->timer);
        dt->timer = 0;
    }
    gtk_widget_set_visible(dt->progress, FALSE);
    if (dt->quiet && dt->scan != NULL) {
        dt->quiet = FALSE;
        if (disk_scan_cancelled(job->scan)) { /* stopped: the tree read stays */
            sync_buttons(dt);
            return;
        }
        /* The new tree in place of the old one, on the same folder */
        DiskScan* old = dt->scan;
        gchar* path = disk_node_path(dt->shown);
        gchar* select = disk_usage_list_selected_name(dt->list);
        dt->scan = job->scan;
        job->scan = NULL;
        dt->elapsed = g_get_monotonic_time() - dt->started;
        DiskNode* node = disk_scan_find(dt->scan, path);
        show_summary(dt);
        disk_usage_stats_show(dt->stats, dt->scan);
        show_folder(dt, node != NULL ? node : disk_scan_root(dt->scan), select);
        disk_scan_free(old); /* nothing shown points into it any more */
        g_free(select);
        g_free(path);
        return;
    }
    dt->quiet = FALSE;
    dt->scan = job->scan;
    job->scan = NULL;
    dt->elapsed = g_get_monotonic_time() - dt->started;
    show_summary(dt);
    disk_usage_stats_show(dt->stats, dt->scan);
    gchar* select = disk_usage_list_selected_name(dt->list);
    show_folder(dt, disk_scan_root(dt->scan), select);
    g_free(select);
}

/* Stop the reading, if one runs (what was read so far is kept) */
static void stop_scan(DiskUsageTab* dt) {
    if (dt->running != NULL) g_cancellable_cancel(dt->cancel);
}

/* Read the tree of dt->root again, from the start, without the excluded
 * entries; quiet: the tree read stays shown until the new one is read (the
 * progress bar only) */
static void start_scan(DiskUsageTab* dt, gboolean quiet) {
    if (dt->cancel != NULL) {
        g_cancellable_cancel(dt->cancel);
        g_object_unref(dt->cancel);
    }
    dt->cancel = g_cancellable_new();
    dt->quiet = quiet && dt->scan != NULL;
    if (!dt->quiet) {
        disk_usage_list_clear(dt->list); /* its rows point into the tree freed below */
        disk_usage_stats_show(dt->stats, NULL);
        disk_usage_map_show(dt->map, NULL);
        dt->shown = NULL;
        g_clear_pointer(&dt->scan, disk_scan_free);
    }

    ScanJob* job = g_new0(ScanJob, 1);
    job->scan = disk_scan_new(dt->root, TRUE);
    for (guint i = 0; i < dt->excluded->len; i++) disk_scan_exclude(job->scan, g_ptr_array_index(dt->excluded, i));
    dt->running = job->scan;
    dt->started = g_get_monotonic_time();
    GTask* task = g_task_new(dt->page, dt->cancel, on_scan_done, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, scan_thread);
    g_object_unref(task);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dt->progress), 0.0);
    gtk_widget_set_visible(dt->progress, TRUE);
    if (!dt->quiet) {
        set_label(dt->title, g_strdup(dt->root));
        gtk_widget_set_tooltip_text(dt->title, dt->root);
        set_label(dt->status, g_strdup(_("Reading…")));
        set_label(dt->current, g_strdup(dt->root));
    }
    if (dt->timer == 0) dt->timer = g_timeout_add(TICK_MS, on_tick, dt);
    sync_buttons(dt);
}

/* The breakdown's rows */

static void on_row_activate(gpointer data, DiskNode* folder, const gchar* path) {
    DiskUsageTab* dt = data;
    if (folder != NULL) {
        show_folder(dt, folder, NULL);
    } else if (path != NULL && g_file_test(path, G_FILE_TEST_IS_DIR)) {
        app_notice(dt->app, _("The folders open once the whole tree is read"), FALSE);
    } else if (path != NULL) {
        reveal_path_in_tree(dt->app, path);
        if (dt->app->tree_view != NULL) gtk_widget_grab_focus(dt->app->tree_view);
    }
}

/* Exclude path from the calculation: out of the tree read at once (its
 * sizes), then the tree read again without it (its statistics); while
 * the first reading runs, it starts again without it */
static void on_row_exclude(gpointer data, const gchar* path) {
    DiskUsageTab* dt = data;
    if (path == NULL) return;
    for (guint i = 0; i < dt->excluded->len; i++) {
        if (g_strcmp0(g_ptr_array_index(dt->excluded, i), path) == 0) return;
    }
    g_ptr_array_add(dt->excluded, g_strdup(path));
    if (dt->scan == NULL) {
        start_scan(dt, FALSE);
        return;
    }
    if (dt->shown != NULL && disk_scan_remove(dt->scan, path)) {
        show_summary(dt);
        disk_usage_stats_show(dt->stats, dt->scan);
        show_folder(dt, dt->shown, NULL);
    }
    start_scan(dt, TRUE);
}

static void on_row_selected(gpointer data, const gchar* name) {
    DiskUsageTab* dt = data;
    disk_usage_map_select(dt->map, dt->shown != NULL ? name : NULL);
}

/* The map: a click selects the row, a double click opens */
static void on_map_selected(gpointer data, const gchar* name) {
    disk_usage_list_select(((DiskUsageTab*)data)->list, name);
}

static void on_map_open(gpointer data, DiskNode* folder) {
    show_folder(data, folder, NULL);
}

static void go_up(DiskUsageTab* dt) {
    if (dt->shown == NULL || dt->shown->parent == NULL) return;
    show_folder(dt, dt->shown->parent, dt->shown->name);
}

static void on_row_up(gpointer data) {
    go_up(data);
}

static void on_row_reveal(gpointer data, const gchar* path) {
    DiskUsageTab* dt = data;
    reveal_path_in_tree(dt->app, path);
    if (dt->app->tree_view != NULL) gtk_widget_grab_focus(dt->app->tree_view);
}

/* Header buttons and keys */

static void on_up_clicked(GtkButton* button, gpointer data) {
    (void)button;
    go_up(data);
}

static void on_refresh_clicked(GtkButton* button, gpointer data) {
    (void)button;
    start_scan(data, FALSE);
}

/* The excluded entries back in: the tree read again with them */
static void on_include_clicked(GtkButton* button, gpointer data) {
    (void)button;
    DiskUsageTab* dt = data;
    g_ptr_array_set_size(dt->excluded, 0);
    start_scan(dt, TRUE);
}

static void on_stop_clicked(GtkButton* button, gpointer data) {
    (void)button;
    stop_scan(data);
}

static gboolean on_page_key(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)controller;
    (void)keycode;
    DiskUsageTab* dt = data;
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) != 0) return FALSE;
    if (keyval == GDK_KEY_F5) {
        if (dt->running == NULL) start_scan(dt, FALSE);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape && dt->running != NULL) {
        stop_scan(dt);
        return TRUE;
    }
    return FALSE;
}

/* The first time the list and the map have a height: half each */
static void on_split_sized(GObject* split, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    DiskUsageTab* dt = data;
    gint max = 0;
    g_object_get(split, "max-position", &max, NULL);
    if (dt->split_set || max < 100) return;
    dt->split_set = TRUE;
    gtk_paned_set_position(GTK_PANED(split), max / 2);
}

/* The page */

static void tab_free(gpointer data) {
    DiskUsageTab* dt = data;
    if (dt->timer != 0) g_source_remove(dt->timer);
    if (dt->cancel != NULL) {
        g_cancellable_cancel(dt->cancel);
        g_object_unref(dt->cancel);
    }
    disk_scan_free(dt->scan);
    g_ptr_array_unref(dt->excluded);
    g_free(dt->root);
    g_clear_object(&dt->label);
    g_free(dt);
}

static void close_page(AppData* app) {
    if (app->disk_usage_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->disk_usage_page;
    DiskUsageTab* dt = tab_state(app);
    if (dt != NULL) stop_scan(dt);
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint num = gtk_notebook_page_num(nb, closed);
    app->disk_usage_page = NULL;
    if (num >= 0) gtk_notebook_remove_page(nb, num);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees the state, once a reading still running is done */
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((DiskUsageTab*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((DiskUsageTab*)data)->app);
}

/* Put the page in the notebook: after the Git tab and the result pages */
static void insert_page(AppData* app, DiskUsageTab* dt) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    if (app->help_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->help_page));
    if (app->sysmon_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->sysmon_page));
    gtk_notebook_insert_page(nb, dt->page, dt->label, after + 1);
    app->disk_usage_page = dt->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* Take the page out of the notebook with its tab: an owned ref, or NULL */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->disk_usage_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    app->disk_usage_page = NULL;
    if (n >= 0) gtk_notebook_remove_page(nb, n);
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

static DiskUsageTab* ensure_page(AppData* app) {
    DiskUsageTab* dt = tab_state(app);
    if (dt != NULL || app->notebook == NULL) return dt;
    ensure_css();
    dt = g_new0(DiskUsageTab, 1);
    dt->app = app;
    dt->excluded = g_ptr_array_new_with_free_func(g_free);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "disk-usage");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    dt->page = page;

    /* Header: title, folder, buttons */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "du-header");
    gtk_box_append(GTK_BOX(bar), icon_label_new("drive-harddisk", _("Disk Usage")));
    dt->title = small_label("du-title");
    gtk_widget_set_hexpand(dt->title, TRUE);
    gtk_box_append(GTK_BOX(bar), dt->title);
    dt->up = icon_button_new("pan-up", _("Parent Folder (Backspace / Left)"));
    g_signal_connect(dt->up, "clicked", G_CALLBACK(on_up_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->up);
    dt->refresh = icon_button_new("view-refresh", _("Read Again (F5)"));
    g_signal_connect(dt->refresh, "clicked", G_CALLBACK(on_refresh_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->refresh);
    dt->include = icon_button_new("edit-undo", _("Include the Excluded Entries Again"));
    g_signal_connect(dt->include, "clicked", G_CALLBACK(on_include_clicked), dt);
    gtk_widget_set_visible(dt->include, FALSE);
    gtk_box_append(GTK_BOX(bar), dt->include);
    dt->stop = icon_button_new("process-stop", _("Stop Reading (Escape)"));
    icon_button_set_content(dt->stop, "process-stop", _("Stop")); /* not taken for the close button */
    g_signal_connect(dt->stop, "clicked", G_CALLBACK(on_stop_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->stop);
    GtkWidget* close = icon_button_new("window-close", _("Close Disk Usage"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), dt);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    dt->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(dt->progress), TRUE);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(dt->progress), 0.05);
    gtk_box_append(GTK_BOX(page), dt->progress);
    dt->status = small_label("du-status");
    gtk_label_set_ellipsize(GTK_LABEL(dt->status), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(dt->status, "du-num");
    gtk_box_append(GTK_BOX(page), dt->status);
    dt->current = small_label("du-current");
    gtk_box_append(GTK_BOX(page), dt->current);

    DiskUsageListHandlers handlers = { on_row_activate, on_row_up, on_row_reveal, on_row_exclude, on_row_selected, dt };
    dt->list = disk_usage_list_new(app, &handlers);
    DiskUsageMapHandlers map_handlers = { on_map_selected, on_map_open, on_row_reveal, dt };
    dt->map = disk_usage_map_new(&map_handlers);
    /* The list over the map, both resizable */
    dt->split = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    paned_use_thin_handle(dt->split);
    gtk_paned_set_start_child(GTK_PANED(dt->split), dt->list);
    gtk_paned_set_resize_start_child(GTK_PANED(dt->split), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(dt->split), FALSE);
    gtk_paned_set_end_child(GTK_PANED(dt->split), dt->map);
    gtk_paned_set_resize_end_child(GTK_PANED(dt->split), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(dt->split), FALSE);
    g_signal_connect(dt->split, "notify::max-position", G_CALLBACK(on_split_sized), dt);
    dt->stats = disk_usage_stats_new(on_row_reveal, dt);
    gtk_widget_set_size_request(dt->stats, 380, -1);
    /* The breakdown takes the room, the statistics keep their width */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    paned_use_thin_handle(paned);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), dt->split);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), dt->stats);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_box_append(GTK_BOX(page), paned);

    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_page_key), dt);
    gtk_widget_add_controller(page, keys);
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, dt, tab_free);

    dt->label = g_object_ref(icon_label_new("drive-harddisk", _("Disk Usage")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), dt);
    gtk_widget_add_controller(dt->label, GTK_EVENT_CONTROLLER(mclick));

    insert_page(app, dt);
    return dt;
}

void disk_usage_tab_show(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL) return;
    DiskUsageTab* dt = ensure_page(app);
    if (dt == NULL) return;
    if (g_strcmp0(dt->root, path) != 0) g_ptr_array_set_size(dt->excluded, 0); /* another folder */
    g_free(dt->root);
    dt->root = g_strdup(path);
    start_scan(dt, FALSE);
    /* The preview panel holds the tab: show it when hidden */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, dt->page));
    gtk_widget_grab_focus(disk_usage_list_view(dt->list));
}

gboolean disk_usage_tab_close_shown(AppData* app) {
    if (app == NULL || app->disk_usage_page == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, app->disk_usage_page)) return FALSE;
    if (!gtk_widget_get_mapped(app->disk_usage_page)) return FALSE;
    close_page(app);
    return TRUE;
}

void disk_usage_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->disk_usage_page != NULL) g_object_unref(ws->disk_usage_page);
    ws->disk_usage_page = page;
}

void disk_usage_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->disk_usage_page == NULL) return;
    GtkWidget* page = ws->disk_usage_page;
    ws->disk_usage_page = NULL;
    DiskUsageTab* dt = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (dt != NULL && app->notebook != NULL && app->disk_usage_page == NULL) insert_page(app, dt);
    g_object_unref(page);
}

void disk_usage_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            DiskUsageTab* dt = g_object_get_data(G_OBJECT(page), PAGE_KEY);
            if (dt != NULL) stop_scan(dt);
            g_object_unref(page);
        }
    }
    if (ws->disk_usage_page != NULL) {
        DiskUsageTab* dt = g_object_get_data(G_OBJECT(ws->disk_usage_page), PAGE_KEY);
        if (dt != NULL) stop_scan(dt);
        g_clear_object(&ws->disk_usage_page);
    }
}
