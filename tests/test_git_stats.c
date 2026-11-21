/* Unit tests for src/git/git_stats.c: the counting, then the reading of a
 * repository made with libgit2 (no git program needed). */
#include "git/git_stats.h"

#include <git2.h>
#include <glib/gstdio.h>

/* 2026-11-16 was a Monday */
#define MON_2026_11_16_UTC G_GINT64_CONSTANT(1794787200)

static void test_days(void) {
    g_assert_cmpint(git_stats_day_of(0, 0), ==, 0);
    g_assert_cmpint(git_stats_day_of(86399, 0), ==, 0);
    g_assert_cmpint(git_stats_day_of(-1, 0), ==, -1);
    /* 23:30 UTC is the next day one hour east */
    g_assert_cmpint(git_stats_day_of(86400 - 1800, 60), ==, 1);
    g_assert_cmpint(git_stats_weekday(0), ==, 3); /* Thursday */
    g_assert_cmpint(git_stats_weekday(-1), ==, 2);
    g_assert_cmpint(git_stats_weekday(git_stats_day_of(MON_2026_11_16_UTC, 0)), ==, 0);
}

static void test_add(void) {
    GitStats* s = git_stats_new();
    /* Monday 09:00 UTC, by Ada; the same moment is 10:00 in UTC+1 */
    git_stats_add(s, MON_2026_11_16_UTC + 9 * 3600, 0, "Ada");
    git_stats_add(s, MON_2026_11_16_UTC + 9 * 3600, 60, "Ada");
    /* Sunday 2026-09-06 23:00 UTC by Bob: Monday 01:00 two hours east */
    gint64 sunday = MON_2026_11_16_UTC - 71 * 86400 + 23 * 3600;
    git_stats_add(s, sunday, 0, "Bob");
    git_stats_add(s, sunday, 120, "Bob");
    git_stats_add(s, sunday, 0, "Bob");
    git_stats_finish(s);

    g_assert_cmpuint(s->commits, ==, 5);
    g_assert_cmpint(s->first, ==, sunday);
    g_assert_cmpint(s->last, ==, MON_2026_11_16_UTC + 9 * 3600);
    g_assert_cmpuint(s->punch[0][9], ==, 1);
    g_assert_cmpuint(s->punch[0][10], ==, 1);
    g_assert_cmpuint(s->punch[6][23], ==, 2);
    g_assert_cmpuint(s->punch[0][1], ==, 1);

    /* Months: September to November 2026, October empty */
    g_assert_cmpint(s->first_month, ==, 2026 * 12 + 8);
    g_assert_cmpuint(s->months->len, ==, 3);
    g_assert_cmpuint(g_array_index(s->months, guint, 0), ==, 3);
    g_assert_cmpuint(g_array_index(s->months, guint, 1), ==, 0);
    g_assert_cmpuint(g_array_index(s->months, guint, 2), ==, 2);

    gint monday = git_stats_day_of(MON_2026_11_16_UTC, 0);
    g_assert_cmpuint(git_stats_on_day(s, monday), ==, 2);
    g_assert_cmpuint(git_stats_on_day(s, monday + 1), ==, 0);

    /* Bob first: more commits */
    g_assert_cmpuint(s->authors->len, ==, 2);
    const GitAuthorStats* bob = g_ptr_array_index(s->authors, 0);
    g_assert_cmpstr(bob->name, ==, "Bob");
    g_assert_cmpuint(bob->commits, ==, 3);
    const GitAuthorStats* ada = g_ptr_array_index(s->authors, 1);
    g_assert_cmpuint(ada->commits, ==, 2);
    git_stats_free(s);
}

/* A month before the first one counted grows the array at its start */
static void test_months_backwards(void) {
    GitStats* s = git_stats_new();
    git_stats_add(s, MON_2026_11_16_UTC, 0, "a");
    git_stats_add(s, MON_2026_11_16_UTC - 365 * 86400, 0, "a"); /* 2025-11 */
    g_assert_cmpint(s->first_month, ==, 2025 * 12 + 10);
    g_assert_cmpuint(s->months->len, ==, 13);
    g_assert_cmpuint(g_array_index(s->months, guint, 0), ==, 1);
    g_assert_cmpuint(g_array_index(s->months, guint, 12), ==, 1);
    git_stats_free(s);
}

static gboolean remove_tree(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d != NULL) g_dir_close(d);
    }
    return g_remove(path) == 0;
}

/* Commit the (empty) index on HEAD by name at time */
static void commit(git_repository* repo, const gchar* name, gint64 time) {
    git_index* index = NULL;
    git_oid tree_id, id;
    git_tree* tree = NULL;
    git_signature* sig = NULL;
    git_object* parent = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    g_assert_cmpint(git_tree_lookup(&tree, repo, &tree_id), ==, 0);
    g_assert_cmpint(git_signature_new(&sig, name, "x@example.org", (git_time_t)time, 0), ==, 0);
    gint n = git_revparse_single(&parent, repo, "HEAD") == 0 ? 1 : 0;
    const git_commit* parents[1] = { (const git_commit*)parent };
    g_assert_cmpint(git_commit_create(&id, repo, "HEAD", sig, sig, NULL, "c", tree, (size_t)n, parents), ==, 0);
    git_object_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

static void test_read(void) {
    git_libgit2_init();
    gchar* dir = g_dir_make_tmp("git_stats_test_XXXXXX", NULL);
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);

    GitStats* empty = git_stats_read(dir, NULL);
    g_assert_nonnull(empty);
    g_assert_cmpuint(empty->commits, ==, 0);
    git_stats_free(empty);

    commit(repo, "Ada", MON_2026_11_16_UTC + 3600);
    commit(repo, "Bob", MON_2026_11_16_UTC + 7200);
    commit(repo, "Ada", MON_2026_11_16_UTC + 10800);
    GitStats* s = git_stats_read(dir, NULL);
    g_assert_nonnull(s);
    g_assert_cmpuint(s->commits, ==, 3);
    g_assert_cmpuint(s->punch[0][1], ==, 1);
    g_assert_cmpuint(s->punch[0][3], ==, 1);
    g_assert_cmpstr(((GitAuthorStats*)g_ptr_array_index(s->authors, 0))->name, ==, "Ada");
    git_stats_free(s);

    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(git_stats_read(dir, cancel));
    g_object_unref(cancel);

    git_repository_free(repo);
    gchar* plain = g_dir_make_tmp("git_stats_plain_XXXXXX", NULL);
    g_assert_null(git_stats_read(plain, NULL));
    remove_tree(plain);
    g_free(plain);
    remove_tree(dir);
    g_free(dir);
    git_libgit2_shutdown();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_stats/days", test_days);
    g_test_add_func("/git_stats/add", test_add);
    g_test_add_func("/git_stats/months_backwards", test_months_backwards);
    g_test_add_func("/git_stats/read", test_read);
    return g_test_run();
}
