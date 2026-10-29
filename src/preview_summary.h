#ifndef PREVIEW_SUMMARY_H
#define PREVIEW_SUMMARY_H

/* Text summaries shown in the preview pane for explorer rows that have no file
 * content: remote machines, notebooks, bookmark folders and the notes trash.
 * Columns are padded with spaces: display them in a monospace font.
 * No widgets (unit-tested in tests/). */
#include "app_types.h"
#include "explorer_row.h"
#include "remote_store.h"

/* Summary of a remote machine (connection details and view shortcuts). Caller frees. */
gchar* preview_summary_remote(const RemoteMachine* rm);

/* Summary of a container row ("notebook", "bm_folder" or "trash") built from
 * its children. NULL for other row types. Caller frees. */
gchar* preview_summary_container(ExplorerRow* row);

#endif /* PREVIEW_SUMMARY_H */
