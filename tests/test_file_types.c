/* Unit tests for src/file_types.c (name- and content-based detection). */
#include "file_types.h"

#include <glib/gstdio.h>

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

#define DETECT(name, lit) file_kind_detect((name), (const guchar*)(lit), sizeof(lit) - 1)

static void test_detect_magic(void) {
    /* Content wins over a missing or wrong extension */
    g_assert_cmpint(DETECT("photo", "\x89PNG\r\n\x1a\n\0\0\0\rIHDR"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("photo.txt", "\xFF\xD8\xFF\xE0\0\x10JFIF"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("anim", "GIF89a\x01\0"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("x", "RIFF\0\0\0\0WEBPVP8 "), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("doc", "%PDF-1.7\n%\xE2\xE3"), ==, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("song", "ID3\x04\0\0\0\0"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("song", "fLaC\0\0\0\x22"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("sound", "RIFF\0\0\0\0WAVEfmt "), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("clip", "\0\0\0\x18" "ftypisom\0\0\0\0"), ==, FILE_KIND_VIDEO);
    g_assert_cmpint(DETECT("clip", "\0\0\0\x18" "ftypM4A \0\0\0\0"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("clip", "\x1A\x45\xDF\xA3\x01\0"), ==, FILE_KIND_VIDEO);
    g_assert_cmpint(DETECT("x", "RIFF\0\0\0\0AVI LIST"), ==, FILE_KIND_VIDEO);
    /* Ambiguous containers follow the name when it says audio or video */
    g_assert_cmpint(DETECT("song.m4a", "\0\0\0\x18" "ftypisom\0\0\0\0"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("song.mka", "\x1A\x45\xDF\xA3\x01\0"), ==, FILE_KIND_VIDEO);
    g_assert_cmpint(DETECT("music.ogg", "OggS\0\x02\0\0\x01vorbis"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(DETECT("film", "OggS\0\x02\0\0\x80theora"), ==, FILE_KIND_VIDEO);
}

static void test_detect_text(void) {
    /* TypeScript, not an MPEG transport stream */
    g_assert_cmpint(DETECT("main.ts", "export const x: number = 1;\n"), ==, FILE_KIND_TEXT);
    /* Text with a media extension */
    g_assert_cmpint(DETECT("notes.png", "just some text\n"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("script", "#!/bin/sh\necho hi\n"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("latin1.txt", "caf\xE9 cr\xE8me\n"), ==, FILE_KIND_TEXT);
    /* UTF-8 sequence cut by the end of the sniffed bytes */
    g_assert_cmpint(DETECT("cut.txt", "abc \xC3"), ==, FILE_KIND_TEXT);
    /* Markdown and Org are known by name only */
    g_assert_cmpint(DETECT("README.md", "# Title\n"), ==, FILE_KIND_MARKDOWN);
    g_assert_cmpint(DETECT("todo.org", "* TODO x\n"), ==, FILE_KIND_ORG);
    /* HTML by content, fragments by name */
    g_assert_cmpint(DETECT("page", "\n<!DOCTYPE html>\n<html>"), ==, FILE_KIND_HTML);
    g_assert_cmpint(DETECT("page", "<html lang=\"en\">"), ==, FILE_KIND_HTML);
    g_assert_cmpint(DETECT("part.html", "<div><svg width=\"1\"></svg></div>"), ==, FILE_KIND_HTML);
    g_assert_cmpint(DETECT("part.txt", "<div>hi</div>"), ==, FILE_KIND_TEXT);
    /* SVG: the root element, after prolog, comments and doctype */
    g_assert_cmpint(DETECT("logo", "<?xml version=\"1.0\"?>\n<!-- c -->\n<!DOCTYPE svg>\n<svg xmlns=\"\">"),
                    ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("logo.svg", "<svg>"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("icon.xpm", "/* XPM */\nstatic char* x[] = {"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("data.xml", "<?xml version=\"1.0\"?><root/>"), ==, FILE_KIND_TEXT);
}

static void test_detect_binary(void) {
    g_assert_cmpint(DETECT("prog", "\x7F" "ELF\x02\x01\x01\0\0\0"), ==, FILE_KIND_EXECUTABLE);
    g_assert_cmpint(DETECT("libz.so.1", "\x7F" "ELF\x01\x02\x01\0"), ==, FILE_KIND_EXECUTABLE);
    g_assert_cmpint(DETECT("data.bin", "\x01\x02\0\x03\x04"), ==, FILE_KIND_BINARY);
    /* Unknown binary signature: trust a media name (TGA has no magic number) */
    g_assert_cmpint(DETECT("pic.tga", "\0\0\x02\0\0\0\0\0"), ==, FILE_KIND_IMAGE);
    /* Empty files are text, whatever their name */
    g_assert_cmpint(file_kind_detect("empty.png", NULL, 0), ==, FILE_KIND_TEXT);
    g_assert_cmpint(file_kind_detect("empty.md", (const guchar*)"", 0), ==, FILE_KIND_MARKDOWN);
}

static void test_kind_of_path(void) {
    gchar* dir = g_dir_make_tmp("ft-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar* path = g_build_filename(dir, "image.txt", NULL);
    g_assert_true(g_file_set_contents(path, "GIF89a\x01\0\x01\0", 10, NULL));
    g_assert_cmpint(file_kind_of_path(path, "image.txt"), ==, FILE_KIND_IMAGE);
    g_remove(path);
    /* Unreadable: name-based guess */
    g_assert_cmpint(file_kind_of_path(path, "image.txt"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(file_kind_of_path(NULL, "a.pdf"), ==, FILE_KIND_PDF);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_kind_predicates(void) {
    g_assert_true(file_kind_is_media(FILE_KIND_PDF));
    g_assert_false(file_kind_is_media(FILE_KIND_HTML));
    g_assert_false(file_kind_is_media(FILE_KIND_BINARY));
    g_assert_true(file_kind_is_renderable(FILE_KIND_ORG));
    g_assert_true(file_kind_is_renderable(FILE_KIND_EXECUTABLE)); /* m: summary / hex dump */
    g_assert_false(file_kind_is_renderable(FILE_KIND_TEXT));
    g_assert_cmpint(file_kind_from_name("a.mp3"), ==, FILE_KIND_AUDIO);
    g_assert_cmpint(file_kind_from_name("a.c"), ==, FILE_KIND_TEXT);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file_types/extension", test_extension);
    g_test_add_func("/file_types/documents", test_document_types);
    g_test_add_func("/file_types/media", test_media_types);
    g_test_add_func("/file_types/null_safety", test_null_safety);
    g_test_add_func("/file_types/renderable", test_renderable);
    g_test_add_func("/file_types/icons", test_icons);
    g_test_add_func("/file_types/detect_magic", test_detect_magic);
    g_test_add_func("/file_types/detect_text", test_detect_text);
    g_test_add_func("/file_types/detect_binary", test_detect_binary);
    g_test_add_func("/file_types/kind_of_path", test_kind_of_path);
    g_test_add_func("/file_types/kind_predicates", test_kind_predicates);
    return g_test_run();
}
