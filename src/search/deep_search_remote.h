#ifndef DEEP_SEARCH_REMOTE_H
#define DEEP_SEARCH_REMOTE_H

/* The pieces of a deep search on a remote machine (deep_search.h): the
 * entries listed by find run there, a record each; the content searched by
 * grep run there on the files passing the other criteria, the matches then
 * those of text_search.h on the lines it gives. Pure GLib, unit-tested. */
#include "search/deep_query.h"

/* The shell command printing every entry below the current folder as
 * deep_search_run() tests them: "<type> <size> <mtime> <path>\0" (type: f,
 * d, l...; path relative), after a first "r 0 0 .\0" for the folder
 * itself (it was entered), hidden names, /proc, /sys, /dev and (with
 * one_filesystem) other filesystems not entered, to max_depth. Needs GNU
 * find (-printf). Caller frees. */
gchar* deep_remote_find_command(const DeepQuery* q);

/* One record of that command (without its NUL): FALSE when it is not one */
typedef struct {
    gchar type;        /* 'f' file, 'd' folder, 'l' link, ...; 'r' the folder searched */
    guint64 size;
    gint64 mtime;
    const gchar* rel;  /* in the record */
} DeepRemoteEntry;
gboolean deep_remote_parse_entry(const gchar* record, DeepRemoteEntry* e);

/* What the content search found in one file */
typedef struct {
    guint count;  /* at most max_count */
    gboolean more;
    gint line;    /* the first match */
    gint column;
    gint length;
    gchar* text;
} DeepRemoteMatch;
void deep_remote_match_free(gpointer m);

/* The matches of query in the lines grep printed ("<path>\0<line>:<text>\n",
 * remote_search_grep_input_command()), by path as written there: path ->
 * DeepRemoteMatch*, the files with at least one */
GHashTable* deep_remote_parse_matches(const gchar* data, gsize len, const gchar* query, const TextSearchOptions* opts,
                                      guint max_count);

#endif /* DEEP_SEARCH_REMOTE_H */
