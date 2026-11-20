#ifndef DISK_USAGE_STATS_H
#define DISK_USAGE_STATS_H

/* The statistics of the Disk Usage tab, beside its breakdown: an overview
 * (sizes, counts, links, empty entries, deepest folder, oldest and newest
 * change), the room by kind of file, by age and by extension, each with a
 * bar, the files by size (a histogram of their number and room), the largest files of the whole folder (a click shows one in the
 * explorer) and the entries that could not be read; in a git work tree,
 * the room of its tracked, untracked and ignored files, its history and
 * its nested repositories (a click on tracked, untracked or ignored counts
 * those files only), the largest ignored entries and tracked files. A
 * group of files (a size, an age, an extension) clicked is listed in the
 * Deep Search tab, except for a tree read with a git filter. */
#include "diskuse/disk_usage_charts.h"
#include "core/app_types.h"
#include "core/disk_scan.h"
#include "git/git_filter.h"
#include "git/git_space.h"

/* The panel in its scrolled window; reveal(data, path): a largest file
 * clicked; filter(data, mode): count the files of mode only; search: a
 * group of files clicked (disk_usage_charts.h) */
GtkWidget* disk_usage_stats_new(void (*reveal)(gpointer data, const gchar* path),
                                void (*filter)(gpointer data, GitFilterMode mode), DiskUsageSearch search,
                                gpointer data);

/* Show the statistics of a scan that is over (cancelled or not), with the
 * room of its work tree (git, NULL outside one or for a tree read with a
 * filter: mode); scan NULL: a note that they come once the folder is read */
void disk_usage_stats_show(GtkWidget* stats, const DiskScan* scan, const GitSpace* git, GitFilterMode mode);

#endif /* DISK_USAGE_STATS_H */
