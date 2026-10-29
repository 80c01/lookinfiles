/* Unit tests for src/epub.c (EPUB e-books to HTML). */
#include "epub.h"

#include <string.h>

static void test_book(void) {
    EpubInfo info = { 0 };
    GError* error = NULL;
    gchar* html = epub_file_to_html("tests/fixtures/preview/story.epub", 100000, &info, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(info.title, ==, "The Lighthouse Keeper");
    g_assert_cmpstr(info.author, ==, "Grace Hopper");
    g_assert_cmpuint(info.n_chapters, ==, 2);
    g_assert_nonnull(strstr(html, "<title>The Lighthouse Keeper</title><meta name=\"author\" content=\"Grace Hopper\">"));
    /* Cover first, then metadata, contents, chapters in order */
    const gchar* cover = strstr(html, "<p><img src=\"data:image/png;base64,");
    const gchar* contents = strstr(html, "<h2>Contents</h2>\n<ul>\n<li>Chapter 1. The storm</li>\n<li>&#160;&#160;&#160;&#160;The light</li>\n<li>Chapter 2. Morning</li>");
    const gchar* ch1 = strstr(html, "<h1>Chapter 1. The storm</h1>");
    const gchar* ch2 = strstr(html, "<h1>Chapter 2. Morning</h1>");
    g_assert_true(cover != NULL && contents != NULL && ch1 != NULL && ch2 != NULL);
    g_assert_true(cover < contents && contents < ch1 && ch1 < ch2);
    g_assert_nonnull(strstr(html, "<b>Publisher:</b> Lookin Press"));
    /* Chapter images are embedded */
    g_assert_null(strstr(html, "../images/fig.png"));
    g_free(html);
    epub_info_clear(&info);
}

static void test_first_chapters_only(void) {
    gchar* html = epub_file_to_html("tests/fixtures/preview/story.epub", 10, NULL, NULL);
    g_assert_nonnull(strstr(html, "Chapter 1. The storm</h1>"));
    g_assert_null(strstr(html, "Calm returned"));
    g_assert_nonnull(strstr(html, "Only the first chapters are shown."));
    g_free(html);
}

static void test_not_an_epub(void) {
    GError* error = NULL;
    g_assert_null(epub_file_to_html("tests/fixtures/preview/sample.zip", 100, NULL, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/epub/book", test_book);
    g_test_add_func("/epub/first_chapters_only", test_first_chapters_only);
    g_test_add_func("/epub/not_an_epub", test_not_an_epub);
    return g_test_run();
}
