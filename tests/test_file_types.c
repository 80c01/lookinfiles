/* Unit tests for src/file_types.c (pure name-based detection). */
#include "file_types.h"

static void test_extension(void) {
    g_assert_cmpstr(file_extension("notes.md"), ==, "md");
    g_assert_cmpstr(file_extension("archive.tar.gz"), ==, "gz");
    g_assert_cmpstr(file_extension("Makefile"), ==, "");
    g_assert_cmpstr(file_extension(".bashrc"), ==, "");
    g_assert_cmpstr(file_extension(".config.json"), ==, "json");
    g_assert_cmpstr(file_extension(""), ==, "");
    g_assert_cmpstr(file_extension(NULL), ==, "");
}

static void test_document_types(void) {
    g_assert_true(is_markdown_file("README.md"));
    g_assert_true(is_markdown_file("README.MD"));
    g_assert_true(is_markdown_file("doc.markdown"));
    g_assert_false(is_markdown_file("md"));
    g_assert_true(is_html_file("index.html"));
    g_assert_true(is_html_file("page.HTM"));
    g_assert_true(is_org_file("todo.org"));
    g_assert_true(is_pdf_file("paper.pdf"));
    g_assert_false(is_pdf_file("paper.pdf.txt"));
}

static void test_media_types(void) {
    g_assert_true(is_image_file("photo.JPG"));
    g_assert_true(is_image_file("icon.png"));
    g_assert_true(is_video_file("clip.mp4"));
    g_assert_true(is_audio_file("song.mp3"));
    g_assert_false(is_image_file("main.c"));
    g_assert_false(is_video_file("main.c"));
    g_assert_false(is_audio_file("main.c"));
}

static void test_null_safety(void) {
    g_assert_false(is_markdown_file(NULL));
    g_assert_false(is_html_file(NULL));
    g_assert_false(is_org_file(NULL));
    g_assert_false(is_image_file(NULL));
    g_assert_false(is_pdf_file(NULL));
    g_assert_false(is_video_file(NULL));
    g_assert_false(is_audio_file(NULL));
    g_assert_false(is_renderable_file(NULL));
    g_assert_nonnull(file_icon(NULL, FALSE));
}

static void test_renderable(void) {
    g_assert_true(is_renderable_file("a.md"));
    g_assert_true(is_renderable_file("a.html"));
    g_assert_true(is_renderable_file("a.org"));
    g_assert_true(is_renderable_file("a.png"));
    g_assert_true(is_renderable_file("a.pdf"));
    g_assert_false(is_renderable_file("a.c"));
    g_assert_false(is_renderable_file("Makefile"));
}

static void test_icons(void) {
    g_assert_cmpstr(file_icon("anything", TRUE), ==, "📁");
    g_assert_cmpstr(file_icon("", FALSE), ==, "📄");
    g_assert_cmpstr(file_icon("Makefile", FALSE), ==, "🛠️");
    g_assert_cmpstr(file_icon("main.rs", FALSE), ==, "🦀");
    g_assert_cmpstr(file_icon("script.py", FALSE), ==, "🐍");
    g_assert_cmpstr(file_icon("unknown.zzz", FALSE), ==, "📄");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file_types/extension", test_extension);
    g_test_add_func("/file_types/documents", test_document_types);
    g_test_add_func("/file_types/media", test_media_types);
    g_test_add_func("/file_types/null_safety", test_null_safety);
    g_test_add_func("/file_types/renderable", test_renderable);
    g_test_add_func("/file_types/icons", test_icons);
    return g_test_run();
}
