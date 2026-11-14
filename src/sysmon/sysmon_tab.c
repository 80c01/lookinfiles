/* The System Monitor tab: a dashboard of the machine (see sysmon_tab.h) */
#include "sysmon/sysmon_tab.h"
#include "sysmon/sysmon_procs.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/sysstat.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/palette.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"
#include "widgets/sparkline.h"

#define PAGE_KEY "lookin-sysmon-tab"
#define HISTORY 120           /* chart points: two minutes */
#define MOUNTS_EVERY 5        /* seconds between two readings of the filesystems */
#define WARN_PERCENT 80.0
#define CRIT_PERCENT 95.0

/* A card of the dashboard: title and value, chart, details */
typedef struct {
    GtkWidget* value;
    GtkWidget* chart;
    GtkWidget* detail;
} Card;

typedef struct {
    AppData* app;
    GtkWidget* page;     /* the notebook page (app->sysmon_page) */
    GtkWidget* label;    /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* info;     /* header: uptime, CPUs, tasks */
    Card cpu, mem, load, net, disk;
    GtkWidget* cores;    /* drawing area: a bar per core */
    GArray* core_values; /* gdouble */
    GtkWidget* swap_bar;
    GtkWidget* mounts;   /* GtkGrid of the filesystems */
    GtkWidget* procs;    /* sysmon_procs */
    GtkWidget* procs_title;
    gchar* kernel;
    guint net_rx, net_tx, disk_read, disk_write; /* series of the charts */
    gdouble load_peak;   /* highest load shown */

    SysSample* prev;     /* last sample read */
    GCancellable* cancel;
    gboolean busy;       /* a reading is running */
    guint timer;
    guint ticks;
    gint64 mounts_at;    /* time of the last filesystem reading, 0: never */
} SysmonTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".sysmon { background-color: @bg_view; }"
                  ".sysmon .sysmon-header { padding: 4px 10px; color: @fg; }"
                  ".sysmon .sysmon-info { color: @fg_subtle; font-size: @size_small; }"
                  ".sysmon .sysmon-card { background-color: @bg_block; border: 1px solid @border;"
                  "  border-radius: 6px; padding: 8px 10px; }"
                  ".sysmon .sysmon-card-title { color: @fg_muted; font-size: @size_small; font-weight: bold; }"
                  ".sysmon .sysmon-card-value { color: @fg_strong; font-size: @size_title; }"
                  ".sysmon .sysmon-detail { color: @fg_subtle; font-size: @size_small; }"
                  ".sysmon .sysmon-section { color: @fg_strong; font-weight: bold; padding: 10px 12px 4px 12px; }"
                  ".sysmon .sysmon-mount { color: @fg_text; }"
                  ".sysmon .sysmon-num { font-feature-settings: \"tnum\"; }"
                  ".sysmon flowbox, .sysmon flowboxchild { background: none; padding: 0; }"
                  ".sysmon levelbar trough { background-color: @bg_input; border: none; border-radius: 3px;"
                  "  min-height: 6px; padding: 0; }"
                  ".sysmon levelbar block { min-height: 6px; border: none; border-radius: 3px; }"
                  ".sysmon levelbar block.filled { background-color: @accent; }"
                  ".sysmon levelbar block.empty { background-color: transparent; }"
                  ".sysmon levelbar.sysmon-warn block.filled { background-color: @warning; }"
                  ".sysmon levelbar.sysmon-crit block.filled { background-color: @error; }");
}

static SysmonTab* tab_state(AppData* app) {
    return app != NULL && app->sysmon_page != NULL ? g_object_get_data(G_OBJECT(app->sysmon_page), PAGE_KEY)
                                                   : NULL;
}

/* "12.3 MB/s" */
static gchar* format_rate(gdouble bytes_per_second) {
    gchar* size = g_format_size((guint64)(bytes_per_second + 0.5));
    gchar* text = g_strdup_printf(_("%s/s"), size);
    g_free(size);
    return text;
}

static gchar* format_uptime(gdouble seconds) {
    guint64 s = (guint64)seconds;
    guint days = (guint)(s / 86400);
    guint hours = (guint)(s % 86400 / 3600);
    guint minutes = (guint)(s % 3600 / 60);
    if (days == 0) return g_strdup_printf("%u:%02u", hours, minutes);
    gchar* d = g_strdup_printf(ngettext("%u day", "%u days", days), days);
    gchar* text = g_strdup_printf("%s %u:%02u", d, hours, minutes);
    g_free(d);
    return text;
}

/* Colour of a level bar by how full it is */
static void level_set(GtkWidget* bar, gdouble percent) {
    gtk_level_bar_set_value(GTK_LEVEL_BAR(bar), CLAMP(percent / 100.0, 0.0, 1.0));
    gtk_widget_remove_css_class(bar, "sysmon-warn");
    gtk_widget_remove_css_class(bar, "sysmon-crit");
    if (percent >= CRIT_PERCENT) {
        gtk_widget_add_css_class(bar, "sysmon-crit");
    } else if (percent >= WARN_PERCENT) {
        gtk_widget_add_css_class(bar, "sysmon-warn");
    }
}

static GtkWidget* level_new(void) {
    GtkWidget* bar = gtk_level_bar_new_for_interval(0.0, 1.0);
    /* No offsets: the colour comes from level_set() */
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_LOW);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_HIGH);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_FULL);
    gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
    return bar;
}

static GtkWidget* small_label(const gchar* css) {
    GtkWidget* l = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(l, css);
    return l;
}

/* A card in flow: title and value on a row, the chart, then the details */
static GtkWidget* card_new(Card* c, GtkWidget* flow, const gchar* title, PaletteRole role, gdouble max) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "sysmon-card");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* t = small_label("sysmon-card-title");
    gtk_label_set_text(GTK_LABEL(t), title);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(row), t);
    c->value = gtk_label_new("–");
    gtk_widget_add_css_class(c->value, "sysmon-card-value");
    gtk_widget_add_css_class(c->value, "sysmon-num");
    gtk_box_append(GTK_BOX(row), c->value);
    gtk_box_append(GTK_BOX(box), row);
    c->chart = sparkline_new(HISTORY);
    sparkline_add_series(c->chart, role);
    sparkline_set_max(c->chart, max);
    sparkline_set_grid(c->chart, TRUE);
    gtk_widget_set_size_request(c->chart, 220, 64);
    gtk_box_append(GTK_BOX(box), c->chart);
    c->detail = small_label("sysmon-detail");
    gtk_label_set_ellipsize(GTK_LABEL(c->detail), PANGO_ELLIPSIZE_NONE);
    gtk_label_set_wrap(GTK_LABEL(c->detail), TRUE);
    gtk_box_append(GTK_BOX(box), c->detail);
    gtk_flow_box_append(GTK_FLOW_BOX(flow), box);
    return box;
}

static void set_label(GtkWidget* label, gchar* text) {
    gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

/* A bar per core, the height its use */
static void on_draw_cores(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    SysmonTab* st = data;
    guint n = st->core_values->len;
    if (n == 0) return;
    double gap = n > 32 ? 1.0 : 2.0;
    double w = ((double)width - gap * (double)(n - 1)) / (double)n;
    if (w < 1.0) w = 1.0;
    double r, g, b;
    palette_rgb(PAL_BG_INPUT, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    for (guint i = 0; i < n; i++) cairo_rectangle(cr, (w + gap) * i, 0, w, height);
    cairo_fill(cr);
    palette_rgb(PAL_ACCENT_FG, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    for (guint i = 0; i < n; i++) {
        double v = CLAMP(g_array_index(st->core_values, gdouble, i), 0.0, 100.0) / 100.0;
        double h = (double)height * v;
        cairo_rectangle(cr, (w + gap) * i, (double)height - h, w, h);
    }
    cairo_fill(cr);
}

/* "used / total (percent), free" of a filesystem */
static gchar* mount_size_text(const SysMount* m, gdouble percent) {
    gchar* used = g_format_size(m->used);
    gchar* total = g_format_size(m->total);
    gchar* avail = g_format_size(m->avail);
    /* used / total (percent), free */
    gchar* text = g_strdup_printf(_("%s / %s (%.0f%%), %s free"), used, total, percent, avail);
    g_free(used);
    g_free(total);
    g_free(avail);
    return text;
}

static gchar* mount_device_text(const SysMount* m) {
    return g_strdup_printf("%s · %s", m->device, m->fs_type);
}

/* Does the grid show these filesystems, in this order? */
static gboolean mounts_shown(SysmonTab* st, GPtrArray* mounts) {
    GtkGrid* grid = GTK_GRID(st->mounts);
    if (mounts->len == 0 || gtk_grid_get_child_at(grid, 0, (gint)mounts->len) != NULL) return FALSE;
    for (guint i = 0; i < mounts->len; i++) {
        const SysMount* m = g_ptr_array_index(mounts, i);
        GtkWidget* dir = gtk_grid_get_child_at(grid, 0, (gint)i);
        GtkWidget* dev = gtk_grid_get_child_at(grid, 1, (gint)i);
        if (!GTK_IS_LABEL(dir) || !GTK_IS_LABEL(dev) || gtk_grid_get_child_at(grid, 3, (gint)i) == NULL) return FALSE;
        if (g_strcmp0(gtk_label_get_text(GTK_LABEL(dir)), m->mount_point) != 0) return FALSE;
        gchar* device = mount_device_text(m);
        gboolean same = g_strcmp0(gtk_label_get_text(GTK_LABEL(dev)), device) == 0;
        g_free(device);
        if (!same) return FALSE;
    }
    return TRUE;
}

/* The filesystems: the same ones get their sizes updated in place, the
 * rows are built again only when the list changes */
static void show_mounts(SysmonTab* st, GPtrArray* mounts) {
    if (mounts_shown(st, mounts)) {
        for (guint i = 0; i < mounts->len; i++) {
            const SysMount* m = g_ptr_array_index(mounts, i);
            gdouble percent = m->total > 0 ? 100.0 * (gdouble)m->used / (gdouble)m->total : 0.0;
            level_set(gtk_grid_get_child_at(GTK_GRID(st->mounts), 2, (gint)i), percent);
            GtkWidget* size = gtk_grid_get_child_at(GTK_GRID(st->mounts), 3, (gint)i);
            gchar* text = mount_size_text(m, percent);
            if (g_strcmp0(gtk_label_get_text(GTK_LABEL(size)), text) != 0) gtk_label_set_text(GTK_LABEL(size), text);
            g_free(text);
        }
        return;
    }
    if (mounts->len == 0 && gtk_grid_get_child_at(GTK_GRID(st->mounts), 0, 0) != NULL &&
        gtk_grid_get_child_at(GTK_GRID(st->mounts), 1, 0) == NULL) {
        return; /* "(empty)" already */
    }
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(st->mounts)) != NULL) gtk_grid_remove(GTK_GRID(st->mounts), child);
    for (guint i = 0; i < mounts->len; i++) {
        const SysMount* m = g_ptr_array_index(mounts, i);
        gint row = (gint)i;
        gdouble percent = m->total > 0 ? 100.0 * (gdouble)m->used / (gdouble)m->total : 0.0;

        GtkWidget* dir = small_label("sysmon-mount");
        gtk_label_set_text(GTK_LABEL(dir), m->mount_point);
        gtk_widget_set_tooltip_text(dir, m->mount_point);
        gtk_label_set_max_width_chars(GTK_LABEL(dir), 28);
        gtk_grid_attach(GTK_GRID(st->mounts), dir, 0, row, 1, 1);

        GtkWidget* dev = small_label("sysmon-detail");
        set_label(dev, mount_device_text(m));
        gtk_label_set_max_width_chars(GTK_LABEL(dev), 30);
        gtk_grid_attach(GTK_GRID(st->mounts), dev, 1, row, 1, 1);

        GtkWidget* bar = level_new();
        gtk_widget_set_hexpand(bar, TRUE);
        gtk_widget_set_size_request(bar, 80, -1);
        level_set(bar, percent);
        gtk_grid_attach(GTK_GRID(st->mounts), bar, 2, row, 1, 1);

        GtkWidget* size = small_label("sysmon-detail");
        gtk_widget_add_css_class(size, "sysmon-num");
        gtk_label_set_xalign(GTK_LABEL(size), 1.0f);
        set_label(size, mount_size_text(m, percent));
        gtk_grid_attach(GTK_GRID(st->mounts), size, 3, row, 1, 1);
    }
    if (mounts->len == 0) {
        GtkWidget* none = small_label("sysmon-detail");
        gtk_label_set_text(GTK_LABEL(none), _("(empty)"));
        gtk_grid_attach(GTK_GRID(st->mounts), none, 0, 0, 1, 1);
    }
}

/* A new sample (rates filled): every card, and what was read with it */
static void show_sample(SysmonTab* st, SysSample* s) {
    guint ncpu = s->cpu->len > 1 ? s->cpu->len - 1 : 1;

    sparkline_push(st->cpu.chart, 0, s->cpu_percent);
    set_label(st->cpu.value, g_strdup_printf("%.0f%%", s->cpu_percent));
    g_array_set_size(st->core_values, 0);
    g_array_append_vals(st->core_values, s->core_percent->data, s->core_percent->len);
    gtk_widget_queue_draw(st->cores);
    gdouble busiest = 0;
    for (guint i = 0; i < s->core_percent->len; i++) busiest = MAX(busiest, g_array_index(s->core_percent, gdouble, i));
    set_label(st->cpu.detail, g_strdup_printf(ngettext("%u core, busiest at %.0f%%", "%u cores, busiest at %.0f%%", ncpu),
                                              ncpu, busiest));

    guint64 used = sysstat_mem_used(&s->mem);
    gdouble mem_percent = s->mem.total > 0 ? 100.0 * (gdouble)used / (gdouble)s->mem.total : 0.0;
    sparkline_push(st->mem.chart, 0, mem_percent);
    set_label(st->mem.value, g_strdup_printf("%.0f%%", mem_percent));
    gchar* u = g_format_size(used);
    gchar* t = g_format_size(s->mem.total);
    gchar* c = g_format_size(s->mem.cached + s->mem.buffers);
    guint64 swap_used = s->mem.swap_total > s->mem.swap_free ? s->mem.swap_total - s->mem.swap_free : 0;
    gchar* su = g_format_size(swap_used);
    gchar* stot = g_format_size(s->mem.swap_total);
    /* used of total, cache; swap used of total */
    set_label(st->mem.detail, s->mem.swap_total > 0
                                  ? g_strdup_printf(_("%s of %s used, %s cache\nSwap: %s of %s"), u, t, c, su, stot)
                                  : g_strdup_printf(_("%s of %s used, %s cache\nNo swap"), u, t, c));
    level_set(st->swap_bar, s->mem.swap_total > 0 ? 100.0 * (gdouble)swap_used / (gdouble)s->mem.swap_total : 0.0);
    gtk_widget_set_visible(st->swap_bar, s->mem.swap_total > 0);
    g_free(u);
    g_free(t);
    g_free(c);
    g_free(su);
    g_free(stot);

    /* Scale of the load: the number of CPUs, or more when the load goes over */
    st->load_peak = MAX(st->load_peak, s->load[0]);
    sparkline_set_max(st->load.chart, MAX((gdouble)ncpu, st->load_peak * 1.1));
    sparkline_push(st->load.chart, 0, s->load[0]);
    set_label(st->load.value, g_strdup_printf("%.2f", s->load[0]));
    set_label(st->load.detail,
              g_strdup_printf(_("1, 5, 15 minutes: %.2f  %.2f  %.2f\n%u running of %u tasks"), s->load[0], s->load[1],
                              s->load[2], s->tasks_running, s->tasks_total));

    sparkline_push(st->net.chart, st->net_rx, s->net_rx_rate);
    sparkline_push(st->net.chart, st->net_tx, s->net_tx_rate);
    gchar* rx = format_rate(s->net_rx_rate);
    gchar* tx = format_rate(s->net_tx_rate);
    set_label(st->net.value, g_strdup_printf("↓ %s", rx));
    set_label(st->net.detail, g_strdup_printf(_("Received %s, sent %s (%u interfaces)"), rx, tx, s->net->len));
    g_free(rx);
    g_free(tx);

    sparkline_push(st->disk.chart, st->disk_read, s->disk_read_rate);
    sparkline_push(st->disk.chart, st->disk_write, s->disk_write_rate);
    set_label(st->disk.value, g_strdup_printf("%.0f%%", s->disk_busy));
    gchar* rd = format_rate(s->disk_read_rate);
    gchar* wr = format_rate(s->disk_write_rate);
    set_label(st->disk.detail, g_strdup_printf(_("Read %s, written %s; busiest disk at %.0f%%"), rd, wr, s->disk_busy));
    g_free(rd);
    g_free(wr);

    gchar* up = format_uptime(s->uptime);
    set_label(st->info, g_strdup_printf(_("%s · Linux %s · up %s · %u CPUs"), g_get_host_name(),
                                        st->kernel != NULL ? st->kernel : "?", up, ncpu));
    g_free(up);

    if (s->mounts != NULL) show_mounts(st, s->mounts);
    if (s->procs != NULL) {
        sysmon_procs_update(st->procs, s->procs);
        set_label(st->procs_title, g_strdup_printf(_("Processes (%u)"), s->procs->len));
    }
}

typedef struct {
    SysSample* prev; /* borrowed: the tab keeps it until the reading is done */
    SysSampleFlags flags;
} ReadJob;

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    (void)cancel;
    ReadJob* job = data;
    SysSample* s = sysstat_sample(NULL, job->flags);
    sysstat_sample_rates(s, job->prev);
    g_task_return_pointer(task, s, (GDestroyNotify)sysstat_sample_free);
}

static void on_read(GObject* source, GAsyncResult* res, gpointer data) {
    (void)data;
    SysmonTab* st = g_object_get_data(source, PAGE_KEY);
    SysSample* s = g_task_propagate_pointer(G_TASK(res), NULL);
    if (st == NULL || s == NULL) {
        sysstat_sample_free(s);
        return;
    }
    st->busy = FALSE;
    if (g_cancellable_is_cancelled(st->cancel)) {
        sysstat_sample_free(s);
        return;
    }
    if (st->prev != NULL) {
        show_sample(st, s);
    } else {
        set_label(st->procs_title, g_strdup(_("Processes")));
        if (s->mounts != NULL) show_mounts(st, s->mounts);
    }
    if (s->mounts != NULL) st->mounts_at = s->time_us;
    sysstat_sample_free(st->prev);
    st->prev = s;
}

/* Every second: read the statistics in a worker (the processes and the
 * filesystems only while the page is on screen) */
static gboolean on_tick(gpointer data) {
    SysmonTab* st = data;
    if (st->busy) return G_SOURCE_CONTINUE;
    gboolean shown = gtk_widget_get_mapped(st->page);
    SysSampleFlags flags = 0;
    if (shown) {
        flags |= SYSSTAT_PROCS;
        if (st->mounts_at == 0 || g_get_monotonic_time() - st->mounts_at >= MOUNTS_EVERY * G_USEC_PER_SEC) {
            flags |= SYSSTAT_MOUNTS;
        }
    }
    ReadJob* job = g_new0(ReadJob, 1);
    job->prev = st->prev;
    job->flags = flags;
    st->busy = TRUE;
    GTask* task = g_task_new(st->page, st->cancel, on_read, NULL);
    g_task_set_task_data(task, job, g_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
    return G_SOURCE_CONTINUE;
}

/* The page is shown again: read now rather than at the next tick */
static void on_map(GtkWidget* page, gpointer data) {
    (void)page;
    SysmonTab* st = data;
    st->mounts_at = 0;
    on_tick(st);
}

static void stop(SysmonTab* st) {
    if (st->timer != 0) {
        g_source_remove(st->timer);
        st->timer = 0;
    }
    g_cancellable_cancel(st->cancel);
}

static void tab_free(gpointer data) {
    SysmonTab* st = data;
    stop(st);
    g_clear_object(&st->cancel);
    sysstat_sample_free(st->prev);
    g_array_unref(st->core_values);
    g_free(st->kernel);
    g_clear_object(&st->label);
    g_free(st);
}

static void close_page(AppData* app) {
    if (app->sysmon_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->sysmon_page;
    SysmonTab* st = tab_state(app);
    if (st != NULL) stop(st);
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint num = gtk_notebook_page_num(nb, closed);
    app->sysmon_page = NULL;
    if (num >= 0) gtk_notebook_remove_page(nb, num);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees the state, once a reading still running is done */
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the top bar chart */
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((SysmonTab*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((SysmonTab*)data)->app);
}

/* Put the page in the notebook: after the Git tab and the result pages */
static void insert_page(AppData* app, SysmonTab* st) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    if (app->help_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->help_page));
    gtk_notebook_insert_page(nb, st->page, st->label, after + 1);
    app->sysmon_page = st->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* Take the page out of the notebook with its tab: an owned ref, or NULL */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->sysmon_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    app->sysmon_page = NULL;
    if (n >= 0) gtk_notebook_remove_page(nb, n);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    return page;
}

static GtkWidget* section_title(const gchar* text) {
    GtkWidget* l = small_label("sysmon-section");
    gtk_label_set_text(GTK_LABEL(l), text);
    return l;
}

static SysmonTab* ensure_page(AppData* app) {
    SysmonTab* st = tab_state(app);
    if (st != NULL || app->notebook == NULL) return st;
    ensure_css();
    st = g_new0(SysmonTab, 1);
    st->app = app;
    st->cancel = g_cancellable_new();
    st->core_values = g_array_new(FALSE, TRUE, sizeof(gdouble));
    gchar* kernel = NULL;
    if (g_file_get_contents("/proc/sys/kernel/osrelease", &kernel, NULL, NULL)) {
        st->kernel = g_strdup(g_strstrip(kernel));
        g_free(kernel);
    }

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "sysmon");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    st->page = page;

    /* Header: title, machine, close button */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "sysmon-header");
    gtk_box_append(GTK_BOX(bar), icon_label_new("utilities-system-monitor", _("System Monitor")));
    st->info = small_label("sysmon-info");
    gtk_widget_set_hexpand(st->info, TRUE);
    gtk_box_append(GTK_BOX(bar), st->info);
    GtkWidget* close = icon_button_new("window-close", _("Close System Monitor"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), st);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    /* Top: the cards and the filesystems */
    GtkWidget* flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 5);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_widget_set_margin_start(flow, 10);
    gtk_widget_set_margin_end(flow, 10);
    gtk_widget_set_margin_top(flow, 6);
    gtk_widget_set_focusable(flow, FALSE);

    GtkWidget* cpu = card_new(&st->cpu, flow, _("CPU"), PAL_ACCENT_FG, 100.0);
    st->cores = gtk_drawing_area_new();
    gtk_widget_set_size_request(st->cores, -1, 18);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(st->cores), on_draw_cores, st, NULL);
    gtk_box_insert_child_after(GTK_BOX(cpu), st->cores, st->cpu.chart);
    GtkWidget* mem = card_new(&st->mem, flow, _("Memory"), PAL_SYN_CONTROL, 100.0);
    st->swap_bar = level_new();
    gtk_widget_set_tooltip_text(st->swap_bar, _("Swap in use"));
    gtk_box_append(GTK_BOX(mem), st->swap_bar);
    card_new(&st->load, flow, _("Load Average"), PAL_WARNING, 0);
    card_new(&st->net, flow, _("Network"), PAL_SUCCESS, 0);
    st->net_rx = 0;
    st->net_tx = sparkline_add_series(st->net.chart, PAL_LINK);
    gtk_widget_set_tooltip_text(st->net.chart, _("Received and sent per second"));
    card_new(&st->disk, flow, _("Disk I/O"), PAL_DIFF_HUNK_FG, 0);
    st->disk_read = 0;
    st->disk_write = sparkline_add_series(st->disk.chart, PAL_GIT_MODIFIED);
    gtk_widget_set_tooltip_text(st->disk.chart, _("Read and written per second"));

    st->mounts = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(st->mounts), 4);
    gtk_grid_set_column_spacing(GTK_GRID(st->mounts), 12);
    gtk_widget_set_margin_start(st->mounts, 12);
    gtk_widget_set_margin_end(st->mounts, 12);
    gtk_widget_set_margin_bottom(st->mounts, 10);

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(top), flow);
    gtk_box_append(GTK_BOX(top), section_title(_("Filesystems")));
    gtk_box_append(GTK_BOX(top), st->mounts);
    GtkWidget* top_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(top_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(top_scroll), top);
    gtk_widget_set_vexpand(top_scroll, TRUE);

    /* Bottom: the processes */
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    st->procs_title = section_title(_("Processes"));
    gtk_box_append(GTK_BOX(bottom), st->procs_title);
    st->procs = sysmon_procs_new(app);
    gtk_box_append(GTK_BOX(bottom), st->procs);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    paned_use_thin_handle(paned);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), top_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), bottom);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 330);
    gtk_box_append(GTK_BOX(page), paned);

    if (!sysstat_available(NULL)) {
        gtk_label_set_text(GTK_LABEL(st->info), _("System statistics are only available on Linux"));
    }

    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, st, tab_free);
    g_signal_connect(page, "map", G_CALLBACK(on_map), st);

    st->label = g_object_ref(icon_label_new("utilities-system-monitor", _("System Monitor")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), st);
    gtk_widget_add_controller(st->label, GTK_EVENT_CONTROLLER(mclick));

    if (sysstat_available(NULL)) st->timer = g_timeout_add_seconds(1, on_tick, st);
    insert_page(app, st);
    return st;
}

void sysmon_tab_show(AppData* app) {
    if (app == NULL) return;
    SysmonTab* st = ensure_page(app);
    if (st == NULL) return;
    /* The preview panel holds the tab: show it when hidden */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, st->page));
    gtk_widget_grab_focus(sysmon_procs_view(st->procs));
}

gboolean sysmon_tab_is_open(AppData* app) {
    return tab_state(app) != NULL;
}

gboolean sysmon_tab_close_shown(AppData* app) {
    if (app == NULL || app->sysmon_page == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, app->sysmon_page)) return FALSE;
    if (!gtk_widget_get_mapped(app->sysmon_page)) return FALSE;
    close_page(app);
    return TRUE;
}

void sysmon_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->sysmon_page != NULL) g_object_unref(ws->sysmon_page);
    ws->sysmon_page = page;
}

void sysmon_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->sysmon_page == NULL) return;
    GtkWidget* page = ws->sysmon_page;
    ws->sysmon_page = NULL;
    SysmonTab* st = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (st != NULL && app->notebook != NULL && app->sysmon_page == NULL) insert_page(app, st);
    g_object_unref(page);
}

void sysmon_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            SysmonTab* st = g_object_get_data(G_OBJECT(page), PAGE_KEY);
            if (st != NULL) stop(st);
            g_object_unref(page);
        }
    }
    if (ws->sysmon_page != NULL) {
        SysmonTab* st = g_object_get_data(G_OBJECT(ws->sysmon_page), PAGE_KEY);
        if (st != NULL) stop(st);
        g_clear_object(&ws->sysmon_page);
    }
}
