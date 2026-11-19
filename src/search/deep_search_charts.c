/* The charts of the Deep Search tab (see deep_search_charts.h) */
#include "search/deep_search_charts.h"
#include "core/date_bins.h"
#include "core/disk_scan.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "widgets/histogram.h"

#include <string.h>

#define CHARTS_KEY "lookin-deep-search-charts"
#define DATE_GROUPS_MAX 24 /* bars of the dates at most */

/* A bar of the dates */
typedef struct {
    gint key;       /* date_bins key */
    gint first_day; /* 0: no bottom (the oldest years merged) */
    gint last_day;
    DiskTally tally;
} DateGroup;

typedef struct {
    GtkWidget* box;
    GtkWidget* sizes_chart;
    GtkWidget* dates_chart;
    GtkWidget* show_all;
    void (*changed)(gpointer data, const DeepSearchRange* range);
    gpointer data;

    DiskTally sizes[DISK_SIZE_COUNT];
    GHashTable* days; /* day (YYYYMMDD) -> DiskTally* */
    guint64 n_files;
    guint64 size;
    gint first_day;   /* oldest and newest day counted, 0: none */
    gint last_day;
    gboolean dirty;   /* counted since the last refresh */
    gint64 day_start; /* the last day met, its times [day_start, day_end) */
    gint64 day_end;
    gint day;

    GArray* groups;   /* DateGroup, the bars of the dates shown */
    DateBinUnit unit;

    gint size_chosen; /* DiskSize, -1: none */
    gboolean date_chosen;
    DateBinUnit date_unit; /* the bar chosen: its unit and key */
    gint date_key;
} Charts;

static Charts* of_charts(GtkWidget* charts) {
    return charts != NULL ? g_object_get_data(G_OBJECT(charts), CHARTS_KEY) : NULL;
}

/* The range of the bars chosen into range; FALSE when none is */
static gboolean chosen_range(Charts* c, DeepSearchRange* range) {
    *range = (DeepSearchRange){ -1, -1, 0, 0 };
    if (c->size_chosen >= 0) disk_size_bounds((DiskSize)c->size_chosen, &range->min_size, &range->max_size);
    for (guint i = 0; c->date_chosen && i < c->groups->len; i++) {
        const DateGroup* g = &g_array_index(c->groups, DateGroup, i);
        if (g->key != c->date_key) continue;
        range->first_day = g->first_day;
        range->last_day = g->last_day;
    }
    return c->size_chosen >= 0 || c->date_chosen;
}

static void notify(Charts* c) {
    DeepSearchRange range;
    gboolean any = chosen_range(c, &range);
    gtk_widget_set_visible(c->show_all, any);
    if (c->changed != NULL) c->changed(c->data, any ? &range : NULL);
}

static void show_sizes(Charts* c) {
    HistogramBin bins[DISK_SIZE_COUNT];
    gchar* tips[DISK_SIZE_COUNT];
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) {
        tips[g] = disk_format_group(disk_size_name((DiskSize)g), &c->sizes[g], c->n_files, c->size);
        bins[g] = (HistogramBin){ disk_size_label((DiskSize)g), tips[g], c->sizes[g].count, c->sizes[g].size };
    }
    histogram_set(c->sizes_chart, c->n_files > 0 ? bins : NULL, c->n_files > 0 ? DISK_SIZE_COUNT : 0);
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) g_free(tips[g]);
    histogram_set_selected(c->sizes_chart, c->size_chosen);
}

/* The bars of the dates from what was counted */
static void build_groups(Charts* c) {
    g_array_set_size(c->groups, 0);
    if (c->n_files == 0) return;
    c->unit = date_bins_unit(c->first_day, c->last_day, DATE_GROUPS_MAX);
    gint start = date_bins_key(c->first_day, c->unit);
    gint end = date_bins_key(c->last_day, c->unit);
    gboolean merged = date_bins_span(start, end, c->unit) > DATE_GROUPS_MAX; /* years only */
    if (merged) start = end - (DATE_GROUPS_MAX - 1);
    for (gint key = start; key <= end; key = date_bins_next(key, c->unit)) {
        DateGroup g = { key, 0, 0, { 0, 0 } };
        date_bins_days(key, c->unit, &g.first_day, &g.last_day);
        if (merged && key == start) g.first_day = 0;
        g_array_append_val(c->groups, g);
    }
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, c->days);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        gint key = date_bins_key(GPOINTER_TO_INT(k), c->unit);
        guint i = key <= start ? 0 : date_bins_span(start, key, c->unit) - 1;
        if (i >= c->groups->len) continue;
        DateGroup* g = &g_array_index(c->groups, DateGroup, i);
        g->tally.count += ((DiskTally*)v)->count;
        g->tally.size += ((DiskTally*)v)->size;
    }
}

static void show_dates(Charts* c) {
    build_groups(c);
    guint n = c->groups->len;
    HistogramBin* bins = g_new0(HistogramBin, MAX(n, 1));
    gchar** texts = g_new0(gchar*, 2 * MAX(n, 1));
    gint chosen = -1;
    for (guint i = 0; i < n; i++) {
        const DateGroup* g = &g_array_index(c->groups, DateGroup, i);
        gchar* label = date_bins_label(g->key, c->unit);
        gchar* name = date_bins_name(g->key, c->unit);
        if (g->first_day == 0) {
            /* The oldest years merged in the first bar */
            gchar* up_to = g_strdup_printf("≤%s", label);
            g_free(label);
            label = up_to;
            /* TRANSLATORS: a bar of the dates: the files of this year and of the years before */
            gchar* full = g_strdup_printf(_("%s and before"), name);
            g_free(name);
            name = full;
        }
        texts[2 * i] = label;
        texts[2 * i + 1] = disk_format_group(name, &g->tally, c->n_files, c->size);
        g_free(name);
        bins[i] = (HistogramBin){ label, texts[2 * i + 1], g->tally.count, g->tally.size };
        if (c->date_chosen && c->date_unit == c->unit && g->key == c->date_key) chosen = (gint)i;
    }
    histogram_set(c->dates_chart, bins, n);
    for (guint i = 0; i < 2 * n; i++) g_free(texts[i]);
    g_free(texts);
    g_free(bins);
    histogram_set_selected(c->dates_chart, chosen);
    if (c->date_chosen && chosen < 0) {
        /* The bars changed under the one chosen (another unit): it goes */
        c->date_chosen = FALSE;
        notify(c);
    }
}

static void on_size_bar(gpointer data, guint bin) {
    Charts* c = data;
    c->size_chosen = c->size_chosen == (gint)bin ? -1 : (gint)bin;
    histogram_set_selected(c->sizes_chart, c->size_chosen);
    notify(c);
}

static void on_date_bar(gpointer data, guint bin) {
    Charts* c = data;
    if (bin >= c->groups->len) return;
    gint key = g_array_index(c->groups, DateGroup, bin).key;
    gboolean same = c->date_chosen && c->date_unit == c->unit && c->date_key == key;
    c->date_chosen = !same;
    c->date_unit = c->unit;
    c->date_key = key;
    histogram_set_selected(c->dates_chart, same ? -1 : (gint)bin);
    notify(c);
}

static void on_show_all(GtkButton* button, gpointer data) {
    (void)button;
    Charts* c = data;
    c->size_chosen = -1;
    c->date_chosen = FALSE;
    histogram_set_selected(c->sizes_chart, -1);
    histogram_set_selected(c->dates_chart, -1);
    notify(c);
}

static GtkWidget* column(const gchar* title, GtkWidget* chart) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "ds-chart-title");
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), chart);
    gtk_widget_set_hexpand(box, TRUE);
    return box;
}

static void charts_free(gpointer data) {
    Charts* c = data;
    g_hash_table_unref(c->days);
    g_array_unref(c->groups);
    g_free(c);
}

GtkWidget* deep_search_charts_new(void (*changed)(gpointer data, const DeepSearchRange* range), gpointer data) {
    Charts* c = g_new0(Charts, 1);
    c->changed = changed;
    c->data = data;
    c->days = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    c->groups = g_array_new(FALSE, TRUE, sizeof(DateGroup));
    c->size_chosen = -1;

    c->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(c->box, "ds-charts");
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* hint = gtk_label_new(_("Click a bar to list only its files"));
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hint), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_widget_add_css_class(hint, "ds-chart-hint");
    gtk_box_append(GTK_BOX(head), hint);
    c->show_all = icon_button_new("edit-clear", _("List Every Result Again"));
    icon_button_set_content(c->show_all, "edit-clear", _("Show All"));
    gtk_widget_set_visible(c->show_all, FALSE);
    g_signal_connect(c->show_all, "clicked", G_CALLBACK(on_show_all), c);
    gtk_box_append(GTK_BOX(head), c->show_all);
    gtk_box_append(GTK_BOX(c->box), head);

    c->sizes_chart = histogram_new();
    histogram_set_activate(c->sizes_chart, on_size_bar, c);
    GtkWidget* sizes = column(_("By Size"), c->sizes_chart);
    c->dates_chart = histogram_new();
    histogram_set_activate(c->dates_chart, on_date_bar, c);
    GtkWidget* dates = column(_("By Last Change"), c->dates_chart);
    /* Side by side in a wide panel, else one over the other */
    gtk_box_append(GTK_BOX(c->box), histogram_pair_new(sizes, dates));
    g_object_set_data_full(G_OBJECT(c->box), CHARTS_KEY, c, charts_free);
    return c->box;
}

void deep_search_charts_reset(GtkWidget* charts) {
    Charts* c = of_charts(charts);
    if (c == NULL) return;
    memset(c->sizes, 0, sizeof c->sizes);
    g_hash_table_remove_all(c->days);
    c->n_files = 0;
    c->size = 0;
    c->first_day = 0;
    c->last_day = 0;
    c->size_chosen = -1;
    c->date_chosen = FALSE;
    gtk_widget_set_visible(c->show_all, FALSE);
    c->dirty = TRUE;
    deep_search_charts_refresh(charts);
}

void deep_search_charts_add(GtkWidget* charts, const GPtrArray* hits) {
    Charts* c = of_charts(charts);
    if (c == NULL || hits == NULL) return;
    for (guint i = 0; i < hits->len; i++) {
        const DeepHit* h = g_ptr_array_index(hits, i);
        if (h->is_dir) continue;
        DiskTally* s = &c->sizes[disk_size_of(h->size)];
        s->count++;
        s->size += h->size;
        if (h->mtime < c->day_start || h->mtime >= c->day_end) {
            c->day = date_bins_day_span(h->mtime, &c->day_start, &c->day_end);
        }
        gint day = c->day;
        DiskTally* d = g_hash_table_lookup(c->days, GINT_TO_POINTER(day));
        if (d == NULL) {
            d = g_new0(DiskTally, 1);
            g_hash_table_insert(c->days, GINT_TO_POINTER(day), d);
        }
        d->count++;
        d->size += h->size;
        if (c->first_day == 0 || day < c->first_day) c->first_day = day;
        if (day > c->last_day) c->last_day = day;
        c->n_files++;
        c->size += h->size;
        c->dirty = TRUE;
    }
}

void deep_search_charts_refresh(GtkWidget* charts) {
    Charts* c = of_charts(charts);
    if (c == NULL || !c->dirty) return;
    c->dirty = FALSE;
    show_sizes(c);
    show_dates(c);
}
