#ifndef PREVIEW_SUMMARY_H
#define PREVIEW_SUMMARY_H

/* Summaries shown in the preview pane for explorer rows that have no file
 * content: remote machines, notebooks, bookmark folders, the notes trash and
 * Canvas notes (directories).
 * A summary is a list of typed lines (title, fields, headings, items, keys)
 * that summary_view draws; preview_summary_to_text() gives its plain-text
 * form, columns padded with spaces for a monospace font.
 * No widgets (unit-tested in tests/). */
#include "core/app_types.h"
#include "explorer/explorer_row.h"
#include "explorer/remote_store.h"

#define PREVIEW_SUMMARY_NAME_MAX 40 /* item name column (longer names are not padded) */

typedef enum {
    PREVIEW_SUMMARY_TITLE,   /* text: the title, with an icon */
    PREVIEW_SUMMARY_FIELD,   /* text: label, detail: value */
    PREVIEW_SUMMARY_HEADING, /* text: a section heading ("Notes", "Shortcuts") */
    PREVIEW_SUMMARY_ITEM,    /* text: a child's name, detail: its kind or target; icon */
    PREVIEW_SUMMARY_KEY,     /* text: keys, detail: action */
    PREVIEW_SUMMARY_NOTE,    /* text: a remark on one line ("(empty)") */
    PREVIEW_SUMMARY_TEXT,    /* text: a block of text kept as is (a Canvas note's Markdown) */
} PreviewSummaryKind;

typedef struct {
    PreviewSummaryKind kind;
    gchar* text;
    gchar* detail;    /* NULL for none */
    gchar* glyph;     /* glyph name (icons.h) drawn before the text, NULL for none */
    gchar* file_icon; /* else a file name whose file-type icon is drawn, NULL for none */
} PreviewSummaryLine;

/* Lines (PreviewSummaryLine, owned by the array) of a remote machine:
 * connection details and view shortcuts. NULL for NULL. Unref the array. */
GPtrArray* preview_summary_remote_lines(const RemoteMachine* rm);

/* Lines of a container row ("notebook", "bm_folder", "trash",
 * "remote_folder") built from its children, or of a row on a Canvas note
 * (preview_summary_canvas_lines()). NULL for other rows. Unref the array. */
GPtrArray* preview_summary_container_lines(ExplorerRow* row);

/* Lines of the Canvas note in directory path: counts, its text as Markdown
 * (page.xml read only up to PREVIEW_BYTES) and the keys. NULL if path is not
 * a Canvas note directory. Unref the array. */
GPtrArray* preview_summary_canvas_lines(const gchar* name, const gchar* path);

/* Width of the name column of the items starting at index i (up to the
 * first line that is not an item), at most PREVIEW_SUMMARY_NAME_MAX */
glong preview_summary_item_width(GPtrArray* lines, guint i);

/* Plain text of lines (NULL for NULL). Caller frees. */
gchar* preview_summary_to_text(GPtrArray* lines);

/* Plain text of the summaries above. Caller frees. */
gchar* preview_summary_remote(const RemoteMachine* rm);
gchar* preview_summary_container(ExplorerRow* row);
gchar* preview_summary_canvas(const gchar* name, const gchar* path);

#endif /* PREVIEW_SUMMARY_H */
