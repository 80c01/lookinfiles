#ifndef DISK_USAGE_STATS_H
#define DISK_USAGE_STATS_H

/* The statistics of the Disk Usage tab, beside its breakdown: an overview
 * (sizes, counts, links, empty entries, deepest folder, oldest and newest
 * change), the room by kind of file, by age and by extension, each with a
 * bar, the largest files of the whole folder (a click shows one in the
 * explorer) and the entries that could not be read. Read only. */
#include "core/app_types.h"
#include "core/disk_scan.h"

/* The panel in its scrolled window; reveal(data, path): a largest file
 * clicked */
GtkWidget* disk_usage_stats_new(void (*reveal)(gpointer data, const gchar* path), gpointer data);

/* Show the statistics of a scan that is over (cancelled or not); NULL: a
 * note that they come once the folder is read */
void disk_usage_stats_show(GtkWidget* stats, const DiskScan* scan);

#endif /* DISK_USAGE_STATS_H */
