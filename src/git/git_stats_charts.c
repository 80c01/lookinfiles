/* The charts of the Statistics view of the Git tab (see git_stats_charts.h) */
#include "git/git_stats_charts.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include <math.h>
#include <stdlib.h>

#define CHART_KEY "lookin-git-chart"
#define WEEKS 53
#define TOP_H 18.0      /* row of the month / hour labels */
#define CAL_MAX_CELL 16.0
#define CAL_MIN_CELL 6.0
#define PUNCH_ROW 22.0
#define MONTHS_H 110.0  /* height of the bars */
#define MONTHS_LABEL_H 18.0
/* Past this many months, one bar per year */
#define MONTHS_MAX 72
/* Widest slot of a bar */
#define MONTHS_MAX_SLOT 28.0

typedef struct {
    gchar* label; /* under the bar, NULL: none */
    gchar* tip;
    guint count;
} Bar;

typedef struct {
    GitChartKind kind;
    GtkWidget* area;
    const GitStats* stats; /* borrowed */
    GArray* bars;          /* GIT_CHART_MONTHS: Bar */
    gint today;            /* day number of today, here */
} Chart;

static void bar_clear(gpointer data) {
    Bar* b = data;
    g_free(b->label);
    g_free(b->tip);
}

static void chart_free(gpointer data) {
    Chart* c = data;
    g_array_unref(c->bars);
    g_free(c);
}

static void set_color(cairo_t* cr, PaletteRole role, gdouble alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

static gint text_width(PangoLayout* layout, const gchar* text) {
    pango_layout_set_text(layout, text, -1);
    gint w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    return w;
}

/* text with its left edge at x (or centred on x), its top at y */
static void draw_text(cairo_t* cr, PangoLayout* layout, const gchar* text, gdouble x, gdouble y, gboolean centred) {
    gint w = text_width(layout, text);
    set_color(cr, PAL_FG_SUBTLE, 1.0);
    cairo_move_to(cr, centred ? x - w / 2.0 : x, y);
    pango_cairo_show_layout(cr, layout);
}

/* The date of a day number, formatted (strftime format) */
static gchar* day_text(gint day, const gchar* format) {
    GDateTime* t = g_date_time_new_from_unix_utc((gint64)day * 86400);
    gchar* text = t != NULL ? g_date_time_format(t, format) : NULL;
    if (t != NULL) g_date_time_unref(t);
    return text != NULL ? text : g_strdup("");
}

/* Short name of day of the week w (0: Monday) in the current language */
static gchar* weekday_name(gint w, gboolean full) {
    return day_text(4 + w, full ? "%A" : "%a"); /* day 4, 1970-01-05, was a Monday */
}

static gint today_here(void) {
    GDateTime* now = g_date_time_new_now_local();
    gint offset = (gint)(g_date_time_get_utc_offset(now) / G_TIME_SPAN_MINUTE);
    gint day = git_stats_day_of(g_date_time_to_unix(now), offset);
    g_date_time_unref(now);
    return day;
}

/* --- Activity of the last weeks */

/* Width of the weekday labels on the left */
static gdouble label_width(PangoLayout* layout) {
    gint widest = 0;
    for (gint w = 0; w < 7; w++) {
        gchar* name = weekday_name(w, FALSE);
        widest = MAX(widest, text_width(layout, name));
        g_free(name);
    }
    return widest + 8.0;
}

static gdouble calendar_cell(gdouble width, gdouble left) {
    return CLAMP(floor((width - left) / WEEKS), CAL_MIN_CELL, CAL_MAX_CELL);
}

/* The day number of the first square (the Monday 52 weeks before this week's) */
static gint calendar_start(const Chart* c) {
    return c->today - git_stats_weekday(c->today) - (WEEKS - 1) * 7;
}

static guint max_day(const Chart* c) {
    guint top = 0;
    gint start = calendar_start(c);
    for (gint d = start; d <= c->today; d++) top = MAX(top, git_stats_on_day(c->stats, d));
    return top;
}

/* Darker with more commits: four steps of the accent over the empty colour */
static void day_color(cairo_t* cr, guint n, guint top) {
    if (n == 0 || top == 0) {
        set_color(cr, PAL_BG_INPUT, 1.0);
        return;
    }
    gdouble f = (gdouble)n / (gdouble)top;
    gdouble alpha = f <= 0.25 ? 0.35 : f <= 0.5 ? 0.55 : f <= 0.75 ? 0.78 : 1.0;
    set_color(cr, PAL_ACCENT, alpha);
}

static void draw_calendar(Chart* c, cairo_t* cr, gint width, PangoLayout* layout) {
    gdouble left = label_width(layout);
    gdouble cell = calendar_cell(width, left);
    gint start = calendar_start(c);
    guint top = c->stats != NULL ? max_day(c) : 0;
    gint last_label = -10;
    for (gint col = 0; col < WEEKS; col++) {
        gint first = start + col * 7;
        gchar* dom = day_text(first, "%d");
        if (atoi(dom) <= 7 && col - last_label >= 3) { /* a month starts in this week */
            gchar* month = day_text(first, "%b");
            draw_text(cr, layout, month, left + col * cell, 0.0, FALSE);
            g_free(month);
            last_label = col;
        }
        g_free(dom);
        for (gint row = 0; row < 7; row++) {
            gint day = first + row;
            if (day > c->today) break;
            day_color(cr, c->stats != NULL ? git_stats_on_day(c->stats, day) : 0, top);
            cairo_rectangle(cr, left + col * cell, TOP_H + row * cell, cell - 2.0, cell - 2.0);
            cairo_fill(cr);
        }
    }
    for (gint row = 0; row < 7; row += 2) { /* Monday, Wednesday, Friday, Sunday */
        gchar* name = weekday_name(row, FALSE);
        draw_text(cr, layout, name, 0.0, TOP_H + row * cell + (cell - 2.0) / 2.0 - 7.0, FALSE);
        g_free(name);
    }
}

static gchar* calendar_tip(Chart* c, gint width, PangoLayout* layout, gint x, gint y) {
    gdouble left = label_width(layout);
    gdouble cell = calendar_cell(width, left);
    if (x < left || y < TOP_H) return NULL;
    gint col = (gint)((x - left) / cell);
    gint row = (gint)((y - TOP_H) / cell);
    if (col >= WEEKS || row >= 7) return NULL;
    gint day = calendar_start(c) + col * 7 + row;
    if (day > c->today) return NULL;
    guint n = c->stats != NULL ? git_stats_on_day(c->stats, day) : 0;
    gchar* date = day_text(day, "%a %Y-%m-%d");
    /* TRANSLATORS: tooltip of a day of the activity chart: commits, date */
    gchar* tip = g_strdup_printf(ngettext("%u commit on %s", "%u commits on %s", n), n, date);
    g_free(date);
    return tip;
}

/* --- Punch card */

static void draw_punch(Chart* c, cairo_t* cr, gint width, PangoLayout* layout) {
    gdouble left = label_width(layout);
    gdouble col = MAX(4.0, (width - left) / 24.0);
    guint top = 0;
    for (gint w = 0; w < 7 && c->stats != NULL; w++) {
        for (gint h = 0; h < 24; h++) top = MAX(top, c->stats->punch[w][h]);
    }
    for (gint h = 0; h < 24; h += 3) {
        gchar* hour = g_strdup_printf("%02d", h);
        draw_text(cr, layout, hour, left + h * col + col / 2.0, 0.0, TRUE);
        g_free(hour);
    }
    gdouble radius_max = MIN(col, PUNCH_ROW) / 2.0 - 1.0;
    for (gint w = 0; w < 7; w++) {
        gchar* name = weekday_name(w, FALSE);
        draw_text(cr, layout, name, 0.0, TOP_H + w * PUNCH_ROW + PUNCH_ROW / 2.0 - 8.0, FALSE);
        g_free(name);
        for (gint h = 0; h < 24; h++) {
            guint n = c->stats != NULL ? c->stats->punch[w][h] : 0;
            gdouble cx = left + h * col + col / 2.0;
            gdouble cy = TOP_H + w * PUNCH_ROW + PUNCH_ROW / 2.0;
            if (n == 0 || top == 0) {
                set_color(cr, PAL_BG_INPUT, 1.0);
                cairo_arc(cr, cx, cy, 1.5, 0, 2 * G_PI);
            } else {
                set_color(cr, PAL_ACCENT, 0.9);
                cairo_arc(cr, cx, cy, MAX(2.0, radius_max * sqrt((gdouble)n / (gdouble)top)), 0, 2 * G_PI);
            }
            cairo_fill(cr);
        }
    }
}

static gchar* punch_tip(Chart* c, gint width, PangoLayout* layout, gint x, gint y) {
    gdouble left = label_width(layout);
    gdouble col = MAX(4.0, (width - left) / 24.0);
    if (x < left || y < TOP_H || c->stats == NULL) return NULL;
    gint h = (gint)((x - left) / col);
    gint w = (gint)((y - TOP_H) / PUNCH_ROW);
    if (h >= 24 || w >= 7) return NULL;
    guint n = c->stats->punch[w][h];
    gchar* name = weekday_name(w, TRUE);
    /* TRANSLATORS: tooltip of the punch card: commits, day of the week, hours */
    gchar* tip = g_strdup_printf(ngettext("%u commit on %s, %02d:00–%02d:00", "%u commits on %s, %02d:00–%02d:00", n),
                                 n, name, h, h + 1);
    g_free(name);
    return tip;
}

/* --- Commits per month (or year) */

static void fill_bars(Chart* c) {
    g_array_set_size(c->bars, 0);
    if (c->stats == NULL || c->stats->months->len == 0) return;
    const GitStats* s = c->stats;
    gboolean by_year = s->months->len > MONTHS_MAX;
    for (guint i = 0; i < s->months->len; i++) {
        gint month = s->first_month + (gint)i;
        gint year = month / 12;
        guint n = g_array_index(s->months, guint, i);
        if (by_year) {
            if (c->bars->len == 0 || i == 0 || month % 12 == 0) {
                Bar b = { g_strdup_printf("%d", year), NULL, 0 };
                g_array_append_val(c->bars, b);
            }
            g_array_index(c->bars, Bar, c->bars->len - 1).count += n;
            continue;
        }
        Bar b = { NULL, NULL, n };
        if (i == 0 || month % 12 == 0) b.label = g_strdup_printf("%d", year);
        gchar* when = g_strdup_printf("%d-%02d", year, month % 12 + 1);
        /* TRANSLATORS: tooltip of a bar of commits: commits, month ("2026-11") or year */
        b.tip = g_strdup_printf(ngettext("%u commit in %s", "%u commits in %s", n), n, when);
        g_free(when);
        g_array_append_val(c->bars, b);
    }
    for (guint i = 0; by_year && i < c->bars->len; i++) {
        Bar* b = &g_array_index(c->bars, Bar, i);
        b->tip = g_strdup_printf(ngettext("%u commit in %s", "%u commits in %s", b->count), b->count, b->label);
    }
}

static void draw_months(Chart* c, cairo_t* cr, gint width, PangoLayout* layout) {
    guint n = c->bars->len;
    if (n == 0) return;
    guint top = 0;
    for (guint i = 0; i < n; i++) top = MAX(top, g_array_index(c->bars, Bar, i).count);
    gdouble slot = MIN((gdouble)width / n, MONTHS_MAX_SLOT); /* a few months: narrow bars on the left */
    gdouble gap = slot > 6.0 ? 1.0 : 0.0;
    gdouble last_label = -1000.0;
    set_color(cr, PAL_BORDER, 1.0);
    cairo_rectangle(cr, 0, MONTHS_H, width, 1.0);
    cairo_fill(cr);
    for (guint i = 0; i < n; i++) {
        const Bar* b = &g_array_index(c->bars, Bar, i);
        gdouble h = b->count == 0 || top == 0 ? 0.0 : MAX(1.0, (MONTHS_H - 4.0) * b->count / top);
        set_color(cr, PAL_ACCENT, 0.85);
        cairo_rectangle(cr, i * slot + gap, MONTHS_H - h, MAX(1.0, slot - 2 * gap), h);
        cairo_fill(cr);
        if (b->label != NULL && i * slot - last_label >= text_width(layout, b->label) + 8.0) {
            draw_text(cr, layout, b->label, i * slot, MONTHS_H + 2.0, FALSE);
            last_label = i * slot;
        }
    }
}

static gchar* months_tip(Chart* c, gint width, gint x, gint y) {
    if (c->bars->len == 0 || y > MONTHS_H) return NULL;
    guint i = (guint)(x / MIN((gdouble)width / c->bars->len, MONTHS_MAX_SLOT));
    if (i >= c->bars->len) return NULL;
    return g_strdup(g_array_index(c->bars, Bar, i).tip);
}

/* --- The drawing area */

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    (void)height;
    Chart* c = data;
    PangoLayout* layout = gtk_widget_create_pango_layout(c->area, NULL);
    switch (c->kind) {
    case GIT_CHART_CALENDAR: draw_calendar(c, cr, width, layout); break;
    case GIT_CHART_PUNCH: draw_punch(c, cr, width, layout); break;
    case GIT_CHART_MONTHS: draw_months(c, cr, width, layout); break;
    }
    g_object_unref(layout);
}

static gboolean on_query_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard, GtkTooltip* tooltip,
                                 gpointer data) {
    Chart* c = data;
    if (keyboard) return FALSE;
    gint width = gtk_widget_get_width(widget);
    PangoLayout* layout = gtk_widget_create_pango_layout(widget, NULL);
    gchar* tip = c->kind == GIT_CHART_CALENDAR ? calendar_tip(c, width, layout, x, y)
                 : c->kind == GIT_CHART_PUNCH  ? punch_tip(c, width, layout, x, y)
                                               : months_tip(c, width, x, y);
    g_object_unref(layout);
    if (tip == NULL) return FALSE;
    gtk_tooltip_set_text(tooltip, tip);
    g_free(tip);
    return TRUE;
}

GtkWidget* git_stats_chart_new(GitChartKind kind) {
    Chart* c = g_new0(Chart, 1);
    c->kind = kind;
    c->bars = g_array_new(FALSE, TRUE, sizeof(Bar));
    g_array_set_clear_func(c->bars, bar_clear);
    c->today = today_here();
    c->area = gtk_drawing_area_new();
    gtk_widget_add_css_class(c->area, "git-chart");
    gtk_widget_set_hexpand(c->area, TRUE);
    gint height = kind == GIT_CHART_CALENDAR ? (gint)(TOP_H + 7 * CAL_MAX_CELL)
                  : kind == GIT_CHART_PUNCH  ? (gint)(TOP_H + 7 * PUNCH_ROW)
                                             : (gint)(MONTHS_H + MONTHS_LABEL_H);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(c->area), height);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->area), on_draw, c, NULL);
    gtk_widget_set_has_tooltip(c->area, TRUE);
    g_signal_connect(c->area, "query-tooltip", G_CALLBACK(on_query_tooltip), c);
    g_object_set_data_full(G_OBJECT(c->area), CHART_KEY, c, chart_free);
    return c->area;
}

void git_stats_chart_set(GtkWidget* chart, const GitStats* stats) {
    Chart* c = chart != NULL ? g_object_get_data(G_OBJECT(chart), CHART_KEY) : NULL;
    if (c == NULL) return;
    c->stats = stats;
    c->today = today_here();
    if (c->kind == GIT_CHART_MONTHS) fill_bars(c);
    gtk_widget_queue_draw(c->area);
}
