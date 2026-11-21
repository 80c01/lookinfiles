/* Unit tests for src/git/git_stash_tags.c. Each test works in a fresh
 * temporary directory; repositories, tags and stashes are made with libgit2
 * (no git program needed). */
#include "git/git_stash_tags.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    gchar* dir; /* the work tree */
    git_repository* repo;
} Fixture;

static void put(Fixture* fx, const gchar* rel, const gchar* text) {
    gchar* full = g_build_filename(fx->dir, rel, NULL);
    g_assert_true(g_file_set_contents(full, text, -1, NULL));
    g_free(full);
}

static void stage(Fixture* fx, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, fx->repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

/* Commit the index on HEAD at time, with message; its id in *id */
static void commit(Fixture* fx, gint64 time, const gchar* message, git_oid* id) {
    git_index* index = NULL;
    git_oid tree_id;
    git_tree* tree = NULL;
    git_signature* sig = NULL;
    git_object* parent = NULL;
    g_assert_cmpint(git_repository_index(&index, fx->repo), ==, 0);
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    g_assert_cmpint(git_tree_lookup(&tree, fx->repo, &tree_id), ==, 0);
    g_assert_cmpint(git_signature_new(&sig, "Test", "test@example.com", time, 0), ==, 0);
    gint n = git_revparse_single(&parent, fx->repo, "HEAD") == 0 ? 1 : 0;
    const git_commit* parents[1] = { (const git_commit*)parent };
    g_assert_cmpint(git_commit_create(id, fx->repo, "HEAD", sig, sig, NULL, message, tree, (size_t)n, parents), ==,
                    0);
    git_object_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
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

static void fixture_setup(Fixture* fx, gconstpointer data) {
    (void)data;
    git_libgit2_init();
    gchar* tmp = g_dir_make_tmp("git_stash_tags_test_XXXXXX", NULL);
    g_assert_nonnull(tmp);
    fx->dir = g_canonicalize_filename(tmp, NULL);
    g_free(tmp);
    g_assert_cmpint(git_repository_init(&fx->repo, fx->dir, 0), ==, 0);
}

static void fixture_teardown(Fixture* fx, gconstpointer data) {
    (void)data;
    git_repository_free(fx->repo);
    remove_tree(fx->dir);
    g_free(fx->dir);
    git_libgit2_shutdown();
}

/* Two commits: the first tagged lightly, the second with an annotated tag */
static void test_tags(Fixture* fx, gconstpointer data) {
    (void)data;
    git_oid first, second, tag_id;
    put(fx, "a.txt", "one\n");
    stage(fx, "a.txt");
    commit(fx, 1700000000, "First commit\n\nWith a body\n", &first);
    put(fx, "a.txt", "two\n");
    stage(fx, "a.txt");
    commit(fx, 1700001000, "Second commit\n", &second);

    git_object* obj = NULL;
    g_assert_cmpint(git_object_lookup(&obj, fx->repo, &first, GIT_OBJECT_COMMIT), ==, 0);
    g_assert_cmpint(git_tag_create_lightweight(&tag_id, fx->repo, "v0.1", obj, 0), ==, 0);
    git_object_free(obj);
    git_signature* sig = NULL;
    g_assert_cmpint(git_signature_new(&sig, "Tagger", "tagger@example.com", 1700002000, 60), ==, 0);
    g_assert_cmpint(git_object_lookup(&obj, fx->repo, &second, GIT_OBJECT_COMMIT), ==, 0);
    g_assert_cmpint(git_tag_create(&tag_id, fx->repo, "v1.0", obj, sig, "Release 1.0\n\nNotes\n", 0), ==, 0);
    git_object_free(obj);
    git_signature_free(sig);

    GPtrArray* tags = git_read_tags(fx->dir);
    g_assert_nonnull(tags);
    g_assert_cmpuint(tags->len, ==, 2);
    const GitTagInfo* newest = g_ptr_array_index(tags, 0);
    g_assert_cmpstr(newest->name, ==, "v1.0");
    g_assert_true(newest->annotated);
    g_assert_cmpstr(newest->hash, ==, git_oid_tostr_s(&second));
    g_assert_nonnull(newest->short_hash);
    g_assert_cmpstr(newest->subject, ==, "Release 1.0");
    g_assert_cmpstr(newest->message, ==, "Release 1.0\n\nNotes");
    g_assert_cmpint(newest->time, ==, 1700002000);
    const GitTagInfo* light = g_ptr_array_index(tags, 1);
    g_assert_cmpstr(light->name, ==, "v0.1");
    g_assert_false(light->annotated);
    g_assert_cmpstr(light->hash, ==, git_oid_tostr_s(&first));
    g_assert_cmpstr(light->subject, ==, "First commit"); /* the commit's */
    g_assert_cmpint(light->time, ==, 1700000000);
    g_ptr_array_unref(tags);

    gchar* shown = git_show_tag(fx->dir, "v1.0");
    g_assert_nonnull(shown);
    g_assert_true(g_str_has_prefix(shown, "tag v1.0\nTagger: Tagger <tagger@example.com>\nDate:   "));
    g_assert_nonnull(strstr(shown, "    Release 1.0\n    \n    Notes\n"));
    g_assert_nonnull(strstr(shown, "commit "));
    g_assert_nonnull(strstr(shown, "-one\n+two\n"));
    g_free(shown);
    shown = git_show_tag(fx->dir, "v0.1");
    g_assert_nonnull(shown);
    g_assert_true(g_str_has_prefix(shown, "commit ")); /* no tag header */
    g_free(shown);
    g_assert_null(git_show_tag(fx->dir, "missing"));
}

/* Two stashes, the newest with untracked files */
static void test_stashes(Fixture* fx, gconstpointer data) {
    (void)data;
    git_oid id;
    put(fx, "a.txt", "one\n");
    stage(fx, "a.txt");
    commit(fx, 1700000000, "Base\n", &id);

    git_signature* sig = NULL;
    g_assert_cmpint(git_signature_new(&sig, "Test", "test@example.com", 1700003000, 0), ==, 0);
    put(fx, "a.txt", "changed\n");
    g_assert_cmpint(git_stash_save(&id, fx->repo, sig, "first try", GIT_STASH_DEFAULT), ==, 0);
    put(fx, "a.txt", "again\n");
    put(fx, "new.txt", "brand new\n");
    g_assert_cmpint(git_stash_save(&id, fx->repo, sig, "second try", GIT_STASH_INCLUDE_UNTRACKED), ==, 0);
    git_signature_free(sig);

    GPtrArray* stashes = git_read_stashes(fx->dir);
    g_assert_nonnull(stashes);
    g_assert_cmpuint(stashes->len, ==, 2);
    const GitStashInfo* newest = g_ptr_array_index(stashes, 0);
    g_assert_cmpuint(newest->index, ==, 0);
    g_assert_cmpstr(newest->name, ==, "stash@{0}");
    g_assert_nonnull(strstr(newest->message, "second try"));
    g_assert_cmpstr(newest->hash, ==, git_oid_tostr_s(&id));
    g_assert_nonnull(newest->untracked_hash);
    const GitStashInfo* older = g_ptr_array_index(stashes, 1);
    g_assert_cmpstr(older->name, ==, "stash@{1}");
    g_assert_nonnull(strstr(older->message, "first try"));
    g_assert_null(older->untracked_hash);

    gchar* shown = git_show_stash(fx->dir, older->hash, older->untracked_hash);
    g_assert_nonnull(shown);
    g_assert_nonnull(strstr(shown, "-one\n+changed\n"));
    g_free(shown);
    shown = git_show_stash(fx->dir, newest->hash, newest->untracked_hash);
    g_assert_nonnull(shown);
    g_assert_nonnull(strstr(shown, "-one\n+again\n"));
    g_assert_nonnull(strstr(shown, "+brand new\n")); /* the untracked file */
    g_free(shown);
    g_ptr_array_unref(stashes);
}

/* Nothing stashed or tagged: empty lists */
static void test_none(Fixture* fx, gconstpointer data) {
    (void)data;
    git_oid id;
    put(fx, "a.txt", "one\n");
    stage(fx, "a.txt");
    commit(fx, 1700000000, "Base\n", &id);
    GPtrArray* stashes = git_read_stashes(fx->dir);
    GPtrArray* tags = git_read_tags(fx->dir);
    g_assert_nonnull(stashes);
    g_assert_nonnull(tags);
    g_assert_cmpuint(stashes->len, ==, 0);
    g_assert_cmpuint(tags->len, ==, 0);
    g_ptr_array_unref(stashes);
    g_ptr_array_unref(tags);
}

static void test_not_a_repo(void) {
    gchar* tmp = g_dir_make_tmp("git_stash_tags_none_XXXXXX", NULL);
    g_assert_null(git_read_stashes(tmp));
    g_assert_null(git_read_tags(tmp));
    g_assert_null(git_show_tag(tmp, "v1"));
    g_assert_null(git_read_stashes(NULL));
    remove_tree(tmp);
    g_free(tmp);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add("/git_stash_tags/tags", Fixture, NULL, fixture_setup, test_tags, fixture_teardown);
    g_test_add("/git_stash_tags/stashes", Fixture, NULL, fixture_setup, test_stashes, fixture_teardown);
    g_test_add("/git_stash_tags/none", Fixture, NULL, fixture_setup, test_none, fixture_teardown);
    g_test_add_func("/git_stash_tags/not_a_repo", test_not_a_repo);
    return g_test_run();
}
