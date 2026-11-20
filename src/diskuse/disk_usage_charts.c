/* The histograms of the Disk Usage tab (see disk_usage_charts.h) */
#include "diskuse/disk_usage_charts.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "widgets/histogram.h"

#define CHARTS_KEY "lookin-disk-usage-charts"

typedef struct {
    DiskUsageSearch search;
    gpointer data;
    const DiskNode* node; /* shown (borrowed), NULL when the groups cannot be chosen */
    GtkWidget* note;  /* before the folder is read, or when it has no file */
    GtkWidget* sizes;
    GtkWidget* ages;
    GtkWidget* charts; /* the box of the two */
} Charts;

static GtkWidget* titled(GtkWidget* box, const gchar* title, GtkWidget* chart) {
    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "du-section");
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_set_vexpand(chart, TRUE);
    gtk_box_append(GTK_BOX(box), chart);
    return chart;
}

gchar* disk_usage_charts_search_tip(const gchar* tip) {
    return g_strdup_printf("%s\n%s", tip, _("List These Files in Deep Search (Click / Enter)"));
}

/* One histogram of n groups of node; with searchable, the tooltip of a
 * group holding files tells the click */
static void fill(GtkWidget* chart, const DiskNode* node, const DiskTally* tallies, guint n,
                 const gchar* (*name)(gint), const gchar* (*label)(gint), gboolean searchable) {
    HistogramBin* bins = g_new0(HistogramBin, n);
    gchar** tips = g_new0(gchar*, n);
    for (guint i = 0; i < n; i++) {
        tips[i] = disk_format_group(name((gint)i), &tallies[i], node->n_files, node->size);
        if (searchable && tallies[i].count > 0) {
            gchar* tip = disk_usage_charts_search_tip(tips[i]);
            g_free(tips[i]);
            tips[i] = tip;
        }
        bins[i] = (HistogramBin){ label((gint)i), tips[i], tallies[i].count, tallies[i].size };
    }
    histogram_set(chart, bins, n);
    for (guint i = 0; i < n; i++) g_free(tips[i]);
    g_free(tips);
    g_free(bins);
}

static const gchar* size_name(gint g) {
    return disk_size_name((DiskSize)g);
}

static const gchar* size_label(gint g) {
    return disk_size_label((DiskSize)g);
}

static const gchar* age_name(gint a) {
    return disk_age_name((DiskAge)a);
}

static const gchar* age_label(gint a) {
    return disk_age_label((DiskAge)a);
}

/* The files of a group of the folder shown, in the Deep Search tab */
static void search_group(Charts* c, gboolean size, guint group) {
    const DiskNode* node = c->node;
    if (node == NULL || c->search == NULL) return;
    if (size ? group >= DISK_SIZE_COUNT || node->sizes[group].count == 0
             : group >= DISK_AGE_COUNT || node->ages[group].count == 0) {
        return;
    }
    gchar* folder = disk_node_path(node);
    gchar *a = NULL, *b = NULL;
    DeepSearchRequest r = { .folder = folder };
    if (size) {
        disk_size_criteria((DiskSize)group, &a, &b);
        r.min_size = a;
        r.max_size = b;
    } else {
        disk_age_criteria((DiskAge)group, &a, &b);
        r.after = a;
        r.before = b;
    }
    c->search(c->data, &r);
    g_free(a);
    g_free(b);
    g_free(folder);
}

static void on_size_bar(gpointer data, guint bin) {
    search_group(data, TRUE, bin);
}

static void on_age_bar(gpointer data, guint bin) {
    search_group(data, FALSE, bin);
}

GtkWidget* disk_usage_charts_new(DiskUsageSearch search, gpointer data) {
    Charts* c = g_new0(Charts, 1);
    c->search = search;
    c->data = data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "du-charts");
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    c->note = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(c->note), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(c->note), TRUE);
    gtk_widget_add_css_class(c->note, "du-muted");
    gtk_box_append(GTK_BOX(box), c->note);
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    c->sizes = titled(left, _("By Size"), histogram_new());
    c->ages = titled(right, _("By Last Change"), histogram_new());
    gtk_widget_add_css_class(c->sizes, "du-histogram");
    gtk_widget_add_css_class(c->ages, "du-histogram");
    /* Side by side in a wide panel, else one over the other */
    c->charts = histogram_pair_new(left, right);
    gtk_widget_set_vexpand(c->charts, TRUE);
    gtk_box_append(GTK_BOX(box), c->charts);
    g_object_set_data_full(G_OBJECT(box), CHARTS_KEY, c, g_free);
    disk_usage_charts_show(box, NULL, FALSE);
    return box;
}

void disk_usage_charts_show(GtkWidget* charts, const DiskNode* node, gboolean searchable) {
    Charts* c = charts != NULL ? g_object_get_data(G_OBJECT(charts), CHARTS_KEY) : NULL;
    if (c == NULL) return;
    searchable = searchable && c->search != NULL && node != NULL;
    c->node = searchable ? node : NULL;
    histogram_set_activate(c->sizes, searchable ? on_size_bar : NULL, c);
    histogram_set_activate(c->ages, searchable ? on_age_bar : NULL, c);
    gboolean files = node != NULL && node->n_files > 0;
    gtk_widget_set_visible(c->charts, files);
    gtk_widget_set_visible(c->note, !files);
    if (!files) {
        gtk_label_set_text(GTK_LABEL(c->note),
                           node == NULL ? _("The charts come once the folder is read") : _("(no file)"));
        return;
    }
    fill(c->sizes, node, node->sizes, DISK_SIZE_COUNT, size_name, size_label, searchable);
    fill(c->ages, node, node->ages, DISK_AGE_COUNT, age_name, age_label, searchable);
}
