#ifndef GIT_STATS_CHARTS_H
#define GIT_STATS_CHARTS_H

/* The charts of the Statistics view of the Git tab (GtkDrawingAreas in the
 * palette colours, a tooltip on each cell or bar):
 * - the activity of the last 53 weeks, a square per day (a column per week,
 *   Monday on top), darker with more commits;
 * - the punch card: a circle per day of the week and hour, larger with more
 *   commits (in each author's time zone);
 * - the commits per month (per year past six years) from the first commit. */
#include <gtk/gtk.h>
#include "git/git_stats.h"

typedef enum {
    GIT_CHART_CALENDAR,
    GIT_CHART_PUNCH,
    GIT_CHART_MONTHS
} GitChartKind;

GtkWidget* git_stats_chart_new(GitChartKind kind);

/* Draw stats (borrowed: it must live while the chart shows it; NULL: an
 * empty chart) */
void git_stats_chart_set(GtkWidget* chart, const GitStats* stats);

#endif /* GIT_STATS_CHARTS_H */
