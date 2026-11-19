#ifndef HISTOGRAM_H
#define HISTOGRAM_H

/* A bar chart of files in a few groups (a GtkDrawingArea): two bars per
 * group, the number of files and the room they take, each series scaled
 * to its largest group (a legend names the two colours), a short label
 * under each group (some left out when they do not fit). The group under
 * the pointer is lit and its tooltip is the one given. With an activate
 * handler, a click on a group or Enter / Space on the group with the focus
 * (Left / Right, Home / End move it) calls it; a group chosen
 * (histogram_set_selected()) is outlined, the others dimmed. Used by the
 * statistics of the Disk Usage tab and the Deep Search tab. */
#include <gtk/gtk.h>

typedef struct {
    const gchar* label; /* under the bars: "1k", "2025"... */
    const gchar* tip;   /* tooltip of the group, NULL: none */
    guint64 count;      /* files */
    guint64 size;       /* bytes */
} HistogramBin;

GtkWidget* histogram_new(void);

/* Show n groups (copied) */
void histogram_set(GtkWidget* chart, const HistogramBin* bins, guint n);

/* A click on a group, or Enter / Space on the one with the focus, calls
 * activate (the chart takes the focus then) */
void histogram_set_activate(GtkWidget* chart, void (*activate)(gpointer data, guint bin), gpointer data);

/* Outline group bin as the one chosen; -1: none */
void histogram_set_selected(GtkWidget* chart, gint bin);

/* Two charts (or boxes holding one) side by side when each can be at least
 * HISTOGRAM_PAIR_MIN_WIDTH wide, else one over the other (when the height
 * holds them both, else still side by side, squeezed); the width (side by
 * side) or the height (one over the other) shared in halves. A chart is
 * 120 px high when there is room, 48 at least. */
#define HISTOGRAM_PAIR_MIN_WIDTH 280
GtkWidget* histogram_pair_new(GtkWidget* first, GtkWidget* second);

/* changed(data, side_by_side) is called when the pair goes from one layout
 * to the other (after the layout, so that it may resize what holds it) */
void histogram_pair_set_changed(GtkWidget* pair, void (*changed)(gpointer data, gboolean side_by_side),
                                gpointer data);

#endif /* HISTOGRAM_H */
