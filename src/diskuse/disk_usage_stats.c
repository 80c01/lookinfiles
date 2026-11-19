/* The statistics of the Disk Usage tab (see disk_usage_stats.h) */
#include "diskuse/disk_usage_stats.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "widgets/histogram.h"

#include <string.h>

#define STATS_KEY "lookin-disk-usage-stats"
#define PATH_KEY "lookin-disk-usage-path"
#define TOP_EXTENSIONS 12
#define TOP_FILES 25 /* largest files shown (the scan keeps more) */
#define ERRORS_SHOWN 20

typedef struct {
    GtkWidget* box; /* the sections, rebuilt for each scan */
    void (*reveal)(gpointer data, const gchar* path);
    gpointer data;
} DuStats;

static GtkWidget* label_new(const gchar* text, const gchar* css, gfloat xalign) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
    if (css != NULL) gtk_widget_add_css_class(l, css);
    return l;
}

/* A label taking a string it frees */
static GtkWidget* label_take(gchar* text, const gchar* css, gfloat xalign) {
    GtkWidget* l = label_new(text, css, xalign);
    g_free(text);
    return l;
}

/* A number: never cut */
static GtkWidget* number_take(gchar* text, const gchar* css) {
    GtkWidget* l = label_take(text, css, 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_NONE);
    return l;
}

static void section_title(DuStats* ds, const gchar* title) {
    gtk_box_append(GTK_BOX(ds->box), label_new(title, "du-section", 0.0f));
}

static GtkWidget* section(DuStats* ds, const gchar* title) {
    section_title(ds, title);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_add_css_class(grid, "du-grid");
    gtk_box_append(GTK_BOX(ds->box), grid);
    return grid;
}

/* "Name   value" */
static void fact(GtkWidget* grid, gint row, const gchar* name, gchar* value) {
    GtkWidget* n = label_new(name, "du-muted", 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_NONE);
    gtk_grid_attach(GTK_GRID(grid), n, 0, row, 1, 1);
    GtkWidget* v = label_take(value, "du-num", 0.0f);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_tooltip_text(v, gtk_label_get_text(GTK_LABEL(v)));
    gtk_grid_attach(GTK_GRID(grid), v, 1, row, 3, 1);
}

static GtkWidget* share_bar(guint64 part, guint64 whole) {
    GtkWidget* bar = gtk_level_bar_new_for_interval(0.0, 1.0);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_LOW);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_HIGH);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_FULL);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(bar), CLAMP(disk_percent(part, whole) / 100.0, 0.0, 1.0));
    gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(bar, 60, -1);
    gtk_widget_set_hexpand(bar, TRUE);
    return bar;
}

/* "Name  [bar]  size  files", the bar the part of whole */
static void tally_row(GtkWidget* grid, gint row, const gchar* name, const DiskTally* t, guint64 whole) {
    GtkWidget* n = label_new(name, NULL, 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(n, name);
    gtk_grid_attach(GTK_GRID(grid), n, 0, row, 1, 1);
    GtkWidget* bar = share_bar(t->size, whole);
    gchar* pct = disk_format_percent(t->size, whole);
    gtk_widget_set_tooltip_text(bar, pct);
    g_free(pct);
    gtk_grid_attach(GTK_GRID(grid), bar, 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), number_take(g_format_size(t->size), "du-num"), 2, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), number_take(disk_format_files(t->count), "du-muted"), 3, row, 1, 1);
}

static gchar* date_text(gint64 t) {
    GDateTime* dt = t > 0 ? g_date_time_new_from_unix_local(t) : NULL;
    if (dt == NULL) return g_strdup("–");
    gchar* text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return text;
}

static gchar* count_text(guint64 n) {
    return g_strdup_printf("%" G_GUINT64_FORMAT, n);
}

/* path inside the folder analysed (root), without its leading separator */
static const gchar* inside(const gchar* path, const gchar* root) {
    if (!g_str_has_prefix(path, root)) return path;
    const gchar* rel = path + strlen(root);
    while (G_IS_DIR_SEPARATOR(*rel)) rel++;
    return rel;
}

/* The overview: sizes, counts and what was left out */
static void show_overview(DuStats* ds, const DiskStats* st, const gchar* root) {
    GtkWidget* g = section(ds, _("Overview"));
    gint row = 0;
    fact(g, row++, _("Size"), g_format_size(st->size));
    fact(g, row++, _("On disk"), g_format_size(st->disk));
    fact(g, row++, _("Files"), count_text(st->n_files));
    fact(g, row++, _("Folders"), count_text(st->n_dirs));
    if (st->n_files > 0) fact(g, row++, _("Average file"), g_format_size(st->size / st->n_files));
    if (st->n_empty_files > 0) fact(g, row++, _("Empty files"), count_text(st->n_empty_files));
    if (st->n_empty_dirs > 0) fact(g, row++, _("Empty folders"), count_text(st->n_empty_dirs));
    if (st->n_symlinks > 0) fact(g, row++, _("Symbolic links"), count_text(st->n_symlinks));
    if (st->n_hardlinks > 0) {
        /* TRANSLATORS: links to files already counted, left out of the sizes */
        fact(g, row++, _("Hard links left out"), count_text(st->n_hardlinks));
    }
    if (st->n_special > 0) fact(g, row++, _("Devices, pipes, sockets"), count_text(st->n_special));
    if (st->n_mounts > 0) fact(g, row++, _("Other filesystems left out"), count_text(st->n_mounts));
    /* TRANSLATORS: files and folders excluded from the calculation on request */
    if (st->n_excluded > 0) fact(g, row++, _("Excluded"), count_text(st->n_excluded));
    if (st->n_files > 0) {
        fact(g, row++, _("Oldest change"), date_text(st->oldest));
        fact(g, row++, _("Newest change"), date_text(st->newest));
    }
    if (st->deepest != NULL) {
        /* TRANSLATORS: depth of the deepest folder, and its path */
        fact(g, row++, _("Deepest folder"),
             g_strdup_printf(ngettext("%1$u level: %2$s", "%1$u levels: %2$s", st->max_depth), st->max_depth,
                             inside(st->deepest, root)));
    }
}

static void show_kinds(DuStats* ds, const DiskStats* st) {
    DiskTally kinds[DISK_KIND_COUNT];
    disk_stats_kinds(st, kinds);
    /* Largest first */
    gint order[DISK_KIND_COUNT];
    for (gint k = 0; k < DISK_KIND_COUNT; k++) order[k] = k;
    for (gint i = 1; i < DISK_KIND_COUNT; i++) {
        for (gint j = i; j > 0 && kinds[order[j]].size > kinds[order[j - 1]].size; j--) {
            gint t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    }
    GtkWidget* g = section(ds, _("By Kind"));
    gint row = 0;
    for (gint i = 0; i < DISK_KIND_COUNT; i++) {
        const DiskTally* t = &kinds[order[i]];
        if (t->count > 0) tally_row(g, row++, disk_kind_name((DiskKind)order[i]), t, st->size);
    }
}

/* "1k to 10k\n3 files (12.0%)\n25 MB (0.4%)" */
static gchar* group_tip(const gchar* name, const DiskTally* t, const DiskStats* st) {
    gchar* files = disk_format_files(t->count);
    gchar* files_pct = disk_format_percent(t->count, st->n_files);
    gchar* size = g_format_size(t->size);
    gchar* size_pct = disk_format_percent(t->size, st->size);
    gchar* tip = g_strdup_printf("%s\n%s (%s)\n%s (%s)", name, files, files_pct, size, size_pct);
    g_free(size_pct);
    g_free(size);
    g_free(files_pct);
    g_free(files);
    return tip;
}

/* The files by size: a histogram of their number and of their room */
static void show_sizes(DuStats* ds, const DiskStats* st) {
    section_title(ds, _("By Size"));
    HistogramBin bins[DISK_SIZE_COUNT];
    gchar* tips[DISK_SIZE_COUNT];
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) {
        tips[g] = group_tip(disk_size_name((DiskSize)g), &st->sizes[g], st);
        bins[g] = (HistogramBin){ disk_size_label((DiskSize)g), tips[g], st->sizes[g].count, st->sizes[g].size };
    }
    GtkWidget* chart = histogram_new();
    gtk_widget_add_css_class(chart, "du-histogram");
    histogram_set(chart, bins, DISK_SIZE_COUNT);
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) g_free(tips[g]);
    gtk_box_append(GTK_BOX(ds->box), chart);
}

static void show_ages(DuStats* ds, const DiskStats* st) {
    GtkWidget* g = section(ds, _("By Last Change"));
    for (gint a = 0; a < DISK_AGE_COUNT; a++) tally_row(g, a, disk_age_name((DiskAge)a), &st->ages[a], st->size);
}

static void show_extensions(DuStats* ds, const DiskStats* st) {
    GArray* top = disk_stats_top_extensions(st, TOP_EXTENSIONS);
    GtkWidget* g = section(ds, _("By Extension"));
    for (guint i = 0; i < top->len; i++) {
        const DiskExtension* e = &g_array_index(top, DiskExtension, i);
        gchar* name = e->ext[0] != '\0' ? g_strconcat(".", e->ext, NULL) : g_strdup(_("(none)"));
        tally_row(g, (gint)i, name, &e->tally, st->size);
        g_free(name);
    }
    g_array_unref(top);
}

static void on_file_clicked(GtkButton* button, gpointer data) {
    DuStats* ds = data;
    const gchar* path = g_object_get_data(G_OBJECT(button), PATH_KEY);
    if (ds->reveal != NULL && path != NULL) ds->reveal(ds->data, path);
}

/* The largest files: a click shows one in the explorer */
static void show_largest(DuStats* ds, const DiskStats* st, const gchar* root) {
    if (st->largest->len == 0) return;
    GtkWidget* g = section(ds, _("Largest Files"));
    for (guint i = 0; i < st->largest->len && i < TOP_FILES; i++) {
        const DiskBigFile* f = &g_array_index(st->largest, DiskBigFile, i);
        const gchar* rel = inside(f->path, root);
        GtkWidget* button = gtk_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
        gtk_widget_add_css_class(button, "du-file");
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* icon = file_type_image_new();
        gchar* base = g_path_get_basename(f->path);
        file_type_image_set(icon, base, FALSE);
        g_free(base);
        gtk_box_append(GTK_BOX(row), icon);
        GtkWidget* name = label_new(rel, NULL, 0.0f);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_box_append(GTK_BOX(row), name);
        gtk_button_set_child(GTK_BUTTON(button), row);
        /* TRANSLATORS: tooltip of a file of the largest files: its path */
        gchar* tip = g_strdup_printf(_("Show in Explorer: %s"), f->path);
        gtk_widget_set_tooltip_text(button, tip);
        g_free(tip);
        g_object_set_data_full(G_OBJECT(button), PATH_KEY, g_strdup(f->path), g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(on_file_clicked), ds);
        gtk_widget_set_hexpand(button, TRUE);
        gtk_grid_attach(GTK_GRID(g), button, 0, (gint)i, 1, 1);
        gtk_grid_attach(GTK_GRID(g), number_take(g_format_size(f->size), "du-num"), 1, (gint)i, 1, 1);
    }
}

static void show_errors(DuStats* ds, const DiskStats* st) {
    if (st->n_errors == 0) return;
    gchar* n = count_text(st->n_errors);
    /* TRANSLATORS: %s is a number of entries */
    gchar* title = g_strdup_printf(ngettext("Unreadable (%s entry)", "Unreadable (%s entries)",
                                            (gulong)MIN(st->n_errors, G_MAXULONG)),
                                   n);
    GtkWidget* g = section(ds, title);
    g_free(title);
    g_free(n);
    for (guint i = 0; i < st->errors->len && i < ERRORS_SHOWN; i++) {
        const DiskError* e = &g_array_index(st->errors, DiskError, i);
        GtkWidget* path = label_new(e->path, NULL, 0.0f);
        gtk_widget_set_hexpand(path, TRUE);
        gchar* tip = g_strdup_printf("%s\n%s", e->path, e->message);
        gtk_widget_set_tooltip_text(path, tip);
        g_free(tip);
        gtk_grid_attach(GTK_GRID(g), path, 0, (gint)i, 1, 1);
        gtk_grid_attach(GTK_GRID(g), label_new(e->message, "du-muted", 1.0f), 1, (gint)i, 1, 1);
    }
}

void disk_usage_stats_show(GtkWidget* stats, const DiskScan* scan) {
    DuStats* ds = stats != NULL ? g_object_get_data(G_OBJECT(stats), STATS_KEY) : NULL;
    if (ds == NULL) return;
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(ds->box)) != NULL) gtk_box_remove(GTK_BOX(ds->box), child);
    if (scan == NULL) {
        GtkWidget* note = label_new(_("The statistics come once the folder is read"), "du-muted", 0.0f);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(note), PANGO_ELLIPSIZE_NONE);
        gtk_box_append(GTK_BOX(ds->box), note);
        return;
    }
    const DiskStats* st = disk_scan_stats(scan);
    show_overview(ds, st, disk_scan_root_path(scan));
    if (st->n_files > 0) {
        show_kinds(ds, st);
        show_sizes(ds, st);
        show_ages(ds, st);
        show_extensions(ds, st);
        show_largest(ds, st, disk_scan_root_path(scan));
    }
    show_errors(ds, st);
}

GtkWidget* disk_usage_stats_new(void (*reveal)(gpointer data, const gchar* path), gpointer data) {
    DuStats* ds = g_new0(DuStats, 1);
    ds->reveal = reveal;
    ds->data = data;
    ds->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(ds->box, "du-stats");
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ds->box);
    g_object_set_data_full(G_OBJECT(scroll), STATS_KEY, ds, g_free);
    disk_usage_stats_show(scroll, NULL);
    return scroll;
}
