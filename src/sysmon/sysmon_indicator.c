/* The CPU chart of the top bar (see sysmon_indicator.h) */
#include "sysmon/sysmon_indicator.h"
#include "sysmon/sysmon_tab.h"
#include "core/i18n.h"
#include "core/sysstat.h"
#include "theme/theme.h"
#include "widgets/sparkline.h"

#define INDICATOR_KEY "lookin-sysmon-indicator"
#define PERIOD_SECONDS 2
#define HISTORY 30 /* one minute */

typedef struct {
    GtkWidget* chart;
    GtkWidget* value;
    GtkWidget* button;
    SysCpuTimes prev;
    gboolean have_prev;
    guint timer;
} Indicator;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".sysmon-indicator .sysmon-indicator-value { font-size: @size_small;"
                  "  font-feature-settings: \"tnum\"; }"
                  ".sysmon-indicator .sparkline { background-color: @bg_input; border-radius: 3px; }");
}

static gchar* read_proc(const gchar* name) {
    gchar* path = g_build_filename("/proc", name, NULL);
    gchar* text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL)) text = NULL;
    g_free(path);
    return text;
}

/* Tiny procfs files: read on the main loop */
static gboolean on_tick(gpointer data) {
    Indicator* ind = data;
    gchar* text = read_proc("stat");
    GArray* cpu = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    gboolean ok = sysstat_parse_cpu(text, cpu);
    g_free(text);
    if (!ok) {
        g_array_unref(cpu);
        return G_SOURCE_CONTINUE;
    }
    SysCpuTimes cur = g_array_index(cpu, SysCpuTimes, 0);
    guint ncpu = cpu->len > 1 ? cpu->len - 1 : 1;
    g_array_unref(cpu);
    if (!ind->have_prev) {
        ind->prev = cur;
        ind->have_prev = TRUE;
        return G_SOURCE_CONTINUE;
    }
    gdouble percent = sysstat_cpu_percent(&ind->prev, &cur);
    ind->prev = cur;
    sparkline_push(ind->chart, 0, percent);
    gchar* label = g_strdup_printf("%.0f%%", percent);
    gtk_label_set_text(GTK_LABEL(ind->value), label);
    g_free(label);

    gdouble load[3] = { 0, 0, 0 };
    text = read_proc("loadavg");
    sysstat_parse_loadavg(text, load, NULL, NULL);
    g_free(text);
    SysMemInfo mem;
    text = read_proc("meminfo");
    gboolean have_mem = sysstat_parse_meminfo(text, &mem);
    g_free(text);
    gchar* used = g_format_size(have_mem ? sysstat_mem_used(&mem) : 0);
    gchar* total = g_format_size(have_mem ? mem.total : 0);
    gchar* tip = g_strdup_printf(_("CPU %.0f%% (%u CPUs) · Load %.2f %.2f %.2f · Memory %s of %s\n"
                                   "Click: System Monitor"),
                                 percent, ncpu, load[0], load[1], load[2], used, total);
    gtk_widget_set_tooltip_text(ind->button, tip);
    g_free(tip);
    g_free(used);
    g_free(total);
    return G_SOURCE_CONTINUE;
}

static void indicator_free(gpointer data) {
    Indicator* ind = data;
    if (ind->timer != 0) g_source_remove(ind->timer);
    g_free(ind);
}

static void on_clicked(GtkButton* button, gpointer data) {
    (void)button;
    AppData* app = data;
    if (!sysmon_tab_close_shown(app)) sysmon_tab_show(app);
}

GtkWidget* sysmon_indicator_new(AppData* app) {
    ensure_css();
    Indicator* ind = g_new0(Indicator, 1);
    ind->chart = sparkline_new(HISTORY);
    sparkline_add_series(ind->chart, PAL_ACCENT_FG);
    sparkline_set_max(ind->chart, 100.0);
    gtk_widget_set_size_request(ind->chart, 56, 16);
    gtk_widget_set_valign(ind->chart, GTK_ALIGN_CENTER);
    ind->value = gtk_label_new("–");
    gtk_widget_add_css_class(ind->value, "sysmon-indicator-value");
    gtk_label_set_xalign(GTK_LABEL(ind->value), 1.0f);
    gtk_label_set_width_chars(GTK_LABEL(ind->value), 4); /* "100%": the bar does not move */

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(box), ind->chart);
    gtk_box_append(GTK_BOX(box), ind->value);
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), box);
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_focusable(btn, FALSE);
    gtk_widget_add_css_class(btn, "status-pill");
    gtk_widget_add_css_class(btn, "sysmon-indicator");
    gtk_widget_set_tooltip_text(btn, _("System Monitor: CPU, memory, disks, network and processes"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_clicked), app);
    ind->button = btn;
    g_object_set_data_full(G_OBJECT(btn), INDICATOR_KEY, ind, indicator_free);

    gtk_widget_set_visible(btn, sysstat_available(NULL));
    if (sysstat_available(NULL) && g_getenv("LOOKIN_SYSMON_FROZEN") == NULL) {
        on_tick(ind);
        ind->timer = g_timeout_add_seconds(PERIOD_SECONDS, on_tick, ind);
    }
    return btn;
}

void sysmon_indicator_sync(GtkWidget* indicator, AppData* app) {
    if (indicator == NULL) return;
    if (sysmon_tab_is_open(app)) {
        gtk_widget_add_css_class(indicator, "pill-active");
    } else {
        gtk_widget_remove_css_class(indicator, "pill-active");
    }
}
