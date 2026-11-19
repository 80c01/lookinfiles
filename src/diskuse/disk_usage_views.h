#ifndef DISK_USAGE_VIEWS_H
#define DISK_USAGE_VIEWS_H

/* The views of the Disk Usage tab under its breakdown, one shown at a
 * time: the treemap (disk_usage_map.h), the wheel (disk_usage_wheel.h)
 * and the histograms (disk_usage_charts.h), all of the folder shown,
 * chosen by three linked buttons above them or in turn with v. */
#include "core/disk_scan.h"

#include <gtk/gtk.h>

typedef struct {
    /* A click on the treemap or the wheel: name of the entry of the folder
     * shown that holds the point */
    void (*selected)(gpointer data, const gchar* name);
    /* A double click on a folder (any depth) */
    void (*open)(gpointer data, DiskNode* folder);
    /* A double click on a file: its full path */
    void (*reveal)(gpointer data, const gchar* path);
    /* A click in the hole of the wheel: the parent folder */
    void (*up)(gpointer data);
    gpointer data;
} DiskUsageViewsHandlers;

GtkWidget* disk_usage_views_new(const DiskUsageViewsHandlers* handlers);

/* Show node in every view (borrowed until the next call); NULL: their notes
 * that they come once the folder is read */
void disk_usage_views_show(GtkWidget* views, DiskNode* node);

/* Outline the entry of the folder shown named name (NULL: none) */
void disk_usage_views_select(GtkWidget* views, const gchar* name);

/* The next view (v), after the last the first */
void disk_usage_views_next(GtkWidget* views);

#endif /* DISK_USAGE_VIEWS_H */
