/* Unit tests for src/git/git_refs.c (libgit2 ref/tag decorations). */
#include "git/git_refs.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    gchar* dir;
} Fixture;

static void fixture_setup(Fixture* fx, gconstpointer data) {
    (void)data;
    GError* err = NULL;
    gchar* tmp = g_dir_make_tmp("git_refs_test_XXXXXX", &err);
    g_assert_no_error(err);
    fx->dir = g_canonicalize_filename(tmp, NULL);
    g_free(tmp);
    git_libgit2_init();
}

static void remove_tree(const gchar* path) {
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
    g_remove(path);
}

static void fixture_teardown(Fixture* fx, gconstpointer data) {
    (void)data;
    remove_tree(fx->dir);
    g_free(fx->dir);
    git_libgit2_shutdown();
}

static git_oid commit_file(git_repository* repo, const gchar* repo_dir,
                           const gchar* rel, const gchar* text, const gchar* msg) {
    gchar* full = g_build_filename(repo_dir, rel, NULL);
    g_assert_true(g_file_set_contents(full, text, -1, NULL));
    g_free(full);

    git_index* idx = NULL;
    g_assert_cmpint(git_repository_index(&idx, repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(idx, rel), ==, 0);
    g_assert_cmpint(git_index_write(idx), ==, 0);

    git_oid tree_oid;
    g_assert_cmpint(git_index_write_tree(&tree_oid, idx), ==, 0);
    git_index_free(idx);

    git_tree* tree = NULL;
    g_assert_cmpint(git_tree_lookup(&tree, repo, &tree_oid), ==, 0);

    git_signature* sig = NULL;
    g_assert_cmpint(git_signature_new(&sig, "Ada", "ada@example.org", 1700000000, 0), ==, 0);

    git_commit* parent = NULL;
    git_oid head_oid;
    int n_parents = 0;
    if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
        g_assert_cmpint(git_commit_lookup(&parent, repo, &head_oid), ==, 0);
        n_parents = 1;
    }

    git_oid commit_oid;
    const git_commit* parents[1] = {parent};
    g_assert_cmpint(git_commit_create(&commit_oid, repo, "HEAD", sig, sig,
                                      NULL, msg, tree, (size_t)n_parents, parents), ==, 0);

    if (parent != NULL) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    return commit_oid;
}

static void test_display_names(void) {
    GitRefInfo head = {.name = "main", .kind = GIT_REF_HEAD, .detached = FALSE};
    gchar* s = git_ref_display_name(&head);
    g_assert_cmpstr(s, ==, "HEAD \xe2\x86\x92 main");
    g_free(s);

    GitRefInfo det = {.name = "HEAD", .kind = GIT_REF_HEAD, .detached = TRUE};
    s = git_ref_display_name(&det);
    g_assert_cmpstr(s, ==, "HEAD");
    g_free(s);

    GitRefInfo br = {.name = "feat/ui", .kind = GIT_REF_BRANCH, .detached = FALSE};
    s = git_ref_display_name(&br);
    g_assert_cmpstr(s, ==, "feat/ui");
    g_free(s);

    GitRefInfo tag = {.name = "v0.20.0", .kind = GIT_REF_TAG, .detached = FALSE};
    s = git_ref_display_name(&tag);
    g_assert_cmpstr(s, ==, "tag: v0.20.0");
    g_free(s);

    GitRefInfo rem = {.name = "origin/main", .kind = GIT_REF_REMOTE, .detached = FALSE};
    s = git_ref_display_name(&rem);
    g_assert_cmpstr(s, ==, "origin/main");
    g_free(s);
}

static void test_branches_and_tags(Fixture* fx, gconstpointer data) {
    (void)data;
    g_assert_null(git_read_refs(NULL));
    g_assert_null(git_read_refs(fx->dir));

    gchar* repo_dir = g_build_filename(fx->dir, "repo", NULL);
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, repo_dir, 0), ==, 0);

    /* Unborn repo: empty map */
    GHashTable* refs = git_read_refs(repo_dir);
    g_assert_nonnull(refs);
    g_assert_cmpuint(g_hash_table_size(refs), ==, 0);
    g_hash_table_unref(refs);

    /* Two commits on default branch (force "main") */
    g_assert_cmpint(git_repository_set_head(repo, "refs/heads/main"), ==, 0);
    git_oid c1 = commit_file(repo, repo_dir, "a.txt", "1\n", "First");
    git_oid c2 = commit_file(repo, repo_dir, "a.txt", "2\n", "Second");

    gchar sha1[GIT_OID_SHA1_HEXSIZE + 1];
    gchar sha2[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(sha1, sizeof(sha1), &c1);
    git_oid_tostr(sha2, sizeof(sha2), &c2);

    git_commit* obj1 = NULL;
    git_commit* obj2 = NULL;
    g_assert_cmpint(git_commit_lookup(&obj1, repo, &c1), ==, 0);
    g_assert_cmpint(git_commit_lookup(&obj2, repo, &c2), ==, 0);

    /* Local branches on c2 and c1 */
    git_reference* out_ref = NULL;
    g_assert_cmpint(git_branch_create(&out_ref, repo, "beta", obj2, 0), ==, 0);
    git_reference_free(out_ref);
    g_assert_cmpint(git_branch_create(&out_ref, repo, "alpha", obj2, 0), ==, 0);
    git_reference_free(out_ref);
    g_assert_cmpint(git_branch_create(&out_ref, repo, "old", obj1, 0), ==, 0);
    git_reference_free(out_ref);

    /* Lightweight tag on c1, annotated tag on c2 */
    git_oid tag_oid;
    g_assert_cmpint(git_tag_create_lightweight(&tag_oid, repo, "v0.1.0", (git_object*)obj1, 0), ==, 0);
    git_signature* sig = NULL;
    g_assert_cmpint(git_signature_new(&sig, "Ada", "ada@example.org", 1700000100, 0), ==, 0);
    g_assert_cmpint(git_tag_create(&tag_oid, repo, "v0.2.0", (git_object*)obj2, sig, "Release 0.2", 0), ==, 0);
    git_signature_free(sig);

    /* Remote branch and symbolic remote HEAD (should be skipped) */
    g_assert_cmpint(git_reference_create(&out_ref, repo, "refs/remotes/origin/main", &c2, 0, NULL), ==, 0);
    git_reference_free(out_ref);
    g_assert_cmpint(git_reference_symbolic_create(&out_ref, repo, "refs/remotes/origin/HEAD",
                                                  "refs/remotes/origin/main", 0, NULL), ==, 0);
    git_reference_free(out_ref);

    refs = git_read_refs(repo_dir);
    g_assert_nonnull(refs);
    g_assert_cmpuint(g_hash_table_size(refs), ==, 2);

    /* Check c2 ordering: HEAD (main), local branches (alpha, beta), tag (v0.2.0), remote (origin/main) */
    GPtrArray* r2 = g_hash_table_lookup(refs, sha2);
    g_assert_nonnull(r2);
    g_assert_cmpuint(r2->len, ==, 5);

    GitRefInfo* i0 = r2->pdata[0];
    g_assert_cmpint(i0->kind, ==, GIT_REF_HEAD);
    g_assert_false(i0->detached);
    g_assert_cmpstr(i0->name, ==, "main");

    GitRefInfo* i1 = r2->pdata[1];
    g_assert_cmpint(i1->kind, ==, GIT_REF_BRANCH);
    g_assert_cmpstr(i1->name, ==, "alpha");

    GitRefInfo* i2 = r2->pdata[2];
    g_assert_cmpint(i2->kind, ==, GIT_REF_BRANCH);
    g_assert_cmpstr(i2->name, ==, "beta");

    GitRefInfo* i3 = r2->pdata[3];
    g_assert_cmpint(i3->kind, ==, GIT_REF_TAG);
    g_assert_cmpstr(i3->name, ==, "v0.2.0");

    GitRefInfo* i4 = r2->pdata[4];
    g_assert_cmpint(i4->kind, ==, GIT_REF_REMOTE);
    g_assert_cmpstr(i4->name, ==, "origin/main");

    /* Check c1: local branch "old" + lightweight tag "v0.1.0" */
    GPtrArray* r1 = g_hash_table_lookup(refs, sha1);
    g_assert_nonnull(r1);
    g_assert_cmpuint(r1->len, ==, 2);
    g_assert_cmpint(((GitRefInfo*)r1->pdata[0])->kind, ==, GIT_REF_BRANCH);
    g_assert_cmpstr(((GitRefInfo*)r1->pdata[0])->name, ==, "old");
    g_assert_cmpint(((GitRefInfo*)r1->pdata[1])->kind, ==, GIT_REF_TAG);
    g_assert_cmpstr(((GitRefInfo*)r1->pdata[1])->name, ==, "v0.1.0");
    g_hash_table_unref(refs);

    /* Detached HEAD on c1: HEAD detached on c1, main becomes regular local branch on c2 */
    g_assert_cmpint(git_repository_set_head_detached(repo, &c1), ==, 0);
    refs = git_read_refs(repo_dir);
    r1 = g_hash_table_lookup(refs, sha1);
    g_assert_nonnull(r1);
    g_assert_cmpuint(r1->len, ==, 3);
    g_assert_cmpint(((GitRefInfo*)r1->pdata[0])->kind, ==, GIT_REF_HEAD);
    g_assert_true(((GitRefInfo*)r1->pdata[0])->detached);
    g_assert_cmpstr(((GitRefInfo*)r1->pdata[0])->name, ==, "HEAD");

    r2 = g_hash_table_lookup(refs, sha2);
    g_assert_nonnull(r2);
    g_assert_cmpint(((GitRefInfo*)r2->pdata[0])->kind, ==, GIT_REF_BRANCH);
    g_assert_cmpstr(((GitRefInfo*)r2->pdata[0])->name, ==, "alpha");
    g_hash_table_unref(refs);

    git_commit_free(obj1);
    git_commit_free(obj2);
    git_repository_free(repo);
    g_free(repo_dir);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_refs/display_names", test_display_names);
    g_test_add("/git_refs/branches_and_tags", Fixture, NULL, fixture_setup,
               test_branches_and_tags, fixture_teardown);
    return g_test_run();
}
