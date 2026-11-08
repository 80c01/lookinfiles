#ifndef DIFF_VIEW_H
#define DIFF_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* Unified diffs and patches (git diff, git format-patch, diff -u, svn) for
 * the preview: a summary of the changed files with their added / removed
 * line counts, then the diff with added lines green, removed lines red, hunk
 * headers and file headers highlighted. Hunk line counts are followed, so a
 * removed line starting with "---" is not taken for a file header. Pure
 * GLib, unit-tested. */

typedef struct {
    gchar* path;          /* new path ("b/" stripped), or the old one if deleted */
    guint added;
    guint removed;
    gboolean binary;      /* "Binary files ... differ" */
} DiffFileStat;

typedef struct {
    GArray* files;        /* DiffFileStat */
    gchar* subject;       /* format-patch Subject:, or NULL */
    guint added;
    guint removed;
} DiffSummary;

/* Summary of a diff; NULL if the text has no file header or hunk */
DiffSummary* diff_summarize(const gchar* text, gsize len);
void diff_summary_free(DiffSummary* summary);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DiffSummary, diff_summary_free)

/* Append the summary and the coloured diff. FALSE (doc unchanged) if the
 * text is not a diff. */
gboolean diff_view_append(PreviewDoc* doc, const gchar* text, gsize len);

#endif /* DIFF_VIEW_H */
