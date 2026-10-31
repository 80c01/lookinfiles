/* Unit tests for src/canvas_doc.c: canvas note pages (page.xml), styled runs,
 * media files and the Markdown export. Headless. */
#include "canvas_doc.h"

#include <glib/gstdio.h>
#include <string.h>

static const CanvasRun* run_at(const CanvasBlock* b, guint i) {
    g_assert_cmpuint(i, <, b->runs->len);
    return &g_array_index(b->runs, CanvasRun, i);
}

static void test_runs_merge(void) {
    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_text(doc, 0, 0, 0, "héllo world");
    g_assert_cmpint(b->w, >, 0);
    canvas_block_add_run(b, 0, 5, CANVAS_STYLE_BOLD);
    canvas_block_add_run(b, 3, 8, CANVAS_STYLE_ITALIC);
    canvas_block_add_run(b, 20, 30, CANVAS_STYLE_BOLD); /* out of the text: ignored */
    canvas_block_add_run(b, 2, 2, CANVAS_STYLE_BOLD);   /* empty: ignored */
    g_assert_cmpuint(b->runs->len, ==, 3);
    g_assert_cmpint(run_at(b, 0)->start, ==, 0);
    g_assert_cmpint(run_at(b, 0)->end, ==, 3);
    g_assert_cmpuint(run_at(b, 0)->styles, ==, CANVAS_STYLE_BOLD);
    g_assert_cmpuint(run_at(b, 1)->styles, ==, CANVAS_STYLE_BOLD | CANVAS_STYLE_ITALIC);
    g_assert_cmpint(run_at(b, 2)->start, ==, 5);
    g_assert_cmpint(run_at(b, 2)->end, ==, 8);
}

static void test_xml_round_trip(void) {
    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    CanvasBlock* t = canvas_doc_add_text(doc, 40, 60, 320, "Agenda <today> & é\nline 2");
    canvas_block_add_run(t, 0, 6, CANVAS_STYLE_HEADING | CANVAS_STYLE_BOLD);
    canvas_block_add_run(t, 17, 18, CANVAS_STYLE_CODE);
    canvas_doc_add_image(doc, 400, 60, 480, 300, "media/0001.png");

    g_autofree gchar* xml = canvas_doc_to_xml(doc);
    g_assert_nonnull(strstr(xml, "<span s=\"b h\">Agenda</span>"));
    g_assert_nonnull(strstr(xml, "&lt;today&gt; &amp; "));
    g_assert_nonnull(strstr(xml, "src=\"media/0001.png\""));

    g_autoptr(GError) error = NULL;
    g_autoptr(CanvasDoc) back = canvas_doc_parse(xml, -1, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(back->blocks->len, ==, 2);
    CanvasBlock* bt = g_ptr_array_index(back->blocks, 0);
    g_assert_cmpint(bt->kind, ==, CANVAS_BLOCK_TEXT);
    g_assert_cmpstr(bt->text, ==, t->text);
    g_assert_cmpint(bt->x, ==, 40);
    g_assert_cmpint(bt->w, ==, 320);
    g_assert_cmpint(bt->h, ==, 0);
    g_assert_cmpuint(bt->runs->len, ==, 2);
    g_assert_cmpint(run_at(bt, 1)->start, ==, 17);
    g_assert_cmpuint(run_at(bt, 1)->styles, ==, CANVAS_STYLE_CODE);
    CanvasBlock* bi = g_ptr_array_index(back->blocks, 1);
    g_assert_cmpint(bi->kind, ==, CANVAS_BLOCK_IMAGE);
    g_assert_cmpint(bi->h, ==, 300);
    g_assert_cmpstr(bi->src, ==, "media/0001.png");

    /* Same output again */
    g_autofree gchar* xml2 = canvas_doc_to_xml(back);
    g_assert_cmpstr(xml, ==, xml2);
}

static void test_parse_errors(void) {
    GError* error = NULL;
    g_assert_null(canvas_doc_parse("<notes/>", -1, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_null(canvas_doc_parse("<canvas><text>", -1, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_null(canvas_doc_parse("", -1, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    /* Unknown elements and images without a source are skipped */
    g_autoptr(CanvasDoc) doc = canvas_doc_parse("<canvas><shape/><image x=\"1\"/><text>a</text></canvas>", -1, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(doc->blocks->len, ==, 1);
}

static void test_raise_remove(void) {
    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    CanvasBlock* a = canvas_doc_add_text(doc, 0, 0, 10, "a");
    CanvasBlock* b = canvas_doc_add_text(doc, 0, 0, 10, "b");
    canvas_doc_raise(doc, a);
    g_assert_true(g_ptr_array_index(doc->blocks, 1) == a);
    g_assert_true(canvas_doc_remove(doc, b));
    g_assert_false(canvas_doc_remove(doc, b));
    g_assert_cmpuint(doc->blocks->len, ==, 1);
}

static void write_file(const gchar* dir, const gchar* name) {
    g_autofree gchar* path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, "x", -1, NULL));
}

static void test_files(void) {
    g_autofree gchar* tmp = g_dir_make_tmp("canvas-XXXXXX", NULL);
    g_autofree gchar* dir = g_build_filename(tmp, "Note.canvas", NULL);
    GError* error = NULL;

    /* Missing directory: error; empty directory: empty page */
    g_assert_null(canvas_doc_load(dir, &error));
    g_clear_error(&error);
    g_autofree gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    g_assert_cmpint(g_mkdir_with_parents(media, 0755), ==, 0);
    g_autoptr(CanvasDoc) empty = canvas_doc_load(dir, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(empty->blocks->len, ==, 0);

    g_autofree gchar* first = canvas_doc_new_media_path(dir, ".png");
    g_assert_cmpstr(first, ==, "media/0001.png");
    write_file(media, "0001.png");
    write_file(media, "0007.jpg");
    g_autofree gchar* next = canvas_doc_new_media_path(dir, NULL);
    g_assert_cmpstr(next, ==, "media/0008.png");

    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    canvas_doc_add_image(doc, 0, 0, 10, 10, "media/0007.jpg");
    g_assert_true(canvas_doc_save(doc, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(canvas_doc_prune_media(doc, dir), ==, 1);
    g_autofree gchar* kept = g_build_filename(media, "0007.jpg", NULL);
    g_autofree gchar* gone = g_build_filename(media, "0001.png", NULL);
    g_assert_true(g_file_test(kept, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(gone, G_FILE_TEST_EXISTS));

    g_autoptr(CanvasDoc) loaded = canvas_doc_load(dir, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(loaded->blocks->len, ==, 1);

    g_autofree gchar* page = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    g_remove(kept);
    g_remove(page);
    g_rmdir(media);
    g_rmdir(dir);
    g_rmdir(tmp);
}

static void test_markdown(void) {
    g_autoptr(CanvasDoc) doc = canvas_doc_new();
    canvas_doc_add_image(doc, 0, 200, 10, 10, "media/0001.png");
    CanvasBlock* t = canvas_doc_add_text(doc, 300, 0, 100, "Title\nsome bold and code");
    canvas_block_add_run(t, 0, 5, CANVAS_STYLE_HEADING);
    canvas_block_add_run(t, 11, 15, CANVAS_STYLE_BOLD | CANVAS_STYLE_ITALIC);
    canvas_block_add_run(t, 20, 24, CANVAS_STYLE_CODE);
    canvas_doc_add_text(doc, 0, 0, 100, "left");
    canvas_doc_add_text(doc, 0, 100, 100, ""); /* empty box: skipped */

    g_autofree gchar* md = canvas_doc_to_markdown(doc, "My Note.canvas/");
    g_assert_cmpstr(md, ==,
                    "left\n\n"
                    "## Title\nsome ***bold*** and `code`\n\n"
                    "![](<My Note.canvas/media/0001.png>)\n");
    g_autofree gchar* plain = canvas_doc_to_markdown(doc, NULL);
    g_assert_nonnull(strstr(plain, "![](media/0001.png)"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/canvas_doc/runs_merge", test_runs_merge);
    g_test_add_func("/canvas_doc/xml_round_trip", test_xml_round_trip);
    g_test_add_func("/canvas_doc/parse_errors", test_parse_errors);
    g_test_add_func("/canvas_doc/raise_remove", test_raise_remove);
    g_test_add_func("/canvas_doc/files", test_files);
    g_test_add_func("/canvas_doc/markdown", test_markdown);
    return g_test_run();
}
