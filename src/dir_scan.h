/* Directory listings for the explorer tree, read off the UI thread: one level
 * sorted like the tree (folders first, then by name, date, size or
 * extension), and the levels of the folders shown expanded below it.
 * No AppData, no widgets. */
#ifndef DIR_SCAN_H
#define DIR_SCAN_H

#include <glib.h>
#include <gio/gio.h>

/* Same values as the explorer sort modes (SORT_NAME... in app_types.h) */
typedef enum {
    DIR_SCAN_SORT_NAME,
    DIR_SCAN_SORT_DATE, /* newest first */
    DIR_SCAN_SORT_SIZE, /* largest first */
    DIR_SCAN_SORT_EXT,
} DirScanSort;

typedef struct {
    gchar* name;
    gboolean is_dir;
    gint64 mtime;
    gint64 size; /* 0 for folders */
} DirScanEntry;

/* One directory level, and the levels of its expanded folders */
typedef struct DirScanNode {
    gchar* dirname;
    gboolean unreadable;  /* the directory could not be opened (entries NULL) */
    GArray* entries;      /* DirScanEntry, sorted */
    GPtrArray* children;  /* DirScanNode* per entry, NULL when not an expanded folder */
} DirScanNode;

/* Read dirname (entries starting with '.' only when show_hidden), and below it
 * every folder whose path is in expanded (a set of paths, may be NULL).
 * Stops early when cancellable is cancelled (the result is then partial). */
DirScanNode* dir_scan_read(const gchar* dirname, gboolean show_hidden, DirScanSort sort,
                           GHashTable* expanded, GCancellable* cancellable);

void dir_scan_node_free(DirScanNode* node);

/* Add to out (a set of paths) the folders below base that "expand all"
 * opens: breadth first, until about max_rows rows are listed; symbolic links
 * to folders stay closed (no loops). base itself is not added. */
void dir_scan_collect_expand_all(const gchar* base, gboolean show_hidden, guint max_rows,
                                 GHashTable* out, GCancellable* cancellable);

/* Order of the tree: folders first, then by sort, then by name */
gint dir_scan_compare(const DirScanEntry* a, const DirScanEntry* b, DirScanSort sort);

#endif /* DIR_SCAN_H */
