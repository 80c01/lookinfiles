#ifndef DISK_USAGE_LIST_H
#define DISK_USAGE_LIST_H

/* The breakdown of the Disk Usage tab: a GtkColumnView of the folders and
 * files of one folder (name, share with a bar, size, files, last change),
 * largest first, sorted by a click on a column header. The largest files
 * of the folder are listed, the others summed up in one row. While the
 * scan runs it shows the folders of the root done so far. Read only. */
#include "core/app_types.h"
#include "core/disk_scan.h"

typedef struct {
    /* Enter, a double click, Right (Vim l, Emacs C-f) on a row: folder is
     * the folder's node when the whole tree is read, else NULL; path is
     * the row's full path (NULL for the row of the other files) */
    void (*activate)(gpointer data, DiskNode* folder, const gchar* path);
    /* Backspace, Alt+Up, Left (Vim h, Emacs C-b): the parent folder */
    void (*up)(gpointer data);
    /* Show in the explorer (context menu) */
    void (*reveal)(gpointer data, const gchar* path);
    gpointer data;
} DiskUsageListHandlers;

/* The list in its scrolled window; the navigation keys of the global mode
 * move in it (focus_route_attach_list()) */
GtkWidget* disk_usage_list_new(AppData* app, const DiskUsageListHandlers* handlers);
/* The column view inside, to focus */
GtkWidget* disk_usage_list_view(GtkWidget* list);

/* Show the folders and files of node (the whole tree is read; the nodes are
 * borrowed until the next call); select is the name of the row to select,
 * NULL for the first */
void disk_usage_list_show_node(GtkWidget* list, DiskNode* node, const gchar* select);
/* While the scan runs: the folders of the root done so far and its files */
void disk_usage_list_show_progress(GtkWidget* list, const gchar* root, const DiskScanSnapshot* snap);
/* No rows */
void disk_usage_list_clear(GtkWidget* list);

/* Name of the selected row, NULL if none (owned) */
gchar* disk_usage_list_selected_name(GtkWidget* list);

#endif /* DISK_USAGE_LIST_H */
