#ifndef DISK_SCAN_H
#define DISK_SCAN_H

/* Disk usage of a folder: the whole tree below it is read in a worker
 * thread (disk_scan_run()), into a tree of its folders (DiskNode: sizes
 * rolled up, the largest files of each folder) and statistics of the whole
 * scan (DiskStats). Symbolic links are never followed, hard links are
 * counted once, and other mounted filesystems are skipped unless asked.
 * While it runs, any thread can take a snapshot of the progress
 * (disk_scan_snapshot()). No widgets, no AppData. */
#include <gio/gio.h>

/* Largest files kept per folder (the others are summed up) */
#define DISK_SCAN_DIR_FILES 20
/* Largest files kept for the whole scan */
#define DISK_SCAN_TOP_FILES 50
/* Unreadable entries whose path is kept */
#define DISK_SCAN_MAX_ERRORS 100

typedef struct {
    gchar* name;
    guint64 size;  /* apparent size in bytes */
    guint64 disk;  /* bytes allocated on disk */
    gint64 mtime;  /* seconds since the epoch */
} DiskFile;

/* Size and number of files of a group (an age, an extension, a kind) */
typedef struct {
    guint64 size;
    guint64 count;
} DiskTally;

/* Age of a file, by its last modification */
typedef enum {
    DISK_AGE_DAY = 0,  /* less than a day */
    DISK_AGE_WEEK,
    DISK_AGE_MONTH,
    DISK_AGE_YEAR,
    DISK_AGE_3_YEARS,
    DISK_AGE_OLDER,
    DISK_AGE_COUNT
} DiskAge;

/* Size of a file, by decades of powers of 1024 (the units of the size
 * criteria of the Deep Search tab: 1k = 1024 bytes) */
typedef enum {
    DISK_SIZE_EMPTY = 0,  /* 0 bytes */
    DISK_SIZE_TINY,       /* under 1k */
    DISK_SIZE_1K,         /* 1k to under 10k */
    DISK_SIZE_10K,
    DISK_SIZE_100K,
    DISK_SIZE_1M,
    DISK_SIZE_10M,
    DISK_SIZE_100M,
    DISK_SIZE_1G,
    DISK_SIZE_HUGE,       /* 10G and more */
    DISK_SIZE_COUNT
} DiskSize;

typedef struct DiskNode DiskNode;
struct DiskNode {
    gchar* name;          /* entry name; the root: its full path */
    DiskNode* parent;     /* NULL for the root */
    GPtrArray* children;  /* DiskNode*, largest first (NULL when it has no folder) */
    GArray* files;        /* DiskFile, the largest files directly inside, largest
                             first (DISK_SCAN_DIR_FILES at most; NULL if none) */
    guint64 size;         /* apparent bytes of the whole subtree */
    guint64 disk;         /* bytes allocated on disk, whole subtree */
    guint64 files_size;   /* apparent bytes of the files directly inside */
    guint64 files_count;  /* files directly inside */
    guint64 n_files;      /* files of the whole subtree */
    guint64 n_dirs;       /* folders of the whole subtree, itself not counted */
    gint64 mtime;         /* newest file modification of the subtree, 0 if none */
    gboolean unreadable;  /* could not be listed (its size is unknown) */
    DiskTally sizes[DISK_SIZE_COUNT]; /* files of the whole subtree by size group */
    DiskTally ages[DISK_AGE_COUNT];   /* and by age */
};

typedef struct {
    gchar* path;   /* full path */
    guint64 size;
    gint64 mtime;
} DiskBigFile;

typedef struct {
    gchar* path;
    gchar* message;
} DiskError;

typedef struct {
    guint64 size;          /* apparent bytes */
    guint64 disk;          /* bytes allocated on disk */
    guint64 n_files;       /* regular files (a hard-linked one once) */
    guint64 n_dirs;        /* folders below the root */
    guint64 n_symlinks;    /* symbolic links (not followed, their own size counted) */
    guint64 n_special;     /* devices, pipes, sockets */
    guint64 n_hardlinks;   /* more links to a file already counted (left out) */
    guint64 n_empty_files;
    guint64 n_empty_dirs;
    guint64 n_mounts;      /* other filesystems mounted below (skipped) */
    guint64 n_excluded;    /* entries left out on request (disk_scan_exclude(),
                              disk_scan_remove()) */
    guint64 n_errors;      /* entries that could not be read */
    GArray* errors;        /* DiskError, the first DISK_SCAN_MAX_ERRORS */
    GArray* largest;       /* DiskBigFile, largest first, DISK_SCAN_TOP_FILES at most */
    GHashTable* extensions;/* lower-case extension ("" for none) -> DiskTally* */
    DiskTally ages[DISK_AGE_COUNT];
    DiskTally sizes[DISK_SIZE_COUNT];
    gint64 oldest;         /* oldest / newest file modification, 0 if no file */
    gint64 newest;
    guint max_depth;       /* depth of the deepest folder (1: directly in the root) */
    gchar* deepest;        /* its path, NULL if the root has no folder */
    gint64 now;            /* time of the scan (seconds), the reference of the ages */
} DiskStats;

/* What a running scan has done so far */
typedef struct {
    guint64 files;
    guint64 dirs;
    guint64 size;
    guint64 disk;
    guint top_done;     /* folders directly in the root read in full */
    guint top_total;    /* folders directly in the root, 0 until it is listed */
    guint64 fs_used;    /* bytes used on the root's filesystem when the root is
                           its mount point, else 0 (a progress reference) */
    gchar* current;     /* folder being read (owned), NULL once done */
    GPtrArray* top;     /* DiskNode* copies (no children, no files) of the
                           folders of the root done so far, in the order read */
    guint64 root_files_size;  /* files directly in the root, once listed */
    guint64 root_files_count;
    gboolean done;
} DiskScanSnapshot;

typedef struct DiskScan DiskScan;

/* A scan of root (not started); one_filesystem: skip other filesystems
 * mounted below it */
DiskScan* disk_scan_new(const gchar* root, gboolean one_filesystem);
void disk_scan_free(DiskScan* scan);

/* Leave path (a file or a folder below the root, full path) out of the
 * reading; call it before disk_scan_run() */
void disk_scan_exclude(DiskScan* scan, const gchar* path);
/* TRUE when path (full path) was given to disk_scan_exclude() */
gboolean disk_scan_is_excluded(const DiskScan* scan, const gchar* path);

/* Read the tree: blocks until it is read or cancel is triggered (a cancelled
 * scan keeps what it read). Call it once, in a worker thread. */
void disk_scan_run(DiskScan* scan, GCancellable* cancel);

/* Progress so far (any thread, also during disk_scan_run()); free with
 * disk_scan_snapshot_clear() */
void disk_scan_snapshot(DiskScan* scan, DiskScanSnapshot* out);
void disk_scan_snapshot_clear(DiskScanSnapshot* snap);

/* The results, once disk_scan_run() returned (owned by the scan) */
const gchar* disk_scan_root_path(const DiskScan* scan);
DiskNode* disk_scan_root(DiskScan* scan);
const DiskStats* disk_scan_stats(const DiskScan* scan);
/* TRUE when the scan was cancelled before the end */
gboolean disk_scan_cancelled(const DiskScan* scan);

/* The folder of the tree read at path (full path), NULL if none */
DiskNode* disk_scan_find(DiskScan* scan, const gchar* path);

/* Take path (a folder or a listed file below the root, full path) out of
 * the tree read: the sizes and counts of the folders above it and of the
 * statistics drop it (also from their groups by age and size), as do the
 * largest files and the unreadable entries below it; the groups by
 * extension, the dates and the deepest folder stay as read (a new
 * reading with disk_scan_exclude() has them exact). The nodes below it are freed. FALSE if path is not in the tree. */
gboolean disk_scan_remove(DiskScan* scan, const gchar* path);

/* An entry of a folder: a folder (dir), a listed file (file) or the files
 * not listed summed up (count of them), or the smallest entries summed up
 * (count of them) */
typedef struct {
    guint64 size;
    DiskNode* dir;
    const gchar* file;  /* borrowed from the tree */
    guint64 count;
} DiskEntry;

/* The entries of node with a size, largest first, max at most (the smallest
 * others summed up in the last one); free with g_array_unref() */
GArray* disk_node_entries(DiskNode* node, guint max);

/* Full path of a node (from the root's path) */
gchar* disk_node_path(const DiskNode* node);
/* Bytes and count of the files of node not among its listed files */
guint64 disk_node_other_files_size(const DiskNode* node);
guint64 disk_node_other_files_count(const DiskNode* node);

/* Age group of a modification time, now being the reference (seconds) */
DiskAge disk_age_of(gint64 mtime, gint64 now);

/* Size group of a file of size bytes */
DiskSize disk_size_of(guint64 size);
/* The smallest and the largest size of a group, in bytes (both included);
 * *max is -1 for DISK_SIZE_HUGE (no top) */
void disk_size_bounds(DiskSize group, gint64* min, gint64* max);

#endif /* DISK_SCAN_H */
