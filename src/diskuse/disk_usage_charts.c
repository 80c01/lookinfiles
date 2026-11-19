/* The histograms of the Disk Usage tab (see disk_usage_charts.h) */
#include "diskuse/disk_usage_charts.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "widgets/histogram.h"

#define CHARTS_KEY "lookin-disk-usage-charts"

typedef struct {
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

/* One histogram of n groups of node */
static void fill(GtkWidget* chart, const DiskNode* node, const DiskTally* tallies, guint n,
                 const gchar* (*name)(gint), const gchar* (*label)(gint)) {
    HistogramBin* bins = g_new0(HistogramBin, n);
    gchar** tips = g_new0(gchar*, n);
    for (guint i = 0; i < n; i++) {
        tips[i] = disk_format_group(name((gint)i), &tallies[i], node->n_files, node->size);
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

GtkWidget* disk_usage_charts_new(void) {
    Charts* c = g_new0(Charts, 1);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "du-charts");
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    c->note = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(c->note), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(c->note), TRUE);
    gtk_widget_add_css_class(c->note, "du-muted");
    gtk_box_append(GTK_BOX(box), c->note);
    c->charts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(c->charts), TRUE);
    gtk_widget_set_vexpand(c->charts, TRUE);
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    c->sizes = titled(left, _("By Size"), histogram_new());
    c->ages = titled(right, _("By Last Change"), histogram_new());
    gtk_widget_add_css_class(c->sizes, "du-histogram");
    gtk_widget_add_css_class(c->ages, "du-histogram");
    gtk_box_append(GTK_BOX(c->charts), left);
    gtk_box_append(GTK_BOX(c->charts), right);
    gtk_box_append(GTK_BOX(box), c->charts);
    g_object_set_data_full(G_OBJECT(box), CHARTS_KEY, c, g_free);
    disk_usage_charts_show(box, NULL);
    return box;
}

void disk_usage_charts_show(GtkWidget* charts, const DiskNode* node) {
    Charts* c = charts != NULL ? g_object_get_data(G_OBJECT(charts), CHARTS_KEY) : NULL;
    if (c == NULL) return;
    gboolean files = node != NULL && node->n_files > 0;
    gtk_widget_set_visible(c->charts, files);
    gtk_widget_set_visible(c->note, !files);
    if (!files) {
        gtk_label_set_text(GTK_LABEL(c->note),
                           node == NULL ? _("The charts come once the folder is read") : _("(no file)"));
        return;
    }
    fill(c->sizes, node, node->sizes, DISK_SIZE_COUNT, size_name, size_label);
    fill(c->ages, node, node->ages, DISK_AGE_COUNT, age_name, age_label);
}
