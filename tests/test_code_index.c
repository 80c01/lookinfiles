/* Unit tests for src/code_index.c: project files and the definition lookup
 * of the built-in code index, on temporary projects. */
#include "code_index.h"

#include <glib/gstdio.h>
#include <string.h>

static void write_file(const gchar* root, const gchar* rel, const gchar* text) {
    gchar* path = g_build_filename(root, rel, NULL);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
    g_free(path);
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK)) {
                remove_tree(child);
            } else {
                g_unlink(child);
            }
            g_free(child);
        }
        g_dir_close(d);
    }
    g_rmdir(path);
}

/* A small C project (plus a Python file of another family) */
static gchar* make_project(void) {
    gchar* root = g_dir_make_tmp("code_index_XXXXXX", NULL);
    g_assert_nonnull(root);
    write_file(root, "include/shop.h", "#ifndef SHOP_H\n#define SHOP_H\nint shop_total(int n);\n"
                                       "typedef struct { int count; } Shop;\n#endif\n");
    write_file(root, "src/shop.c", "#include \"shop.h\"\nint shop_total(int n)\n{\n    return n * 2;\n}\n");
    write_file(root, "src/main.c", "#include \"shop.h\"\nint main(void)\n{\n    return shop_total(3);\n}\n");
    write_file(root, "src/extra/other.c", "static int shop_total(int n) { return n; }\n");
    write_file(root, "tools/gen.py", "def shop_total(n):\n    return n\n");
    write_file(root, "build/generated.c", "int shop_total(int n) { return 0; }\n");
    write_file(root, ".cache/hidden.c", "int shop_total(int n) { return 0; }\n");
    return root;
}

static gchar* rel_path(const gchar* root, const gchar* path) {
    g_assert_true(g_str_has_prefix(path, root));
    return g_strdup(path + strlen(root) + 1);
}

/* Relative paths of a file list, sorted, joined by spaces */
static gchar* list_of(const gchar* root, GPtrArray* files) {
    GPtrArray* rel = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < files->len; i++) g_ptr_array_add(rel, rel_path(root, g_ptr_array_index(files, i)));
    g_ptr_array_sort_values(rel, (GCompareFunc)g_strcmp0);
    g_ptr_array_add(rel, NULL);
    gchar* s = g_strjoinv(" ", (gchar**)rel->pdata);
    g_ptr_array_unref(rel);
    return s;
}

/* Matches as "relpath:line" (1-based, "!" for a declaration) */
static gchar* matches_of(const gchar* root, GPtrArray* matches) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < matches->len; i++) {
        CodeMatch* m = g_ptr_array_index(matches, i);
        gchar* rel = rel_path(root, m->path);
        if (s->len > 0) g_string_append_c(s, ' ');
        g_string_append_printf(s, "%s:%d%s", rel, m->symbol->line + 1, m->symbol->declaration ? "!" : "");
        g_free(rel);
    }
    return g_string_free(s, FALSE);
}

static void test_project_root(void) {
    gchar* root = make_project();
    gchar* file = g_build_filename(root, "src", "main.c", NULL);
    gchar* got = code_index_project_root(file);
    gchar* dir = g_build_filename(root, "src", NULL);
    /* Not a repository: the directory of the file */
    g_assert_cmpstr(got, ==, dir);
    g_free(got);
    g_free(dir);
    g_free(file);
    remove_tree(root);
    g_free(root);
}

static void test_walk_files(void) {
    gchar* root = make_project();
    gboolean truncated = TRUE;
    GPtrArray* files = code_index_project_files(root, CODE_LANG_C, NULL, &truncated);
    gchar* got = list_of(root, files);
    /* Hidden and build directories are skipped, Python is another family */
    g_assert_cmpstr(got, ==, "include/shop.h src/extra/other.c src/main.c src/shop.c");
    g_assert_false(truncated);
    g_free(got);
    g_ptr_array_unref(files);
    files = code_index_project_files(root, CODE_LANG_PYTHON, NULL, NULL);
    got = list_of(root, files);
    g_assert_cmpstr(got, ==, "tools/gen.py");
    g_free(got);
    g_ptr_array_unref(files);
    /* Every language (quick open, text search), still without hidden and build directories */
    files = code_index_all_files(root, NULL, NULL);
    got = list_of(root, files);
    g_assert_cmpstr(got, ==, "include/shop.h src/extra/other.c src/main.c src/shop.c tools/gen.py");
    g_free(got);
    g_ptr_array_unref(files);
    remove_tree(root);
    g_free(root);
}

static void test_git_files(void) {
    gchar* git = g_find_program_in_path("git");
    if (git == NULL) {
        g_test_skip("git is not installed");
        return;
    }
    g_free(git);
    gchar* root = make_project();
    write_file(root, ".gitignore", "src/extra/\n");
    gchar* argv[] = {"git", "init", "-q", NULL};
    gint status = 0;
    g_assert_true(g_spawn_sync(root, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                               NULL, NULL, &status, NULL));
    g_assert_true(g_spawn_check_wait_status(status, NULL));
    gchar* file = g_build_filename(root, "src", "main.c", NULL);
    gchar* project = code_index_project_root(file);
    gchar* canon = g_canonicalize_filename(root, NULL);
    g_assert_cmpstr(project, ==, canon);
    GPtrArray* files = code_index_project_files(project, CODE_LANG_C, NULL, NULL);
    gchar* got = list_of(project, files);
    /* Untracked files are listed, ignored ones are not; build/ is listed
     * (not ignored by this repository) */
    g_assert_cmpstr(got, ==, ".cache/hidden.c build/generated.c include/shop.h src/main.c src/shop.c");
    g_free(got);
    g_ptr_array_unref(files);
    g_free(canon);
    g_free(project);
    g_free(file);
    remove_tree(root);
    g_free(root);
}

static void test_find_ranking(void) {
    gchar* root = make_project();
    GPtrArray* files = code_index_project_files(root, CODE_LANG_C, NULL, NULL);
    gchar* origin = g_build_filename(root, "src", "main.c", NULL);
    CodeQuery q = {"shop_total", CODE_LANG_C, FALSE, origin, NULL, NULL};
    GPtrArray* m = code_index_find(&q, files, NULL, NULL);
    gchar* got = matches_of(root, m);
    /* Definitions first: same directory, then elsewhere; then the prototype */
    g_assert_cmpstr(got, ==, "src/shop.c:2 src/extra/other.c:1 include/shop.h:3!");
    g_free(got);
    g_ptr_array_unref(m);
    /* Declaration wanted: the prototype first */
    q.declaration = TRUE;
    m = code_index_find(&q, files, NULL, NULL);
    got = matches_of(root, m);
    g_assert_cmpstr(got, ==, "include/shop.h:3! src/shop.c:2 src/extra/other.c:1");
    g_free(got);
    g_ptr_array_unref(m);
    /* Fields and types */
    CodeQuery q2 = {"count", CODE_LANG_C, FALSE, origin, NULL, NULL};
    m = code_index_find(&q2, files, NULL, NULL);
    got = matches_of(root, m);
    g_assert_cmpstr(got, ==, "include/shop.h:4");
    g_free(got);
    g_ptr_array_unref(m);
    g_ptr_array_unref(files);
    g_free(origin);
    remove_tree(root);
    g_free(root);
}

static void test_find_open_buffers(void) {
    gchar* root = make_project();
    GPtrArray* files = code_index_project_files(root, CODE_LANG_C, NULL, NULL);
    gchar* origin = g_build_filename(root, "src", "main.c", NULL);
    gchar* shop = g_build_filename(root, "src", "shop.c", NULL);
    /* Unsaved buffers are used instead of the files on disk */
    GHashTable* texts = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(texts, origin, "static int helper(void) { return 1; }\nint main(void) { return helper(); }\n");
    g_hash_table_insert(texts, shop, "/* moved */\n\n\nint shop_total(int n) { return n; }\n");
    CodeQuery q = {"helper", CODE_LANG_C, FALSE, origin, texts, NULL};
    GPtrArray* m = code_index_find(&q, files, NULL, NULL);
    gchar* got = matches_of(root, m);
    g_assert_cmpstr(got, ==, "src/main.c:1");
    g_free(got);
    g_ptr_array_unref(m);
    q.name = "shop_total";
    m = code_index_find(&q, files, NULL, NULL);
    got = matches_of(root, m);
    g_assert_cmpstr(got, ==, "src/shop.c:4 src/extra/other.c:1 include/shop.h:3!");
    g_free(got);
    g_ptr_array_unref(m);
    /* An origin outside the list (new file) is scanned too */
    gchar* fresh = g_build_filename(root, "notes", "draft.c", NULL);
    g_hash_table_insert(texts, fresh, "int draft_value = 3;\n");
    CodeQuery q3 = {"draft_value", CODE_LANG_C, FALSE, fresh, texts, NULL};
    m = code_index_find(&q3, files, NULL, NULL);
    g_assert_cmpuint(m->len, ==, 1);
    g_ptr_array_unref(m);
    g_hash_table_destroy(texts);
    g_ptr_array_unref(files);
    g_free(fresh);
    g_free(shop);
    g_free(origin);
    remove_tree(root);
    g_free(root);
}

static void test_find_cancelled(void) {
    gchar* root = make_project();
    GPtrArray* files = code_index_project_files(root, CODE_LANG_C, NULL, NULL);
    GCancellable* c = g_cancellable_new();
    g_cancellable_cancel(c);
    CodeQuery q = {"shop_total", CODE_LANG_C, FALSE, NULL, NULL, NULL};
    GPtrArray* m = code_index_find(&q, files, c, NULL);
    g_assert_cmpuint(m->len, ==, 0);
    g_ptr_array_unref(m);
    g_object_unref(c);
    g_ptr_array_unref(files);
    remove_tree(root);
    g_free(root);
}

static void test_references(void) {
    gchar* root = make_project();
    GPtrArray* files = code_index_project_files(root, CODE_LANG_C, NULL, NULL);
    gchar* origin = g_build_filename(root, "src", "main.c", NULL);
    gboolean truncated = TRUE;
    GPtrArray* refs = code_index_references("shop_total", CODE_LANG_C, origin, NULL, files, NULL, &truncated);
    g_assert_false(truncated);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < refs->len; i++) {
        CodeReference* r = g_ptr_array_index(refs, i);
        gchar* rel = rel_path(root, r->path);
        g_string_append_printf(s, "%s:%d:%d ", rel, r->line + 1, r->column);
        g_free(rel);
    }
    /* The origin first, then by path */
    g_assert_cmpstr(s->str, ==, "src/main.c:4:11 include/shop.h:3:4 src/extra/other.c:1:11 src/shop.c:2:4 ");
    CodeReference* first = g_ptr_array_index(refs, 0);
    g_assert_cmpstr(first->text, ==, "    return shop_total(3);");
    g_string_free(s, TRUE);
    g_ptr_array_unref(refs);
    /* Whole words only; open buffers instead of files */
    GHashTable* texts = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(texts, origin, "int shop_total_x;\nshop_total();\tshop_total;\n");
    refs = code_index_references("shop_total", CODE_LANG_C, origin, texts, NULL, NULL, NULL);
    g_assert_cmpuint(refs->len, ==, 2);
    first = g_ptr_array_index(refs, 1);
    g_assert_cmpint(first->line, ==, 1);
    g_assert_cmpint(first->column, ==, 14);
    g_assert_cmpstr(first->text, ==, "shop_total(); shop_total;");
    g_ptr_array_unref(refs);
    g_hash_table_destroy(texts);
    g_ptr_array_unref(files);
    g_free(origin);
    remove_tree(root);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_index/project_root", test_project_root);
    g_test_add_func("/code_index/walk_files", test_walk_files);
    g_test_add_func("/code_index/git_files", test_git_files);
    g_test_add_func("/code_index/find_ranking", test_find_ranking);
    g_test_add_func("/code_index/find_open_buffers", test_find_open_buffers);
    g_test_add_func("/code_index/find_cancelled", test_find_cancelled);
    g_test_add_func("/code_index/references", test_references);
    return g_test_run();
}
