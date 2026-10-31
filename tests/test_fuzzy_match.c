/* Unit tests for src/fuzzy_match.c: quick open path matching. */
#include "fuzzy_match.h"

static void test_matches(void) {
    g_assert_cmpint(fuzzy_match_score("flc", "src/file_list.c"), >=, 0);
    g_assert_cmpint(fuzzy_match_score("FL", "src/file_list.c"), <, 0);   /* capitals: exact case */
    g_assert_cmpint(fuzzy_match_score("fl", "src/FileList.c"), >=, 0);
    g_assert_cmpint(fuzzy_match_score("xyz", "src/file_list.c"), <, 0);
    g_assert_cmpint(fuzzy_match_score("cl", "lc"), <, 0);                /* order matters */
    g_assert_cmpint(fuzzy_match_score("file list", "src/file_list.c"), >=, 0); /* spaces ignored */
    g_assert_cmpint(fuzzy_match_score("", "a/b.c"), >=, 0);
    g_assert_cmpint(fuzzy_match_score(NULL, "a"), <, 0);
}

static void test_ranking(void) {
    /* The file name beats the directories */
    g_assert_cmpint(fuzzy_match_score("main", "src/main.c"), >,
                    fuzzy_match_score("main", "main_tools/x/readme.txt"));
    /* Consecutive characters beat scattered ones */
    g_assert_cmpint(fuzzy_match_score("view", "src/viewer.c"), >,
                    fuzzy_match_score("view", "src/vim_engine_ex.c"));
    /* Word starts: fl -> file_list beats a scattered f..l */
    g_assert_cmpint(fuzzy_match_score("fl", "src/file_list.c"), >,
                    fuzzy_match_score("fl", "src/fooball.c"));
    /* Shorter paths win ties */
    g_assert_cmpint(fuzzy_match_score("a", "a.c"), >, fuzzy_match_score("a", "a/longer/path/a.c"));
    /* Empty query: shorter first */
    g_assert_cmpint(fuzzy_match_score("", "a.c"), >, fuzzy_match_score("", "abc/def.c"));
}

static void test_positions(void) {
    gsize* pos = fuzzy_match_positions("fl", "src/file_list.c");
    g_assert_nonnull(pos);
    g_assert_cmpuint(pos[0], ==, 4); /* f of file */
    g_assert_cmpuint(pos[1], ==, 9); /* l of list */
    g_assert_cmpuint(pos[2], ==, G_MAXSIZE);
    g_free(pos);

    pos = fuzzy_match_positions("main", "src/main.c");
    g_assert_nonnull(pos);
    for (gsize i = 0; i < 4; i++) g_assert_cmpuint(pos[i], ==, 4 + i);
    g_free(pos);

    g_assert_null(fuzzy_match_positions("zz", "src/main.c"));
    g_assert_null(fuzzy_match_positions("", "src/main.c"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/fuzzy_match/matches", test_matches);
    g_test_add_func("/fuzzy_match/ranking", test_ranking);
    g_test_add_func("/fuzzy_match/positions", test_positions);
    return g_test_run();
}
