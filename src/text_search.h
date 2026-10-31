#ifndef TEXT_SEARCH_H
#define TEXT_SEARCH_H

/* Search the files of a project for a text (Ctrl+Shift+F), like grep -F:
 * literal, case-insensitive (ASCII letters) unless the query has a capital
 * letter. Binary files (a NUL byte in their first 8 KB) and files over
 * CODE_INDEX_MAX_FILE_BYTES are skipped. Blocking: call it from a worker
 * thread. Pure GLib, unit-tested. */
#include "code_index.h"

/* Every occurrence of query in files (absolute paths), as CodeReference*
 * (code_index.h) by path, line and column. At most CODE_INDEX_MAX_REFERENCES
 * and CODE_INDEX_MAX_BYTES read; *truncated (may be NULL) tells when a limit
 * was hit. */
GPtrArray* text_search_files(const gchar* query, GPtrArray* files, GCancellable* cancellable,
                             gboolean* truncated);

/* The query is matched with its case (it has a capital letter) */
gboolean text_search_exact_case(const gchar* query);

#endif /* TEXT_SEARCH_H */
