#ifndef PREVIEW_SUMMARY_H
#define PREVIEW_SUMMARY_H

/* Text summaries shown in the preview pane for explorer rows that have no file
 * content: remote machines, notebooks, bookmark folders, the notes trash and
 * Canvas notes (directories).
 * Columns are padded with spaces: display them in a monospace font.
 * No widgets (unit-tested in tests/). */
#include "core/app_types.h"
#include "explorer_row.h"
#include "remote_store.h"

/* Summary of a remote machine (connection details and view shortcuts). Caller frees. */
gchar* preview_summary_remote(const RemoteMachine* rm);

/* Summary of a container row ("notebook", "bm_folder" or "trash") built from
 * its children, or of a row on a Canvas note (preview_summary_canvas()).
 * NULL for other rows. Caller frees. */
gchar* preview_summary_container(ExplorerRow* row);

/* Summary of the Canvas note in directory path: counts, its text as Markdown
 * (page.xml read only up to PREVIEW_BYTES) and the keys. NULL if path is not
 * a Canvas note directory. Caller frees. */
gchar* preview_summary_canvas(const gchar* name, const gchar* path);

#endif /* PREVIEW_SUMMARY_H */
