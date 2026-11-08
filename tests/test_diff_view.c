/* Unit tests for src/formats/diff_view.c (unified diff summary and colours). */
#include "formats/diff_view.h"

#include <string.h>

static gchar* read_fixture(const gchar* name, gsize* len) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, len, NULL));
    g_free(path);
    return text;
}

static void test_summary_git_patch(void) {
    gsize len;
    g_autofree gchar* text = read_fixture("fix-parser.patch", &len);
    g_autoptr(DiffSummary) s = diff_summarize(text, len);
    g_assert_nonnull(s);
    g_assert_cmpstr(s->subject, ==, "[PATCH] Fix the parser on empty input");
    g_assert_cmpuint(s->files->len, ==, 2);
    const DiffFileStat* f = &g_array_index(s->files, DiffFileStat, 0);
    g_assert_cmpstr(f->path, ==, "src/parser.c");
    /* The removed "--- ..." line inside the hunk is counted, not a header */
    g_assert_cmpuint(f->added, ==, 4);
    g_assert_cmpuint(f->removed, ==, 2);
    g_assert_cmpstr(g_array_index(s->files, DiffFileStat, 1).path, ==, "tests/empty");
    g_assert_cmpuint(s->added, ==, 4);
    g_assert_cmpuint(s->removed, ==, 2);
}

static void test_summary_plain_diff(void) {
    const gchar* text = "--- old/a.txt\t2026-01-01\n+++ new/a.txt\t2026-01-02\n@@ -1 +1,2 @@\n-x\n+y\n+z\n";
    g_autoptr(DiffSummary) s = diff_summarize(text, strlen(text));
    g_assert_nonnull(s);
    g_assert_cmpuint(s->files->len, ==, 1);
    g_assert_cmpstr(g_array_index(s->files, DiffFileStat, 0).path, ==, "new/a.txt");
    g_assert_cmpuint(s->added, ==, 2);
    g_assert_cmpuint(s->removed, ==, 1);
    /* Deleted file: the old path is kept */
    const gchar* del = "diff --git a/gone.c b/gone.c\ndeleted file mode 100644\n--- a/gone.c\n+++ /dev/null\n@@ -1 +0,0 @@\n-int x;\n";
    g_autoptr(DiffSummary) d = diff_summarize(del, strlen(del));
    g_assert_cmpstr(g_array_index(d->files, DiffFileStat, 0).path, ==, "gone.c");
    /* Binary */
    const gchar* bin = "diff --git a/i.png b/i.png\nindex 1..2 100644\nBinary files a/i.png and b/i.png differ\n";
    g_autoptr(DiffSummary) b = diff_summarize(bin, strlen(bin));
    g_assert_nonnull(b);
    g_assert_true(g_array_index(b->files, DiffFileStat, 0).binary);
}

static void test_not_a_diff(void) {
    const gchar* text = "just some text\n--- a list\n";
    g_assert_null(diff_summarize(text, strlen(text)));
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_false(diff_view_append(doc, text, strlen(text)));
    g_assert_cmpuint(doc->text->len, ==, 0);
}

/* Style of the line that starts with prefix */
static PreviewStyle line_style(const PreviewDoc* doc, const gchar* prefix) {
    const gchar* at = strstr(doc->text->str, prefix);
    g_assert_nonnull(at);
    gint offset = (gint)g_utf8_pointer_to_offset(doc->text->str, at);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        if (s->start == offset) {
            return s->style;
        }
    }
    return PREVIEW_STYLE_PLAIN;
}

static void test_styles(void) {
    gsize len;
    g_autofree gchar* text = read_fixture("fix-parser.patch", &len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(diff_view_append(doc, text, len));
    g_assert_true(g_str_has_prefix(doc->text->str, "[PATCH] Fix the parser on empty input\n2 files changed, +4 −2\n"));
    g_assert_cmpint(line_style(doc, "@@ -10,7"), ==, PREVIEW_STYLE_HUNK);
    g_assert_cmpint(line_style(doc, "+    if (len == 0)"), ==, PREVIEW_STYLE_ADDED);
    g_assert_cmpint(line_style(doc, "--- this removed"), ==, PREVIEW_STYLE_REMOVED);
    g_assert_cmpint(line_style(doc, "diff --git a/src"), ==, PREVIEW_STYLE_FILE);
    g_assert_cmpint(line_style(doc, "index 1a2b3c4"), ==, PREVIEW_STYLE_DIM);
    g_assert_cmpint(line_style(doc, "         i++;"), ==, PREVIEW_STYLE_PLAIN);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/diff_view/summary_git_patch", test_summary_git_patch);
    g_test_add_func("/diff_view/summary_plain_diff", test_summary_plain_diff);
    g_test_add_func("/diff_view/not_a_diff", test_not_a_diff);
    g_test_add_func("/diff_view/styles", test_styles);
    return g_test_run();
}
