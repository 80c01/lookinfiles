#ifndef REMOTE_SEARCH_H
#define REMOTE_SEARCH_H

/* Search in files and Go to File on a remote machine (vfs.h): the files
 * are listed, and the lines that may match found, by commands run on the
 * machine (git ls-files or find, then grep, through vfs_exec()); the
 * matches are then those of text_search.h on these lines, so the options
 * (any case, whole word, regex, fuzzy) give what they give locally.
 * Blocking: worker threads only. The commands and the reading of their
 * output are unit-tested. */
#include "code/code_index.h"
#include "search/text_search.h"

/* Bytes of output read at most (a search that finds more says so) */
#define REMOTE_SEARCH_MAX_OUTPUT (32 * 1024 * 1024)

/* The shell command that prints the files below the current folder,
 * relative, each ended by a NUL: those git lists (with ignored, the ones
 * it ignores too), outside git every file but those of hidden and build
 * folders (ignored: all but .git). Caller frees. */
gchar* remote_search_list_command(gboolean ignored);

/* The shell command that prints, for each line of these files that may
 * match query: "<path>\0<line>:<text>\n" (grep -nH --null), a superset of
 * the matches; binary files skipped. Caller frees. */
gchar* remote_search_grep_command(const gchar* query, const TextSearchOptions* opts, gboolean ignored);

/* The paths of a NUL-separated list (remote_search_list_command()), below
 * root ("./" dropped), those keep (NULL: all) keeps, at most max
 * (*truncated then) */
GPtrArray* remote_search_parse_list(const gchar* root, const gchar* data, gsize len, CodeIndexKeepFunc keep,
                                    gpointer keep_data, guint max, gboolean* truncated);

/* The matches of query in the lines printed by remote_search_grep_command()
 * (data, len bytes: a last line cut short is dropped), as CodeReference*
 * by path and line, their paths below root; files keep (NULL: all) does
 * not keep are left out. stats->stop says when TEXT_SEARCH_MAX_RESULTS
 * were found. */
GPtrArray* remote_search_parse_lines(const gchar* root, const gchar* data, gsize len, const gchar* query,
                                     const TextSearchOptions* opts, CodeIndexKeepFunc keep, gpointer keep_data,
                                     TextSearchStats* stats);

/* The whole search in the files below root, a remote machine's folder:
 * as text_search_files() on code_index_some_files(). *cut: the output
 * was longer than REMOTE_SEARCH_MAX_OUTPUT. NULL with error when the
 * commands could not run. */
GPtrArray* remote_search_files(const gchar* root, const gchar* query, const TextSearchOptions* opts, gboolean ignored,
                               CodeIndexKeepFunc keep, gpointer keep_data, GCancellable* cancel,
                               TextSearchStats* stats, gboolean* cut, GError** error);

/* The files below root, a remote machine's folder, as
 * code_index_all_files() lists them (at most max, *truncated then); NULL
 * with error when the command could not run */
GPtrArray* remote_search_all_files(const gchar* root, guint max, GCancellable* cancel, gboolean* truncated,
                                   GError** error);

#endif /* REMOTE_SEARCH_H */
