#ifndef DISK_USAGE_CHARTS_H
#define DISK_USAGE_CHARTS_H

/* The histograms of the Disk Usage tab, one of the views under its
 * breakdown: the files of the folder shown and of everything below it by
 * size (empty, under 1k... 10G and more) and by age (less than a day...
 * over 3 years), each a histogram of their number and of their room
 * (histogram.h), from the groups the scan keeps for every folder. A click
 * on a group (Enter on the one with the focus) asks for a Deep Search of
 * its files below the folder. */
#include "core/app_types.h"
#include "core/disk_scan.h"

#include <gtk/gtk.h>

/* search(data, request): a group chosen, request (folder, sizes or dates)
 * borrowed during the call; NULL: the groups cannot be chosen */
typedef void (*DiskUsageSearch)(gpointer data, DeepSearchRequest* request);

GtkWidget* disk_usage_charts_new(DiskUsageSearch search, gpointer data);

/* Show the files of node (borrowed until the next call); NULL: a note that
 * the charts come once the folder is read. With searchable FALSE (a tree
 * read with a git filter, which the Deep Search cannot repeat), a click on
 * a group does nothing. */
void disk_usage_charts_show(GtkWidget* charts, const DiskNode* node, gboolean searchable);

/* The tooltip of a group of files that can be listed in the Deep Search
 * tab: tip and the hint of the click (a new string) */
gchar* disk_usage_charts_search_tip(const gchar* tip);

#endif /* DISK_USAGE_CHARTS_H */
