#ifndef ORG_RENDER_H
#define ORG_RENDER_H

/* Org document parsed for the preview into text, tags of
 * ensure_orgmode_tags() and objects (md_out.h), with the Org line of each
 * interactive preview line (headings, list items, text). No widgets: runs
 * in a worker thread (pictures are read there too). */
#include <glib.h>

#include "markup/md_out.h"

/* Orgmode global fold states */
enum {
    ORG_GLOBAL_SHOW_ALL = 0, /* All headings and subtrees expanded */
    ORG_GLOBAL_OVERVIEW,     /* Top-level headings only (Level 1) */
    ORG_GLOBAL_CONTENTS      /* All headings visible (Levels 1-6), body text hidden */
};

/* A line of the rendered preview */
typedef struct {
    gint buffer_line;       /* line in the preview (0-based) */
    gint file_line;         /* line in the .org file (0-based) */
    gint heading_level;     /* 1-6 if heading, 0 otherwise */
    gchar* todo_state;      /* "TODO", "DONE", "NEXT", "RUNNING", "WAITING", etc., or NULL */
    gboolean is_checkbox;   /* TRUE if line contains a checkbox */
    gboolean checkbox_done; /* TRUE if [X] */
    gchar* link_target;     /* URL or file path if line contains primary link */
} OrgLineInfo;

void org_line_info_free(gpointer data);

/* Parse org_text (doc_path: the file, for relative pictures; is_preview:
 * cut before the end, a banner says so with file_name and total_size).
 * folded_lines: file lines of the headings folded by hand (set of
 * GINT_TO_POINTER), or NULL; global_fold: ORG_GLOBAL_*. *line_maps gets a
 * GPtrArray of OrgLineInfo (free with g_ptr_array_unref()). */
MdOut* org_render_parse(const gchar* org_text, const gchar* doc_path, gboolean is_preview,
                        const gchar* file_name, gint64 total_size, GHashTable* folded_lines,
                        gint global_fold, GPtrArray** line_maps);

#endif /* ORG_RENDER_H */
