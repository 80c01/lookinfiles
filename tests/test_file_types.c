/* Unit tests for src/file_types.c (name- and content-based detection). */
#include "file_types.h"

#include <glib/gstdio.h>
#include <string.h>

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


#define DETECT(name, lit) file_kind_detect((name), (const guchar*)(lit), sizeof(lit) - 1)

static void test_detect_magic(void) {
    /* Content wins over a missing or wrong extension */
    g_assert_cmpint(DETECT("photo", "\x89PNG\r\n\x1a\n\0\0\0\rIHDR"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("photo.txt", "\xFF\xD8\xFF\xE0\0\x10JFIF"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("anim", "GIF89a\x01\0"), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("x", "RIFF\0\0\0\0WEBPVP8 "), ==, FILE_KIND_IMAGE);
    g_assert_cmpint(DETECT("doc", "%PDF-1.7\n%\xE2\xE3"), ==, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("figure", "%!PS-Adobe-3.0\n%%Pages: 2\n"), ==, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("figure.eps", "%!PS-Adobe-3.0 EPSF-3.0\n"), ==, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("paper.dvi", "\xF7\x02\x01\x83\x92\xC0\x1C"), ==, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("data.bin", "\xF7\x02\x01\x83\x92\xC0\x1C"), !=, FILE_KIND_PDF);
    g_assert_cmpint(DETECT("script.sh", "%!shell\necho\n"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(file_kind_from_name("a.ps"), ==, FILE_KIND_PDF);
    g_assert_cmpint(file_kind_from_name("a.DVI"), ==, FILE_KIND_PDF);
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
    g_assert_cmpint(DETECT("Main.class", "\xCA\xFE\xBA\xBE\0\0\0\x3D"), ==, FILE_KIND_CLASS);
    g_assert_cmpint(DETECT("unknown", "\xCA\xFE\xBA\xBE\0\0\0\x3D"), ==, FILE_KIND_CLASS);
    g_assert_cmpint(file_kind_from_name("App.class"), ==, FILE_KIND_CLASS);
    g_assert_cmpint(DETECT("module.wasm", "\0asm\x01\0\0\0"), ==, FILE_KIND_WASM);
    g_assert_cmpint(DETECT("binary", "\0asm\x01\0\0\0"), ==, FILE_KIND_WASM);
    g_assert_cmpint(file_kind_from_name("module.wasm"), ==, FILE_KIND_WASM);
    /* Unknown binary signature: trust a media name (TGA has no magic number) */
    g_assert_cmpint(DETECT("pic.tga", "\0\0\x02\0\0\0\0\0"), ==, FILE_KIND_IMAGE);
    /* ZIP archives: Word documents by name or by their first entries */
    g_assert_cmpint(DETECT("report.docx", "PK\x03\x04\x14\0\x06\0"), ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(DETECT("report", "PK\x03\x04\x14\0\x06\0\x08\0\0\0[Content_Types].xml...PK\x03\x04word/_rels"),
                    ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(DETECT("report", "PK\x03\x04\x14\0\0\0\x08\0\0\0word/document.xml"), ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(DETECT("archive.zip", "PK\x03\x04\x14\0\0\0\x08\0\0\0src/main.c"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("notes.odt", "PK\x03\x04\x14\0\0\0\0\0mimetype"), ==, FILE_KIND_DOCUMENT);
    /* A .docx that is not a ZIP is shown for what it is */
    g_assert_cmpint(DETECT("fake.docx", "just text"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("fake.docx", "\x01\x02\0\x03"), ==, FILE_KIND_BINARY);
    g_assert_cmpint(file_kind_detect("empty.docx", NULL, 0), ==, FILE_KIND_TEXT);
    /* Empty files are text, whatever their name */
    g_assert_cmpint(file_kind_detect("empty.png", NULL, 0), ==, FILE_KIND_TEXT);
    g_assert_cmpint(file_kind_detect("empty.md", (const guchar*)"", 0), ==, FILE_KIND_MARKDOWN);
}

static void test_detect_containers(void) {
    /* ZIP-based formats: by name, then by the parts named in the first entries */
    g_assert_cmpint(DETECT("book", "PK\x03\x04\x0a\0\0\0mimetypeapplication/epub+zip"), ==, FILE_KIND_EBOOK);
    g_assert_cmpint(DETECT("doc", "PK\x03\x04\0\0mimetypeapplication/vnd.oasis.opendocument.text"),
                    ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(DETECT("calc", "PK\x03\x04\0\0mimetypeapplication/vnd.oasis.opendocument.spreadsheet"),
                    ==, FILE_KIND_SPREADSHEET);
    g_assert_cmpint(DETECT("show", "PK\x03\x04\0\0mimetypeapplication/vnd.oasis.opendocument.presentation"),
                    ==, FILE_KIND_PRESENTATION);
    g_assert_cmpint(DETECT("sheet", "PK\x03\x04\0\0[Content_Types].xml..PK\x03\x04xl/workbook.xml"),
                    ==, FILE_KIND_SPREADSHEET);
    g_assert_cmpint(DETECT("deck", "PK\x03\x04\0\0[Content_Types].xml..PK\x03\x04ppt/slides"),
                    ==, FILE_KIND_PRESENTATION);
    g_assert_cmpint(DETECT("data.xlsx", "PK\x03\x04\0\0"), ==, FILE_KIND_SPREADSHEET);
    g_assert_cmpint(DETECT("lib.jar", "PK\x03\x04\0\0META-INF/"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("empty.zip", "PK\x05\x06\0\0\0\0"), ==, FILE_KIND_ARCHIVE);
    /* Other archives and compressed files */
    g_assert_cmpint(DETECT("x", "\x1F\x8B\x08\0\0\0"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("ls.1.gz", "\x1F\x8B\x08\0\0\0"), ==, FILE_KIND_MANPAGE);
    g_assert_cmpint(DETECT("x", "BZh91AY&SY"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("x", "\xFD" "7zXZ\0\0"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("x", "\x28\xB5\x2F\xFD\0"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("x", "7z\xBC\xAF\x27\x1C\0"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("x", "Rar!\x1A\x07\x01\0"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("libfoo.a", "!<arch>\nfoo.o/"), ==, FILE_KIND_ARCHIVE);
    guchar tar[512] = "hello.txt";
    memcpy(tar + 257, "ustar", 5);
    g_assert_cmpint(file_kind_detect("t", tar, sizeof(tar)), ==, FILE_KIND_ARCHIVE);
    /* Databases, fonts, certificates */
    g_assert_cmpint(DETECT("app.db", "SQLite format 3\0\x10\0"), ==, FILE_KIND_DATABASE);
    g_assert_cmpint(DETECT("messages", "\xDE\x12\x04\x95\0\0\0\0"), ==, FILE_KIND_TRANSLATION);
    g_assert_cmpint(DETECT("fr.po", "msgid \"\"\nmsgstr \"\"\n"), ==, FILE_KIND_TRANSLATION);
    g_assert_cmpint(DETECT("disc.iso", "\0\0\0\0\x01\x02"), ==, FILE_KIND_ARCHIVE);
    g_assert_cmpint(DETECT("cache.db", "\x01\x02\0\x03"), ==, FILE_KIND_BINARY);
    g_assert_cmpint(DETECT("font", "OTTO\0\x0a"), ==, FILE_KIND_FONT);
    g_assert_cmpint(DETECT("font", "wOF2\0\x01"), ==, FILE_KIND_FONT);
    g_assert_cmpint(DETECT("Sans.ttf", "\0\x01\0\0\0\x10"), ==, FILE_KIND_FONT);
    g_assert_cmpint(DETECT("blob", "\0\x01\0\0\0\x10"), ==, FILE_KIND_BINARY);
    g_assert_cmpint(DETECT("ca.der", "\x30\x82\x03\x0a\x30\x82"), ==, FILE_KIND_CERTIFICATE);
    g_assert_cmpint(DETECT("server", "-----BEGIN CERTIFICATE-----\nMIID"), ==, FILE_KIND_CERTIFICATE);
}

static void test_detect_text_formats(void) {
    g_assert_cmpint(DETECT("data.csv", "a,b\n1,2\n"), ==, FILE_KIND_CSV);
    /* UTF-16 text without a byte order mark is text, not binary */
    g_assert_cmpint(DETECT("notes", "h\0e\0l\0l\0o\0 \0w\0o\0r\0l\0d\0"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("data.json", "{\"a\": 1}"), ==, FILE_KIND_JSON);
    g_assert_cmpint(DETECT("config", "{\n  \"a\": 1}"), ==, FILE_KIND_JSON);
    g_assert_cmpint(DETECT("list", "[{\"a\": 1}]"), ==, FILE_KIND_JSON);
    g_assert_cmpint(DETECT("settings.ini", "[section]\nkey=1"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("main.js", "{\"a\": 1}"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("nb.ipynb", "{\n \"cells\": ["), ==, FILE_KIND_NOTEBOOK);
    g_assert_cmpint(DETECT("fix.patch", "From: a\n"), ==, FILE_KIND_DIFF);
    g_assert_cmpint(DETECT("changes", "diff --git a/x b/x\n"), ==, FILE_KIND_DIFF);
    g_assert_cmpint(DETECT("changes.txt", "--- a/x\n+++ b/x\n@@ -1 +1 @@\n"), ==, FILE_KIND_DIFF);
    g_assert_cmpint(DETECT("notes.txt", "--- a list\nitem\n"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("0001-fix.txt",
                           "From 0123456789abcdef0123456789abcdef01234567 Mon Sep 17 00:00:00 2001\n"),
                    ==, FILE_KIND_DIFF);
    g_assert_cmpint(DETECT("app.log", "2026-01-01 INFO start\n"), ==, FILE_KIND_LOG);
    g_assert_cmpint(DETECT("app.log.1", "2026-01-01 INFO start\n"), ==, FILE_KIND_LOG);
    g_assert_cmpint(DETECT("meeting", "BEGIN:VCALENDAR\r\nVERSION:2.0"), ==, FILE_KIND_CALENDAR);
    g_assert_cmpint(DETECT("bob", "begin:vcard\r\n"), ==, FILE_KIND_CONTACT);
    g_assert_cmpint(DETECT("app.desktop", "[Desktop Entry]\nName=App"), ==, FILE_KIND_DESKTOP);
    g_assert_cmpint(DETECT("ls.1", ".TH LS 1\n"), ==, FILE_KIND_MANPAGE);
    g_assert_cmpint(DETECT("ls.1", ".\\\" comment\n.TH LS 1\n"), ==, FILE_KIND_MANPAGE);
    g_assert_cmpint(DETECT("page", ".TH X 1\n"), ==, FILE_KIND_MANPAGE);
    g_assert_cmpint(DETECT("version.1", "1.2.3\n"), ==, FILE_KIND_TEXT);
    g_assert_cmpint(DETECT("doc.rtf", "{\\rtf1\\ansi"), ==, FILE_KIND_RTF);
    /* OLE2 compound files: by content whatever the name (a .pot PowerPoint
     * template is not a gettext template), by extension otherwise */
    static const guchar ole[] = "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1\0\0";
    g_assert_cmpint(file_kind_detect("report.doc", ole, sizeof ole), ==, FILE_KIND_OLE);
    g_assert_cmpint(file_kind_detect("slides.pot", ole, sizeof ole), ==, FILE_KIND_OLE);
    g_assert_cmpint(file_kind_detect("noext", ole, sizeof ole), ==, FILE_KIND_OLE);
    g_assert_cmpint(file_kind_from_name("budget.XLS"), ==, FILE_KIND_OLE);
    g_assert_cmpint(file_kind_from_name("show.pps"), ==, FILE_KIND_OLE);
    g_assert_cmpint(file_kind_from_name("fr.pot"), ==, FILE_KIND_TRANSLATION);
    g_assert_cmpint(DETECT("key.pem", "-----BEGIN PRIVATE KEY-----\n"), ==, FILE_KIND_CERTIFICATE);
    g_assert_cmpint(DETECT("key.pem", "not a pem\n"), ==, FILE_KIND_TEXT);
    /* An empty file keeps a text kind from its name only */
    g_assert_cmpint(file_kind_detect("e.csv", NULL, 0), ==, FILE_KIND_CSV);
    g_assert_cmpint(file_kind_detect("e.zip", NULL, 0), ==, FILE_KIND_TEXT);
}

static void test_kind_labels(void) {
    g_assert_null(file_kind_label(FILE_KIND_TEXT));
    g_assert_cmpstr(file_kind_label(FILE_KIND_CSV), ==, "CSV");
    g_assert_cmpstr(file_kind_label(FILE_KIND_ARCHIVE), ==, "Archive");
    for (gint k = 1; k <= FILE_KIND_LAST; k++) {
        g_assert_nonnull(file_kind_label((FileKind)k));
    }
    g_assert_true(file_kind_is_text(FILE_KIND_JSON));
    g_assert_false(file_kind_is_text(FILE_KIND_ARCHIVE));
    g_assert_true(file_kind_is_renderable(FILE_KIND_CSV));
    g_assert_false(file_kind_is_renderable(FILE_KIND_BINARY));
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
    g_assert_cmpint(file_kind_from_name("Report.DOCX"), ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(file_kind_from_name("old.doc"), ==, FILE_KIND_OLE);
    g_assert_true(file_kind_is_renderable(FILE_KIND_OLE)); /* m: hex dump */
    g_assert_true(file_kind_is_renderable(FILE_KIND_DOCUMENT)); /* m: hex dump */
    g_assert_false(file_kind_is_media(FILE_KIND_DOCUMENT));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file_types/extension", test_extension);
    g_test_add_func("/file_types/documents", test_document_types);
    g_test_add_func("/file_types/media", test_media_types);
    g_test_add_func("/file_types/null_safety", test_null_safety);
    g_test_add_func("/file_types/renderable", test_renderable);
    g_test_add_func("/file_types/detect_magic", test_detect_magic);
    g_test_add_func("/file_types/detect_text", test_detect_text);
    g_test_add_func("/file_types/detect_binary", test_detect_binary);
    g_test_add_func("/file_types/detect_containers", test_detect_containers);
    g_test_add_func("/file_types/detect_text_formats", test_detect_text_formats);
    g_test_add_func("/file_types/kind_labels", test_kind_labels);
    g_test_add_func("/file_types/kind_of_path", test_kind_of_path);
    g_test_add_func("/file_types/kind_predicates", test_kind_predicates);
    return g_test_run();
}
