#ifndef GIT_GRAPH_H
#define GIT_GRAPH_H

/* The lanes of a commit graph, like `git log --graph`: given the commits
 * newest first (children before their parents), where each one's dot goes
 * and which lines join the rows. Pure: no git, no widgets. */
#include <glib.h>

/* A line of a row: in its top half, from column from at the top edge to
 * column to at the middle (the dot's height); in its bottom half, from
 * column from at the middle to column to at the bottom edge */
typedef struct {
    guint16 from;
    guint16 to;
    guint16 color;   /* colour of the lane (an index that only grows: modulo the colours drawn) */
    gboolean bottom; /* FALSE: top half, TRUE: bottom half */
} GitGraphLine;

/* The graph of one commit's row */
typedef struct {
    guint node;       /* column of the commit's dot */
    guint node_color; /* colour of its lane */
    guint width;      /* columns used by the row (lines and dot) */
    guint n_lines;
    GitGraphLine lines[];
} GitGraphRow;

/* The lanes open after the rows added so far */
typedef struct GitGraph GitGraph;

GitGraph* git_graph_new(void);
void git_graph_free(GitGraph* g);

/* The row of the next commit, hash, whose parents are parents (full
 * hashes, NULL-terminated; NULL or empty: a root commit), the first parent
 * first. Commits must come after all their children for the lanes to meet.
 * g_free() */
GitGraphRow* git_graph_add(GitGraph* g, const gchar* hash, const gchar* const* parents);

#endif /* GIT_GRAPH_H */
