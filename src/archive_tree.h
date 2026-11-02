#ifndef ARCHIVE_TREE_H
#define ARCHIVE_TREE_H

/* Archives as folders in the explorer: a file row of a browsable archive
 * (archive_extract_can_browse()) has children, so Right / Enter open it like a
 * directory. The entries are listed in a worker thread (archive_list()) and
 * become "arc_dir" rows (containers, already loaded) and "arc_file" rows
 * (leaves) whose path is "<archive>!/<entry name>". Selecting an "arc_file"
 * row extracts the entry to the cache (archive_extract_cache_path()) in a
 * worker thread and previews that copy. Read-only. */
#include "app_types.h"
#include "explorer_row.h"

/* A file row that opens as a folder */
gboolean archive_tree_row_is_archive(ExplorerRow* row);

/* A row inside an archive ("arc_dir" or "arc_file") */
gboolean archive_tree_is_entry(ExplorerRow* row);

/* Row expanded or collapsed: lists the archive on first expansion. TRUE when
 * the row is an archive or an entry (nothing else to do). */
gboolean archive_tree_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded);

/* Cache file holding the content of an "arc_file" row. If the entry is not
 * extracted yet, NULL: it is extracted in the background and the selection
 * is signalled again (or an error shown) once done. Free with g_free(). */
gchar* archive_tree_resolve(AppData* app, ExplorerRow* row);

#endif /* ARCHIVE_TREE_H */
