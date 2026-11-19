#ifndef HISTOGRAM_H
#define HISTOGRAM_H

/* A bar chart of files in a few groups (a GtkDrawingArea): two bars per
 * group, the number of files and the room they take, each series scaled
 * to its largest group (a legend names the two colours), a short label
 * under each group. The group under the pointer is lit and its tooltip is
 * the one given. Used by the statistics of the Disk Usage tab. */
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

#endif /* HISTOGRAM_H */
