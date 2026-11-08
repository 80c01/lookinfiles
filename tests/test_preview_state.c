/* Unit tests for src/preview/preview_state.c: what the preview shows. */
#include "preview/preview_state.h"

static void test_defaults(void) {
    AppData app = {0};
    g_assert_null(preview_state_path(&app));
    g_assert_null(preview_state_name(&app));
    g_assert_cmpint(preview_state_kind(&app), ==, FILE_KIND_TEXT);
    g_assert_false(preview_state_raw(&app));
    g_assert_cmpfloat(preview_state_zoom(&app), ==, 1.0);
    g_assert_cmpuint(preview_state_pages(&app), ==, 0);
    preview_state_free(&app);
}

static void test_file(void) {
    AppData app = {0};
    preview_state_set_file(&app, "/tmp/a.txt", "a.txt");
    g_assert_cmpstr(preview_state_path(&app), ==, "/tmp/a.txt");
    g_assert_cmpstr(preview_state_name(&app), ==, "a.txt");
    /* The current values given back */
    preview_state_set_file(&app, preview_state_path(&app), preview_state_name(&app));
    g_assert_cmpstr(preview_state_path(&app), ==, "/tmp/a.txt");
    g_assert_cmpstr(preview_state_name(&app), ==, "a.txt");
    preview_state_set_file(&app, NULL, "ignored");
    g_assert_null(preview_state_path(&app));
    g_assert_null(preview_state_name(&app));
    g_assert_cmpfloat(preview_state_zoom(&app), ==, 1.0);
    preview_state_free(&app);
    g_assert_null(app.preview_state);
}

static void test_detected(void) {
    AppData app = {0};
    static const gchar utf8[] = "UTF-8";
    g_assert_true(preview_state_set_detected(&app, FILE_KIND_MARKDOWN, utf8, "text/markdown"));
    g_assert_false(preview_state_set_detected(&app, FILE_KIND_MARKDOWN, utf8, "text/markdown"));
    g_assert_cmpint(preview_state_kind(&app), ==, FILE_KIND_MARKDOWN);
    g_assert_true(preview_state_encoding(&app) == utf8);
    g_assert_cmpstr(preview_state_content_type(&app), ==, "text/markdown");
    g_assert_true(preview_state_set_detected(&app, FILE_KIND_MARKDOWN, utf8, NULL));
    g_assert_null(preview_state_content_type(&app));
    preview_state_free(&app);
}

static void test_page(void) {
    AppData app = {0};
    preview_state_set_page(&app, 3, 5);
    g_assert_cmpuint(preview_state_page(&app), ==, 3);
    g_assert_cmpuint(preview_state_pages(&app), ==, 5);
    preview_state_set_page(&app, preview_state_page(&app), 2);
    g_assert_cmpuint(preview_state_page(&app), ==, 2);
    preview_state_set_raw(&app, TRUE);
    preview_state_set_zoom(&app, 2.5);
    g_assert_true(preview_state_raw(&app));
    g_assert_cmpfloat(preview_state_zoom(&app), ==, 2.5);
    preview_state_free(&app);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_state/defaults", test_defaults);
    g_test_add_func("/preview_state/file", test_file);
    g_test_add_func("/preview_state/detected", test_detected);
    g_test_add_func("/preview_state/page", test_page);
    return g_test_run();
}
