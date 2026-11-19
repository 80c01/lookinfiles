#ifndef DISK_USAGE_WHEEL_H
#define DISK_USAGE_WHEEL_H

/* The wheel of the Disk Usage tab (a sunburst), one of the views under its
 * breakdown: the folder shown in the hole at the centre (name and size),
 * what it holds in rings around it, each entry an arc of its share of its
 * folder, a few levels deep (the slimmest arcs left out). An entry of the
 * folder shown and everything below it take one colour, paler at each
 * level; files are grey. The row selected in the breakdown is outlined; a
 * click on an arc selects its row, a double click opens a folder or shows a
 * file in the explorer, a click in the hole goes back to the parent folder;
 * the tooltip names what is under the pointer. */
#include "core/disk_scan.h"

#include <gtk/gtk.h>

typedef struct {
    /* A click: name of the entry of the folder shown that holds the point */
    void (*selected)(gpointer data, const gchar* name);
    /* A double click on a folder (any depth) */
    void (*open)(gpointer data, DiskNode* folder);
    /* A double click on a file: its full path */
    void (*reveal)(gpointer data, const gchar* path);
    /* A click in the hole: the parent folder */
    void (*up)(gpointer data);
    gpointer data;
} DiskUsageWheelHandlers;

GtkWidget* disk_usage_wheel_new(const DiskUsageWheelHandlers* handlers);

/* Draw node (borrowed until the next call); NULL: a note that the wheel
 * comes once the folder is read */
void disk_usage_wheel_show(GtkWidget* wheel, DiskNode* node);

/* Outline the entry of the folder shown named name (NULL: none) */
void disk_usage_wheel_select(GtkWidget* wheel, const gchar* name);

#endif /* DISK_USAGE_WHEEL_H */
