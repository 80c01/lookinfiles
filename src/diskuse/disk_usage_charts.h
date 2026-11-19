#ifndef DISK_USAGE_CHARTS_H
#define DISK_USAGE_CHARTS_H

/* The histograms of the Disk Usage tab, one of the views under its
 * breakdown: the files of the folder shown and of everything below it by
 * size (empty, under 1k... 10G and more) and by age (less than a day...
 * over 3 years), each a histogram of their number and of their room
 * (histogram.h), from the groups the scan keeps for every folder. */
#include "core/disk_scan.h"

#include <gtk/gtk.h>

GtkWidget* disk_usage_charts_new(void);

/* Show the files of node (borrowed until the next call); NULL: a note that
 * the charts come once the folder is read */
void disk_usage_charts_show(GtkWidget* charts, const DiskNode* node);

#endif /* DISK_USAGE_CHARTS_H */
