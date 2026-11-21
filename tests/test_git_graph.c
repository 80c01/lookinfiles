/* Unit tests for src/git/git_graph.c (lanes of the commit graph). */
#include "git/git_graph.h"

/* A row as text: "node/width: t0-0 b0-1 ..." (t: top half, b: bottom
 * half), lines in their order */
static gchar* row_text(GitGraphRow* row) {
    GString* s = g_string_new(NULL);
    g_string_append_printf(s, "%u/%u:", row->node, row->width);
    for (guint i = 0; i < row->n_lines; i++) {
        const GitGraphLine* l = &row->lines[i];
        g_string_append_printf(s, " %c%u-%u", l->bottom ? 'b' : 't', l->from, l->to);
    }
    g_free(row);
    return g_string_free(s, FALSE);
}

/* The next row as text, parents separated by spaces (NULL: a root) */
static gchar* add(GitGraph* g, const gchar* hash, const gchar* parents) {
    gchar** p = parents != NULL ? g_strsplit(parents, " ", -1) : NULL;
    gchar* text = row_text(git_graph_add(g, hash, (const gchar* const*)p));
    g_strfreev(p);
    return text;
}

static void expect(GitGraph* g, const gchar* hash, const gchar* parents, const gchar* want) {
    gchar* got = add(g, hash, parents);
    g_assert_cmpstr(got, ==, want);
    g_free(got);
}

static void test_linear(void) {
    GitGraph* g = git_graph_new();
    expect(g, "c", "b", "0/1: b0-0");
    expect(g, "b", "a", "0/1: t0-0 b0-0");
    expect(g, "a", NULL, "0/1: t0-0");
    git_graph_free(g);
}

static void test_merge(void) {
    /* m merges f into b; both come from a */
    GitGraph* g = git_graph_new();
    expect(g, "m", "b f", "0/2: b0-0 b0-1");
    expect(g, "b", "a", "0/2: t0-0 t1-1 b0-0 b1-1");
    expect(g, "f", "a", "1/2: t0-0 t1-1 b1-1 b0-0");
    expect(g, "a", NULL, "0/2: t0-0 t1-0");
    /* Every lane met: the next tip starts at the left again */
    expect(g, "z", NULL, "0/1:");
    git_graph_free(g);
}

static void test_colors(void) {
    GitGraph* g = git_graph_new();
    GitGraphRow* m = git_graph_add(g, "m", (const gchar* const[]){"b", "f", NULL});
    /* The merged lane has a colour of its own */
    g_assert_cmpuint(m->lines[1].color, !=, m->node_color);
    g_assert_cmpuint(m->lines[0].color, ==, m->node_color);
    guint merged = m->lines[1].color;
    g_free(m);
    GitGraphRow* b = git_graph_add(g, "b", (const gchar* const[]){"a", NULL});
    g_assert_cmpuint(b->lines[1].color, ==, merged); /* the f lane goes on in its colour */
    g_free(b);
    git_graph_free(g);
}

static void test_tips(void) {
    /* Two branches from a, both listed: the second tip opens a lane */
    GitGraph* g = git_graph_new();
    expect(g, "x", "a", "0/1: b0-0");
    expect(g, "y", "a", "1/2: t0-0 b1-1 b0-0");
    expect(g, "a", NULL, "0/2: t0-0 t1-0");
    git_graph_free(g);
}

static void test_hole_reused(void) {
    /* Lane 0 ends (root) while lane 1 goes on: the next tip takes column 0 */
    GitGraph* g = git_graph_new();
    expect(g, "m", "r q", "0/2: b0-0 b0-1");
    expect(g, "r", NULL, "0/2: t0-0 t1-1 b1-1");
    expect(g, "t", "q", "0/2: t1-1 b0-0 b1-1");
    expect(g, "q", NULL, "0/2: t0-0 t1-0");
    git_graph_free(g);
}

static void test_merge_into_waiting_lane(void) {
    /* m's second parent is already waited for by another lane: no new lane */
    GitGraph* g = git_graph_new();
    expect(g, "x", "p", "0/1: b0-0");
    expect(g, "m", "a p", "1/2: t0-0 b1-1 b1-0 b0-0");
    expect(g, "p", "a", "0/2: t0-0 t1-1 b0-0 b1-1");
    expect(g, "a", NULL, "0/2: t0-0 t1-0");
    git_graph_free(g);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_graph/linear", test_linear);
    g_test_add_func("/git_graph/merge", test_merge);
    g_test_add_func("/git_graph/colors", test_colors);
    g_test_add_func("/git_graph/tips", test_tips);
    g_test_add_func("/git_graph/hole_reused", test_hole_reused);
    g_test_add_func("/git_graph/merge_into_waiting_lane", test_merge_into_waiting_lane);
    return g_test_run();
}
