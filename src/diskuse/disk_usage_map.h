#ifndef DISK_USAGE_MAP_H
#define DISK_USAGE_MAP_H

/* The treemap of the Disk Usage tab, under its breakdown: the folder shown
 * as nested rectangles whose areas are the sizes of what it holds (folders
 * drawn with their name and their own content, a few levels deep; files
 * coloured by kind). The row selected in the breakdown is outlined; a click
 * on a rectangle selects its row, a double click opens a folder or shows a
 * file in the explorer; the tooltip names what is under the pointer. */
#include "core/disk_scan.h"

#include <gtk/gtk.h>

typedef struct {
    /* A click: name of the entry of the folder shown that holds the point */
    void (*selected)(gpointer data, const gchar* name);
    /* A double click on a folder (any depth) */
    void (*open)(gpointer data, DiskNode* folder);
    /* A double click on a file: its full path */
    void (*reveal)(gpointer data, const gchar* path);
    gpointer data;
} DiskUsageMapHandlers;

GtkWidget* disk_usage_map_new(const DiskUsageMapHandlers* handlers);

/* Draw node (borrowed until the next call); NULL: a note that the map comes
 * once the folder is read */
void disk_usage_map_show(GtkWidget* map, DiskNode* node);

/* Outline the entry of the folder shown named name (NULL: none) */
void disk_usage_map_select(GtkWidget* map, const gchar* name);

#endif /* DISK_USAGE_MAP_H */
