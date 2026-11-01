/* Unit tests for src/user_guide.c: the user guide pages built into the
 * binary (docs/user-guide/ and the reference pages) and their titles.
 * Headless. */
#include "user_guide.h"

static void test_home(void) {
    gchar* text = user_guide_read(USER_GUIDE_HOME);
    g_assert_nonnull(text);
    gchar* title = user_guide_title(text);
    g_assert_cmpstr(title, ==, "LookinFiles User Guide");
    g_free(title);
    g_free(text);
}

static void test_pages(void) {
    gchar** pages = user_guide_pages();
    g_assert_nonnull(pages);
    gboolean home = FALSE, keyboard = FALSE;
    guint n = 0;
    for (gint i = 0; pages[i] != NULL; i++, n++) {
        if (g_strcmp0(pages[i], USER_GUIDE_HOME) == 0) home = TRUE;
        if (g_strcmp0(pages[i], "keyboard.md") == 0) keyboard = TRUE;
        if (i > 0) g_assert_cmpint(g_strcmp0(pages[i - 1], pages[i]), <, 0);
        /* Every page reads and has a title */
        gchar* text = user_guide_read(pages[i]);
        g_assert_nonnull(text);
        gchar* title = user_guide_title(text);
        g_assert_nonnull(title);
        g_assert_cmpstr(title, !=, "");
        g_free(title);
        g_free(text);
    }
    g_assert_true(home);
    g_assert_true(keyboard);
    g_assert_cmpuint(n, >, 10);
    g_strfreev(pages);
}

static void test_missing(void) {
    g_assert_null(user_guide_read("user-guide/nope.md"));
    g_assert_null(user_guide_read("../README.md"));
    g_assert_null(user_guide_read("user-guide/../keyboard.md"));
    g_assert_null(user_guide_read("/org/lookin/guide/keyboard.md"));
    g_assert_null(user_guide_read(""));
    g_assert_null(user_guide_read(NULL));
}

static void test_title(void) {
    gchar* t = user_guide_title("Intro\n\n#Not a title\n# The  Title  \ntext\n# Second\n");
    g_assert_cmpstr(t, ==, "The  Title");
    g_free(t);
    /* Headings inside code blocks and deeper levels are skipped */
    t = user_guide_title("```sh\n# comment\n```\n## Sub\n# Real");
    g_assert_cmpstr(t, ==, "Real");
    g_free(t);
    g_assert_null(user_guide_title("no heading\n"));
    g_assert_null(user_guide_title(""));
    g_assert_null(user_guide_title(NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/user_guide/home", test_home);
    g_test_add_func("/user_guide/pages", test_pages);
    g_test_add_func("/user_guide/missing", test_missing);
    g_test_add_func("/user_guide/title", test_title);
    return g_test_run();
}
