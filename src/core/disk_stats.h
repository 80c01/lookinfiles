#ifndef DISK_STATS_H
#define DISK_STATS_H

/* Statistics of a disk usage scan (disk_scan.h) grouped for display: file
 * kinds from the extensions, the largest extensions, the names of the age
 * groups, percentages. Pure (no I/O), no widgets. */
#include "core/disk_scan.h"

/* Broad kinds of files */
typedef enum {
    DISK_KIND_DOCUMENTS = 0,
    DISK_KIND_IMAGES,
    DISK_KIND_VIDEOS,
    DISK_KIND_AUDIO,
    DISK_KIND_ARCHIVES,
    DISK_KIND_CODE,      /* text, source code, data formats */
    DISK_KIND_PROGRAMS,  /* executables, libraries, bytecode */
    DISK_KIND_OTHER,     /* no extension or an unknown one */
    DISK_KIND_COUNT
} DiskKind;

/* Kind of the files with this lower-case extension ("" for none) */
DiskKind disk_kind_of_extension(const gchar* ext);

/* Translated name of a kind: "Documents", "Images"... */
const gchar* disk_kind_name(DiskKind kind);

/* Size and count per kind, summed from stats->extensions */
void disk_stats_kinds(const DiskStats* stats, DiskTally out[DISK_KIND_COUNT]);

/* An extension and its files */
typedef struct {
    const gchar* ext;  /* borrowed from the stats */
    DiskTally tally;
} DiskExtension;

/* The n extensions taking the most room, largest first (DiskExtension;
 * "" for the files without extension). Ties by name. */
GArray* disk_stats_top_extensions(const DiskStats* stats, guint n);

/* Translated name of an age group: "Less than a day", "Over 3 years"... */
const gchar* disk_age_name(DiskAge age);
/* Short label of an age group, under its bar: "< 1 d", "< 1 w"... "3 y +" */
const gchar* disk_age_label(DiskAge age);

/* Translated name of a size group: "Empty", "Under 1k", "1k to 10k"...
 * "10G and more" (k, M, G: powers of 1024, as the Deep Search criteria) */
const gchar* disk_size_name(DiskSize size);
/* Short label of a size group, under its bar: "0", "<1k", "1k"... "10G+" */
const gchar* disk_size_label(DiskSize size);

/* part of whole as a percentage, 0 when whole is 0 */
gdouble disk_percent(guint64 part, guint64 whole);

/* "12.3%", "< 0.1%" for a tiny non-zero part, "0.0%" */
gchar* disk_format_percent(guint64 part, guint64 whole);

/* "1 file", "12340 files" */
gchar* disk_format_files(guint64 n);

/* Tooltip of a group of a histogram: "1k to 10k\n3 files (12.0%)\n25 MB
 * (0.4%)", the percentages of all_count files and all_size bytes */
gchar* disk_format_group(const gchar* name, const DiskTally* t, guint64 all_count, guint64 all_size);

/* "2.1 GB (12340 files)" */
gchar* disk_format_tally(const DiskTally* t);

#endif /* DISK_STATS_H */
