#ifndef DEEP_SEARCH_CHARTS_H
#define DEEP_SEARCH_CHARTS_H

/* The charts of the Deep Search tab, under its results: the files found
 * by size (the groups of the Disk Usage tab: empty, under 1k... 10G and
 * more) and by day of last change (days, months or years, the finest that
 * fits; the oldest years merged in one), each a histogram of their number
 * and of their room (histogram.h), filled while the search runs. A click
 * on a bar (or Enter on it) shows only its files in the list, a second
 * one shows them all again, as does the Show All button; a size and a
 * date chosen together both apply. Folders are not counted. The two charts
 * are side by side in a wide panel, else one over the other. */
#include "search/deep_search.h"
#include "search/deep_search_list.h"

#include <gtk/gtk.h>

/* changed(data, range): the bars chosen changed; range NULL: none */
GtkWidget* deep_search_charts_new(void (*changed)(gpointer data, const DeepSearchRange* range), gpointer data);

/* No file counted, no bar chosen (changed is not called) */
void deep_search_charts_reset(GtkWidget* charts);

/* Count hits (DeepHit*, borrowed) */
void deep_search_charts_add(GtkWidget* charts, const GPtrArray* hits);

/* Redraw with what was counted since the last time */
void deep_search_charts_refresh(GtkWidget* charts);

/* changed(data, side_by_side): the two charts went side by side or one
 * over the other (the panel width crossed the threshold), so that what holds
 * them may give them their height again */
void deep_search_charts_on_layout(GtkWidget* charts, void (*changed)(gpointer data, gboolean side_by_side),
                                  gpointer data);

#endif /* DEEP_SEARCH_CHARTS_H */
