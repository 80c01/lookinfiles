/* Unit tests for src/git/git_code_stats.c: the languages, the counting of
 * lines, then the reading of a repository made with libgit2 (no git
 * program needed). */
#include "git/git_code_stats.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

static void test_language(void) {
    g_assert_cmpstr(git_code_language("src/main.c", NULL, 0), ==, "C");
    g_assert_cmpstr(git_code_language("src/x.H", NULL, 0), ==, "C");
    g_assert_cmpstr(git_code_language("a/b/view.tsx", NULL, 0), ==, "TSX");
    g_assert_cmpstr(git_code_language("Makefile", NULL, 0), ==, "Makefile");
    g_assert_cmpstr(git_code_language("sub/CMakeLists.txt", NULL, 0), ==, "CMake");
    g_assert_cmpstr(git_code_language("notes.txt", NULL, 0), ==, "Text");
    g_assert_cmpstr(git_code_language(".bashrc", NULL, 0), ==, "Shell");
    g_assert_null(git_code_language("data.unknownext", NULL, 0));
    g_assert_null(git_code_language(".gitignore", NULL, 0));

    /* The #! line of a file without an extension */
    const gchar* py = "#!/usr/bin/env python3\nprint(1)\n";
    g_assert_cmpstr(git_code_language("tools/run", py, strlen(py)), ==, "Python");
    const gchar* sh = "#!/bin/sh -e\necho\n";
    g_assert_cmpstr(git_code_language("configure", sh, strlen(sh)), ==, "Shell");
    const gchar* env_s = "#!/usr/bin/env -S node --harmony\n";
    g_assert_cmpstr(git_code_language("cli", env_s, strlen(env_s)), ==, "JavaScript");
    const gchar* none = "#!/opt/unknown\n";
    g_assert_null(git_code_language("cli", none, strlen(none)));
    /* An extension wins over the #! line */
    g_assert_cmpstr(git_code_language("x.rb", py, strlen(py)), ==, "Ruby");
}

static void test_generated(void) {
    g_assert_true(git_code_path_generated("web/node_modules/a/index.js"));
    g_assert_true(git_code_path_generated("vendor/lib.c"));
    g_assert_true(git_code_path_generated("Cargo.lock"));
    g_assert_true(git_code_path_generated("static/app.min.js"));
    g_assert_false(git_code_path_generated("src/vendor.c")); /* a file, not the folder */
    g_assert_false(git_code_path_generated("src/main.c"));
}

static void test_lines(void) {
    guint64 lines, nonblank;
    git_code_count_lines("", 0, &lines, &nonblank);
    g_assert_cmpuint(lines, ==, 0);
    g_assert_cmpuint(nonblank, ==, 0);
    const gchar* text = "a\n\n  \t\nb";
    git_code_count_lines(text, strlen(text), &lines, &nonblank);
    g_assert_cmpuint(lines, ==, 4);
    g_assert_cmpuint(nonblank, ==, 2);
    const gchar* crlf = "x\r\n\r\ny\r\n";
    git_code_count_lines(crlf, strlen(crlf), &lines, &nonblank);
    g_assert_cmpuint(lines, ==, 3);
    g_assert_cmpuint(nonblank, ==, 2);

    g_assert_false(git_code_is_binary("abc", 3));
    g_assert_true(git_code_is_binary("a\0c", 3));
}

static void test_add(void) {
    GitCodeStats* s = git_code_stats_new();
    git_code_stats_add(s, "a.c", "int a;\n\nint b;\n", 15);
    git_code_stats_add(s, "b.h", "int c;\n", 7);
    git_code_stats_add(s, "x.py", "a\nb\nc\nd\ne\n", 10);
    git_code_stats_add(s, "y.unknownext", "1\n2\n3\n4\n5\n6\n", 12);
    git_code_stats_add(s, "logo.png", "\x89PNG\0\0", 6);
    git_code_stats_add(s, "yarn.lock", "x\n", 2);
    git_code_stats_finish(s);

    g_assert_cmpuint(s->files, ==, 4);
    g_assert_cmpuint(s->lines, ==, 15);
    g_assert_cmpuint(s->nonblank, ==, 14);
    g_assert_cmpuint(s->bytes, ==, 44);
    g_assert_cmpuint(s->binary, ==, 1);
    g_assert_cmpuint(s->generated, ==, 1);

    /* Most lines first, the files of no known language last */
    g_assert_cmpuint(s->languages->len, ==, 3);
    const GitLangStats* py = g_ptr_array_index(s->languages, 0);
    g_assert_cmpstr(py->name, ==, "Python");
    g_assert_cmpuint(py->lines, ==, 5);
    const GitLangStats* c = g_ptr_array_index(s->languages, 1);
    g_assert_cmpstr(c->name, ==, "C");
    g_assert_cmpuint(c->files, ==, 2);
    g_assert_cmpuint(c->lines, ==, 4);
    g_assert_cmpuint(c->nonblank, ==, 3);
    g_assert_cmpuint(c->bytes, ==, 22);
    const GitLangStats* other = g_ptr_array_index(s->languages, 2);
    g_assert_null(other->name);
    g_assert_cmpuint(other->lines, ==, 6);
    git_code_stats_free(s);
}

static void remove_tree(const gchar* path) {
    GStatBuf st;
    if (g_lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        GDir* dir = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (dir != NULL && (name = g_dir_read_name(dir)) != NULL) {
            gchar* full = g_build_filename(path, name, NULL);
            remove_tree(full);
            g_free(full);
        }
        if (dir != NULL) g_dir_close(dir);
    }
    g_remove(path);
}

static void put(const gchar* top, const gchar* rel, const gchar* data, gsize len) {
    gchar* full = g_build_filename(top, rel, NULL);
    gchar* parent = g_path_get_dirname(full);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
    g_assert_true(g_file_set_contents(full, data, (gssize)len, NULL));
    g_free(parent);
    g_free(full);
}

/* Commit every path of the index (with paths added first) */
static void commit_all(git_repository* repo, const gchar* const* paths) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    for (gint i = 0; paths[i] != NULL; i++) g_assert_cmpint(git_index_add_bypath(index, paths[i]), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_oid tree_id, commit_id;
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    git_tree* tree = NULL;
    g_assert_cmpint(git_tree_lookup(&tree, repo, &tree_id), ==, 0);
    git_signature* sig = NULL;
    g_assert_cmpint(git_signature_new(&sig, "Ada", "ada@example.org", 1794787200, 0), ==, 0);
    g_assert_cmpint(git_commit_create_v(&commit_id, repo, "HEAD", sig, sig, NULL, "files", tree, 0), ==, 0);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

static void test_read(void) {
    git_libgit2_init();
    gchar* dir = g_dir_make_tmp("git-code-stats-XXXXXX", NULL);
    g_assert_nonnull(dir);
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);

    /* Without commits: empty */
    gboolean unchanged = TRUE;
    GitCodeStats* empty = git_code_stats_read(dir, NULL, &unchanged, NULL);
    g_assert_nonnull(empty);
    g_assert_false(unchanged);
    g_assert_cmpuint(empty->files, ==, 0);
    g_assert_null(empty->tree);
    git_code_stats_free(empty);

    put(dir, ".gitattributes", "gen/*.c linguist-generated\n", 27);
    put(dir, "src/main.c", "int main(void) {\n\n    return 0;\n}\n", 34);
    put(dir, "gen/table.c", "int t[] = {1};\n", 15);
    put(dir, "img.bin", "a\0b", 3);
    put(dir, "untracked.c", "int u;\n", 7);
    const gchar* const paths[] = {".gitattributes", "src/main.c", "gen/table.c", "img.bin", NULL};
    commit_all(repo, paths);

    GitCodeStats* s = git_code_stats_read(dir, NULL, &unchanged, NULL);
    g_assert_nonnull(s);
    g_assert_false(unchanged);
    g_assert_nonnull(s->tree);
    /* src/main.c and .gitattributes (no known language); not the untracked file */
    g_assert_cmpuint(s->files, ==, 2);
    g_assert_cmpuint(s->generated, ==, 1);
    g_assert_cmpuint(s->binary, ==, 1);
    const GitLangStats* c = g_hash_table_lookup(s->by_name, "C");
    g_assert_nonnull(c);
    g_assert_cmpuint(c->files, ==, 1);
    g_assert_cmpuint(c->lines, ==, 4);
    g_assert_cmpuint(c->nonblank, ==, 3);

    /* The same tree again: not read */
    GitCodeStats* again = git_code_stats_read(dir, s->tree, &unchanged, NULL);
    g_assert_null(again);
    g_assert_true(unchanged);
    git_code_stats_free(s);

    /* Cancelled: NULL */
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(git_code_stats_read(dir, NULL, &unchanged, cancel));
    g_assert_false(unchanged);
    g_object_unref(cancel);

    g_assert_null(git_code_stats_read("/nonexistent/repository", NULL, NULL, NULL));

    git_repository_free(repo);
    remove_tree(dir);
    g_free(dir);
    git_libgit2_shutdown();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_code_stats/language", test_language);
    g_test_add_func("/git_code_stats/generated", test_generated);
    g_test_add_func("/git_code_stats/lines", test_lines);
    g_test_add_func("/git_code_stats/add", test_add);
    g_test_add_func("/git_code_stats/read", test_read);
    return g_test_run();
}
