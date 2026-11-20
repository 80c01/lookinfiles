/* Unit tests for src/search/deep_search_store.c: saved Deep Search criteria. */
#include "search/deep_search_store.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_round_trip_full(void) {
    gchar* dir = g_dir_make_tmp("dsearch_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "search.ini", NULL);

    g_assert_null(deep_search_store_load(path));

    SavedDeepSearch s;
    saved_deep_search_init(&s);
    s.folder = g_strdup("/home/u/projects");
    s.name = g_strdup("*.c");
    s.extensions = g_strdup("c, h");
    s.content = g_strdup("  TODO: fix  ");
    s.content_opts.match_case = TRUE;
    s.content_opts.whole_word = TRUE;
    s.content_opts.regex = TRUE;
    s.kind = DEEP_KIND_FILES;
    s.min_size = g_strdup("10k");
    s.max_size = g_strdup("5M");
    s.after = g_strdup("7d");
    s.before = g_strdup("today");
    s.exclude = g_strdup("build/, .git");
    s.depth = g_strdup("4");
    s.hidden = TRUE;
    s.one_filesystem = FALSE;

    GError* err = NULL;
    g_assert_true(deep_search_store_save(&s, path, &err));
    g_assert_no_error(err);
    saved_deep_search_clear(&s);

    SavedDeepSearch* loaded = deep_search_store_load(path);
    g_assert_nonnull(loaded);
    g_assert_false(loaded->use_current_folder);
    g_assert_cmpstr(loaded->folder, ==, "/home/u/projects");
    g_assert_cmpstr(loaded->name, ==, "*.c");
    g_assert_cmpstr(loaded->extensions, ==, "c, h");
    g_assert_cmpint(loaded->family, ==, -1);
    g_assert_cmpstr(loaded->content, ==, "  TODO: fix  ");
    g_assert_true(loaded->content_opts.match_case);
    g_assert_true(loaded->content_opts.whole_word);
    g_assert_true(loaded->content_opts.regex);
    g_assert_false(loaded->content_opts.fuzzy);
    g_assert_cmpint(loaded->kind, ==, DEEP_KIND_FILES);
    g_assert_cmpstr(loaded->min_size, ==, "10k");
    g_assert_cmpstr(loaded->max_size, ==, "5M");
    g_assert_cmpstr(loaded->after, ==, "7d");
    g_assert_cmpstr(loaded->before, ==, "today");
    g_assert_cmpstr(loaded->exclude, ==, "build/, .git");
    g_assert_cmpstr(loaded->depth, ==, "4");
    g_assert_true(loaded->hidden);
    g_assert_false(loaded->one_filesystem);
    saved_deep_search_free(loaded);

    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_current_folder_and_category(void) {
    gchar* dir = g_dir_make_tmp("dsearch_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "cat.ini", NULL);

    SavedDeepSearch s;
    saved_deep_search_init(&s);
    s.folder = g_strdup("/ignored/when/current");
    s.use_current_folder = TRUE;
    s.extensions = g_strdup("jpg");
    s.family = FILE_FAMILY_IMAGES;
    s.content = g_strdup("edvim");
    s.content_opts.fuzzy = TRUE;
    s.kind = DEEP_KIND_FOLDERS;

    g_assert_true(deep_search_store_save(&s, path, NULL));
    saved_deep_search_clear(&s);

    SavedDeepSearch* loaded = deep_search_store_load(path);
    g_assert_nonnull(loaded);
    g_assert_true(loaded->use_current_folder);
    g_assert_null(loaded->folder);
    g_assert_cmpint(loaded->family, ==, FILE_FAMILY_IMAGES);
    g_assert_null(loaded->extensions);
    g_assert_true(loaded->content_opts.fuzzy);
    g_assert_false(loaded->content_opts.regex);
    g_assert_cmpint(loaded->kind, ==, DEEP_KIND_FOLDERS);
    g_assert_false(loaded->hidden);
    g_assert_true(loaded->one_filesystem);
    saved_deep_search_free(loaded);

    /* Empty criteria still save and reload cleanly */
    saved_deep_search_init(&s);
    g_assert_true(deep_search_store_save(&s, path, NULL));
    loaded = deep_search_store_load(path);
    g_assert_nonnull(loaded);
    g_assert_false(loaded->use_current_folder);
    g_assert_null(loaded->folder);
    g_assert_cmpint(loaded->family, ==, -1);
    g_assert_cmpint(loaded->kind, ==, DEEP_KIND_ANY);
    g_assert_true(loaded->one_filesystem);
    saved_deep_search_free(loaded);

    /* Mutual exclusions and bad files handled safely */
    g_assert_true(g_file_set_contents(
        path, "[DeepSearch]\ncategory=videos\nextensions=mp4\nregex=true\nfuzzy=true\n", -1, NULL));
    loaded = deep_search_store_load(path);
    g_assert_nonnull(loaded);
    g_assert_cmpint(loaded->family, ==, FILE_FAMILY_VIDEOS);
    g_assert_null(loaded->extensions);
    g_assert_true(loaded->content_opts.regex);
    g_assert_false(loaded->content_opts.fuzzy);
    saved_deep_search_free(loaded);

    g_assert_true(g_file_set_contents(path, "[Other]\nfoo=bar\n", -1, NULL));
    g_assert_null(deep_search_store_load(path));

    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_target_ids(void) {
    gchar* t = deep_search_target_make("0123abcd");
    g_assert_cmpstr(t, ==, "lookinfiles-search://0123abcd");
    g_assert_cmpstr(deep_search_target_id(t), ==, "0123abcd");
    g_free(t);

    g_assert_null(deep_search_target_make("../etc"));
    g_assert_null(deep_search_target_make("ABCD"));
    g_assert_null(deep_search_target_make(""));
    g_assert_null(deep_search_target_make(NULL));
    g_assert_null(deep_search_target_id(NULL));
    g_assert_null(deep_search_target_id("lookinfiles-session://0123abcd"));
    g_assert_null(deep_search_target_id("lookinfiles-search://"));
    g_assert_null(deep_search_target_id("lookinfiles-search://../../x"));
    g_assert_null(deep_search_target_id("lookinfiles-search://ab/cd"));

    gchar* id = deep_search_store_new_id();
    gchar* made = deep_search_target_make(id);
    g_assert_nonnull(made);
    g_assert_cmpstr(deep_search_target_id(made), ==, id);
    gchar* path = deep_search_store_path(id);
    g_assert_nonnull(path);
    g_assert_true(g_str_has_suffix(path, ".ini"));
    g_assert_nonnull(strstr(path, "searches"));
    g_free(path);
    g_free(made);
    g_free(id);
    g_assert_null(deep_search_store_path("../bad"));
}

static void test_default_name(void) {
    SavedDeepSearch s;
    saved_deep_search_init(&s);
    gchar* n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "Deep Search");
    g_free(n);

    s.folder = g_strdup("/home/u/Documents");
    n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "Documents");
    g_free(n);

    s.family = FILE_FAMILY_IMAGES;
    n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "Images");
    g_free(n);

    s.extensions = g_strdup("pdf, docx");
    n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "pdf, docx");
    g_free(n);

    s.content = g_strdup("FIXME");
    n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "FIXME");
    g_free(n);

    s.name = g_strdup("report*");
    n = saved_deep_search_default_name(&s);
    g_assert_cmpstr(n, ==, "report*");
    g_free(n);

    saved_deep_search_clear(&s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/deep_search_store/round_trip_full", test_round_trip_full);
    g_test_add_func("/deep_search_store/current_folder_and_category", test_current_folder_and_category);
    g_test_add_func("/deep_search_store/target_ids", test_target_ids);
    g_test_add_func("/deep_search_store/default_name", test_default_name);
    return g_test_run();
}
