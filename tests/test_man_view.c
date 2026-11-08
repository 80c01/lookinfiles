/* Unit tests for src/formats/man_view.c (roff man pages). */
#include "formats/man_view.h"

#include <string.h>

static gchar* styled_text(const PreviewDoc* doc, PreviewStyle style, guint nth) {
    guint seen = 0;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        if (s->style == style && seen++ == nth) {
            const gchar* a = g_utf8_offset_to_pointer(doc->text->str, s->start);
            const gchar* b = g_utf8_offset_to_pointer(doc->text->str, s->end);
            return g_strndup(a, (gsize)(b - a));
        }
    }
    return NULL;
}

static void test_plain(void) {
    gchar* s = man_view_plain("\\fBbold\\fR \\- \\(em \\(bu \\[u00E9] \\e \\&. \\*(lqq\\*(rq \\s-1SMALL\\s0");
    g_assert_cmpstr(s, ==, "bold - — • é \\ . “q” SMALL");
    g_free(s);
}

static void test_page(void) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", "lookin.1", NULL);
    gchar* text = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(path, &text, &len, NULL));
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(man_view_append(doc, text, len));
    const gchar* out = doc->text->str;
    g_assert_true(g_str_has_prefix(out, "LOOKIN(1)   User Commands\nLookin 1.4 · 2026-10-29\n\nNAME\nlookin - look in your files\n"));
    /* Filled paragraphs: lines joined */
    g_assert_nonnull(strstr(out, "Lookin is a file explorer that previews files in a blink. It shows “most” formats — text, images, archives.\n"));
    g_assert_nonnull(strstr(out, "\nlookin [-f] [-s WxH] [directory]\n"));
    /* Tagged paragraphs: tag line, then the body */
    g_assert_nonnull(strstr(out, "\n-f, --fullscreen\nStart in full screen.\n"));
    g_assert_nonnull(strstr(out, "Window size, e.g. 1920x1080.\n"));
    /* No-fill block keeps its lines */
    g_assert_nonnull(strstr(out, "lookin ~/projects\nlookin -s 1280x800 .\n"));
    g_assert_nonnull(strstr(out, "\n•\nFirst item.\n"));
    g_assert_nonnull(strstr(out, "ls(1), file(1)"));
    g_autofree gchar* section = styled_text(doc, PREVIEW_STYLE_SECTION, 0);
    g_assert_cmpstr(section, ==, "NAME");
    g_autofree gchar* sub = styled_text(doc, PREVIEW_STYLE_SECTION, 4);
    g_assert_cmpstr(sub, ==, "Examples");
    g_autofree gchar* bold = styled_text(doc, PREVIEW_STYLE_BOLD, 0);
    g_assert_cmpstr(bold, ==, "lookin");
    g_autofree gchar* italic = styled_text(doc, PREVIEW_STYLE_ITALIC, 0);
    g_assert_cmpstr(italic, ==, "WxH");
    g_free(text);
    g_free(path);
}

static void test_not_roff(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_false(man_view_append(doc, "plain text\n", 11));
    g_assert_cmpuint(doc->text->len, ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/man_view/plain", test_plain);
    g_test_add_func("/man_view/page", test_page);
    g_test_add_func("/man_view/not_roff", test_not_roff);
    return g_test_run();
}
