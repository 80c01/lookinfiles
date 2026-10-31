#ifndef DIR_SUMMARY_H
#define DIR_SUMMARY_H

/* Overview of one directory level for the directory preview: its entries
 * (name, type, size, date), totals and sorting. Pure: the caller enumerates
 * the directory (one level only, never recursively) and adds the entries. */
#include <glib.h>

#include "file_types.h"

/* Entries kept at most: a larger directory is reported as truncated */
#define DIR_SUMMARY_SCAN_LIMIT 5000

/* Sort orders, same meaning as the explorer's SORT_* modes */
typedef enum {
    DIR_SUMMARY_SORT_NAME = 0,
    DIR_SUMMARY_SORT_DATE,  /* newest first */
    DIR_SUMMARY_SORT_SIZE,  /* largest first */
    DIR_SUMMARY_SORT_EXT
} DirSummarySort;

typedef struct {
    gchar* name;
    gboolean is_dir;
    guint64 size;   /* bytes, 0 for directories */
    gint64 mtime;   /* seconds since the epoch, 0 if unknown */
} DirSummaryEntry;

typedef struct {
    GArray* entries;      /* DirSummaryEntry */
    guint n_dirs;
    guint n_files;
    guint64 total_size;   /* sum of the file sizes of this level */
    gint64 newest_mtime;  /* most recent entry date, 0 if none */
    gboolean truncated;   /* stopped at DIR_SUMMARY_SCAN_LIMIT entries */
    gchar* readme;        /* best README file name of this level, NULL if none */
    guint kinds[FILE_KIND_LAST + 1]; /* files per FileKind, guessed from the name */
} DirSummary;

/* Changed files below a directory, by git status */
typedef struct {
    guint modified;
    guint added;
    guint untracked;
    guint deleted;
    guint renamed;
    guint conflicts;
} DirGitCounts;

DirSummary* dir_summary_new(void);
void dir_summary_free(DirSummary* s);

/* Same entries (names, sizes, dates, in the same order) and README */
gboolean dir_summary_equal(const DirSummary* a, const DirSummary* b);

/* Add one entry. Returns FALSE (and sets truncated) once the limit is reached. */
gboolean dir_summary_add(DirSummary* s, const gchar* name, gboolean is_dir,
                         guint64 size, gint64 mtime);

/* Sort the entries: directories first, then by the given order (name as tie-breaker) */
void dir_summary_sort(DirSummary* s, DirSummarySort sort);

/* "2 documents · 1 image · 3 other" from the kinds of the files (from their
 * names, no I/O). NULL when no file has a known kind (all "other"). */
gchar* dir_summary_kinds_line(const DirSummary* s);

/* README rank of a file name, case-insensitive: README.md / README.markdown 4,
 * README.org 3, README 2, README.txt 1, anything else 0 */
gint dir_summary_readme_rank(const gchar* name);

/* Status letter of a git badge ("<span ...>M</span>" -> 'M'), 0 if none */
gchar dir_summary_git_letter(const gchar* badge);

/* Count the changed files strictly below dir in statuses (full path -> badge
 * markup, as made by scan_git_statuses()). Parent directories carry a rolled-up
 * badge too: only leaves (paths with no entry below them) are counted. */
void dir_summary_count_git(GHashTable* statuses, const gchar* dir, DirGitCounts* counts);

/* Short relative date: "just now", "5 min ago", "3 h ago", "yesterday",
 * "4 days ago", then "2026-03-14". Empty string for an unknown date (0). */
gchar* dir_summary_format_age(gint64 mtime, gint64 now);

/* Text padded with spaces (or cut with "…") to exactly width characters */
gchar* dir_summary_pad(const gchar* text, glong width, gboolean align_right);

#endif /* DIR_SUMMARY_H */
