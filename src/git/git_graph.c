#include "git/git_graph.h"

#include <string.h>

struct GitGraph {
    GPtrArray* lanes; /* per column: the hash its lane waits for (owned), or NULL: free */
    GArray* colors;   /* per column: the colour of its lane (guint) */
    guint next_color; /* colour of the next lane opened */
};

GitGraph* git_graph_new(void) {
    GitGraph* g = g_new0(GitGraph, 1);
    g->lanes = g_ptr_array_new_with_free_func(g_free);
    g->colors = g_array_new(FALSE, TRUE, sizeof(guint));
    return g;
}

void git_graph_free(GitGraph* g) {
    if (g == NULL) return;
    g_ptr_array_unref(g->lanes);
    g_array_unref(g->colors);
    g_free(g);
}

/* A new lane waiting for hash, in the first free column: its column */
static guint open_lane(GitGraph* g, const gchar* hash) {
    guint col = 0;
    while (col < g->lanes->len && g_ptr_array_index(g->lanes, col) != NULL) col++;
    if (col == g->lanes->len) {
        g_ptr_array_add(g->lanes, NULL);
        g_array_set_size(g->colors, g->lanes->len);
    }
    g_free(g_ptr_array_index(g->lanes, col));
    g_ptr_array_index(g->lanes, col) = g_strdup(hash);
    g_array_index(g->colors, guint, col) = g->next_color++;
    return col;
}

static guint lane_color(GitGraph* g, guint col) {
    return g_array_index(g->colors, guint, col);
}

static void add_line(GArray* lines, guint from, guint to, guint color, gboolean bottom) {
    GitGraphLine line = {(guint16)from, (guint16)to, (guint16)color, bottom};
    g_array_append_val(lines, line);
}

GitGraphRow* git_graph_add(GitGraph* g, const gchar* hash, const gchar* const* parents) {
    GArray* lines = g_array_new(FALSE, FALSE, sizeof(GitGraphLine));
    guint before = g->lanes->len;

    /* The dot goes in the first lane waiting for this commit, or in a new
     * one (a branch tip) */
    gint found = -1;
    for (guint i = 0; i < before && found < 0; i++) {
        if (g_strcmp0(g_ptr_array_index(g->lanes, i), hash) == 0) found = (gint)i;
    }
    guint node = found >= 0 ? (guint)found : open_lane(g, hash);
    guint node_color = lane_color(g, node);

    /* Top half: the lanes waiting for this commit meet at its dot, the
     * others go on */
    gboolean* through = g_new0(gboolean, MAX(before, 1));
    for (guint i = 0; i < before; i++) {
        const gchar* want = g_ptr_array_index(g->lanes, i);
        if (want == NULL || (found < 0 && i == node)) continue; /* a tip: nothing above its dot */
        if (strcmp(want, hash) == 0) {
            add_line(lines, i, node, lane_color(g, i), FALSE);
            g_free(g_ptr_array_index(g->lanes, i));
            g_ptr_array_index(g->lanes, i) = NULL;
        } else {
            add_line(lines, i, i, lane_color(g, i), FALSE);
            through[i] = TRUE;
        }
    }

    /* The first parent goes on in the dot's lane */
    const gchar* first = parents != NULL ? parents[0] : NULL;
    if (first != NULL) {
        g_ptr_array_index(g->lanes, node) = g_strdup(first);
        g_array_index(g->colors, guint, node) = node_color;
        add_line(lines, node, node, node_color, TRUE);
    }
    /* The other parents (a merge): the lane already waiting for one, else a
     * new lane */
    for (guint p = 1; first != NULL && parents[p] != NULL; p++) {
        gboolean seen = FALSE;
        for (guint q = 0; q < p && !seen; q++) seen = strcmp(parents[q], parents[p]) == 0;
        if (seen) continue;
        gint col = -1;
        for (guint i = 0; i < g->lanes->len && col < 0; i++) {
            if (i != node && g_strcmp0(g_ptr_array_index(g->lanes, i), parents[p]) == 0) col = (gint)i;
        }
        guint to = col >= 0 ? (guint)col : open_lane(g, parents[p]);
        add_line(lines, node, to, lane_color(g, to), TRUE);
    }
    /* Bottom half of the lanes going on */
    for (guint i = 0; i < before; i++) {
        if (through[i]) add_line(lines, i, i, lane_color(g, i), TRUE);
    }
    g_free(through);

    /* Free columns at the right end are given back */
    while (g->lanes->len > 0 && g_ptr_array_index(g->lanes, g->lanes->len - 1) == NULL) {
        g_ptr_array_set_size(g->lanes, g->lanes->len - 1);
    }
    g_array_set_size(g->colors, g->lanes->len);

    GitGraphRow* row = g_malloc(sizeof(GitGraphRow) + lines->len * sizeof(GitGraphLine));
    row->node = node;
    row->node_color = node_color;
    row->width = node + 1;
    row->n_lines = lines->len;
    for (guint i = 0; i < lines->len; i++) {
        GitGraphLine* line = &g_array_index(lines, GitGraphLine, i);
        row->lines[i] = *line;
        row->width = MAX(row->width, (guint)MAX(line->from, line->to) + 1);
    }
    g_array_unref(lines);
    return row;
}
