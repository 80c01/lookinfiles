/* Unit tests for src/note_types.c: note type extensions, names and the
 * strings stored in notes.xml. Headless. */
#include "note_types.h"

static void test_from_filename(void) {
    g_assert_cmpint(note_type_from_filename("a.org"), ==, NOTE_TYPE_ORG);
    g_assert_cmpint(note_type_from_filename("a.org_archive"), ==, NOTE_TYPE_ORG);
    g_assert_cmpint(note_type_from_filename("a.MD"), ==, NOTE_TYPE_MARKDOWN);
    g_assert_cmpint(note_type_from_filename("a.markdown"), ==, NOTE_TYPE_MARKDOWN);
    g_assert_cmpint(note_type_from_filename("a.txt"), ==, NOTE_TYPE_TEXT);
    g_assert_cmpint(note_type_from_filename("dir.md/a"), ==, NOTE_TYPE_TEXT);
    g_assert_cmpint(note_type_from_filename(".md"), ==, NOTE_TYPE_TEXT);
    g_assert_cmpint(note_type_from_filename(NULL), ==, NOTE_TYPE_TEXT);
}

static void test_has_note_ext(void) {
    g_assert_true(note_type_has_note_ext("a.txt"));
    g_assert_true(note_type_has_note_ext("a.mkd"));
    g_assert_true(note_type_has_note_ext("a.org"));
    g_assert_false(note_type_has_note_ext("a.c"));
    g_assert_false(note_type_has_note_ext("Notes"));
    g_assert_false(note_type_has_note_ext(NULL));
}

static void test_str(void) {
    for (gint t = 0; t < NOTE_TYPE_COUNT; t++) {
        g_assert_cmpint(note_type_from_str(note_type_to_str(t), NULL), ==, t);
        g_assert_cmpint(note_type_from_badge(note_type_badge(t)), ==, t);
    }
    g_assert_cmpstr(note_type_to_str(NOTE_TYPE_MARKDOWN), ==, "markdown");
    /* Former name of Markdown notes in notes.xml */
    g_assert_cmpint(note_type_from_str("rich", "x.txt"), ==, NOTE_TYPE_MARKDOWN);
    g_assert_cmpint(note_type_from_str("nope", "x.org"), ==, NOTE_TYPE_ORG);
    g_assert_cmpint(note_type_from_str(NULL, "x.md"), ==, NOTE_TYPE_MARKDOWN);
    g_assert_cmpint(note_type_from_badge("nope"), ==, NOTE_TYPE_TEXT);
    g_assert_cmpint(note_type_from_badge(NULL), ==, NOTE_TYPE_TEXT);
}

static void test_names(void) {
    g_assert_cmpstr(note_type_ext(NOTE_TYPE_MARKDOWN), ==, ".md");
    g_assert_cmpstr(note_type_ext(NOTE_TYPE_ORG), ==, ".org");
    g_assert_cmpstr(note_type_ext(NOTE_TYPE_TEXT), ==, ".txt");
    g_assert_cmpstr(note_type_name(NOTE_TYPE_MARKDOWN), ==, "Markdown");
    g_assert_cmpstr(note_type_name(NOTE_TYPE_COUNT), ==, "Plain Text");
    g_assert_cmpstr(note_type_badge(NOTE_TYPE_TEXT), ==, "");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/note_types/from_filename", test_from_filename);
    g_test_add_func("/note_types/has_note_ext", test_has_note_ext);
    g_test_add_func("/note_types/str", test_str);
    g_test_add_func("/note_types/names", test_names);
    return g_test_run();
}
