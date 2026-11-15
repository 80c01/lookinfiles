/* Unit tests for src/formats/xml_view.c (XML pretty-printing). */
#include "formats/xml_view.h"

#include <string.h>

static gchar* pretty_with(const gchar* xml, gboolean cut, const PreviewFold* fold, XmlViewInfo* info) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = xml_view_append(doc, xml, strlen(xml), cut, fold, info, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    return g_strdup(doc->text->str);
}

static gchar* pretty(const gchar* xml, XmlViewInfo* info) {
    return pretty_with(xml, FALSE, NULL, info);
}

static void test_pretty(void) {
    XmlViewInfo info;
    g_autofree gchar* out = pretty("<?xml version=\"1.0\"?>\n<!-- feed -->\n<feed xmlns=\"http://www.w3.org/2005/Atom\">"
                                   "<title type='text'>News &amp; notes</title><entry id=\"1\"><empty/><none></none>"
                                   "<p>Hello <b>you</b>\n  there</p></entry></feed>", &info);
    g_assert_cmpstr(out, ==,
                    "<?xml version=\"1.0\"?>\n"
                    "<!-- feed -->\n"
                    "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
                    "  <title type='text'>News &amp; notes</title>\n"
                    "  <entry id=\"1\">\n"
                    "    <empty/>\n"
                    "    <none></none>\n"
                    "    <p>\n"
                    "      Hello\n"
                    "      <b>you</b>\n"
                    "      there\n"
                    "    </p>\n"
                    "  </entry>\n"
                    "</feed>\n");
    g_assert_cmpuint(info.n_elements, ==, 7);
    g_assert_cmpstr(info.root, ==, "feed");
    g_assert_false(info.cut);
}

static void test_sections(void) {
    /* DOCTYPE with an internal subset, CDATA kept as written, attributes on several lines */
    g_autofree gchar* out = pretty("<!DOCTYPE note [ <!ENTITY a \"x>y\"> ]>\n<note\n   a=\"1\"\tb='2'>"
                                   "<![CDATA[if (a < b) {\n    go();\n}]]><?php echo 1; ?></note>", NULL);
    g_assert_cmpstr(out, ==,
                    "<!DOCTYPE note [ <!ENTITY a \"x>y\"> ]>\n"
                    "<note a=\"1\" b='2'>\n"
                    "  <![CDATA[if (a < b) {\n"
                    "    go();\n"
                    "}]]>\n"
                    "  <?php echo 1; ?>\n"
                    "</note>\n");
}

static void test_styles(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(xml_view_append(doc, "<a k=\"v\"><!--c--></a>", 21, FALSE, NULL, NULL, NULL));
    gboolean tag = FALSE, key = FALSE, value = FALSE, comment = FALSE;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        tag |= (s->style == PREVIEW_STYLE_KEYWORD && s->end - s->start == 1);
        key |= (s->style == PREVIEW_STYLE_KEY && s->end - s->start == 1);
        value |= (s->style == PREVIEW_STYLE_STRING && s->end - s->start == 3);
        comment |= (s->style == PREVIEW_STYLE_COMMENT && s->end - s->start == 8);
    }
    g_assert_true(tag && key && value && comment);
}

static void test_errors(void) {
    const gchar* bad[] = {
        "", "plain text", "<a>", "<a></b>", "</a>", "<a b></a>", "<a b=1></a>", "<a b=\"1\"c=\"2\"/>",
        "<a>x</a> trailing", "<a><!-- open</a>", "<![CDATA[x]]>", "<a>1 < 2</a>", "<!-- only a comment -->",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(PreviewDoc) doc = preview_doc_new();
        GError* error = NULL;
        g_assert_false(xml_view_append(doc, bad[i], strlen(bad[i]), FALSE, NULL, NULL, &error));
        g_assert_nonnull(error);
        g_assert_cmpuint(doc->text->len, ==, 0);
        g_error_free(error);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_false(xml_view_append(doc, "<a>\n  <b></c>\n</a>", 18, FALSE, NULL, NULL, &error));
    g_assert_cmpstr(error->message, ==, "line 2, column 6: expected </b>");
    g_error_free(error);
}

/* Path of the value on the line holding text */
static const gchar* path_of(const PreviewDoc* doc, const gchar* text) {
    const gchar* at = strstr(doc->text->str, text);
    g_assert_nonnull(at);
    return preview_doc_path_at(doc, (gint)g_utf8_pointer_to_offset(doc->text->str, at));
}

static void test_paths_and_folds(void) {
    const gchar* xml = "<?xml version=\"1.0\"?><r><s:item n=\"é\"><v>1</v>text\nmore</s:item><s:item><v>2</v>"
                       "<w><x/></w></s:item><one><v>3</v><v>4</v></one></r>";
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(xml_view_append(doc, xml, strlen(xml), FALSE, NULL, NULL, NULL));
    g_assert_null(path_of(doc, "<?xml"));
    g_assert_cmpstr(path_of(doc, "<r>"), ==, "/r");
    g_assert_cmpstr(path_of(doc, "<s:item n"), ==, "/r/s:item[1]");
    g_assert_cmpstr(path_of(doc, "<v>1"), ==, "/r/s:item[1]/v");
    g_assert_cmpstr(path_of(doc, "more"), ==, "/r/s:item[1]");
    g_assert_cmpstr(path_of(doc, "<x/>"), ==, "/r/s:item[2]/w/x");
    g_assert_cmpstr(path_of(doc, "<v>4"), ==, "/r/one/v[2]");
    g_assert_cmpstr(path_of(doc, "</r>"), ==, "/r");
    /* Blocks only: not <v>1</v> or <w><x/></w> on its own line... */
    const gchar* ids[] = { "/r/s:item[1]", "/r/s:item[2]/w", "/r/s:item[2]", "/r/one", "/r" };
    g_assert_cmpuint(doc->folds->len, ==, G_N_ELEMENTS(ids));
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
        g_assert_cmpstr(g_array_index(doc->folds, PreviewFoldPoint, i).id, ==, ids[i]);
    }
    /* The children of the outer element go to the tagbar: name and attributes */
    guint outline = 0;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        if (span->style == PREVIEW_STYLE_OUTLINE1) {
            outline++;
            g_assert_cmpint(doc->text->str[g_utf8_offset_to_pointer(doc->text->str, span->start) - doc->text->str - 1],
                            ==, '<');
        }
    }
    g_assert_cmpuint(outline, ==, 3);

    g_autoptr(PreviewFold) fold = preview_fold_new(1);
    preview_fold_toggle(fold, "/r/one");
    g_autofree gchar* folded = pretty_with(xml, FALSE, fold, NULL);
    g_assert_cmpstr(folded, ==,
                    "<?xml version=\"1.0\"?>\n"
                    "<r>\n"
                    "  <s:item n=\"é\">…</s:item>  1 element\n"
                    "  <s:item>…</s:item>  2 elements\n"
                    "  <one>\n"
                    "    <v>3</v>\n"
                    "    <v>4</v>\n"
                    "  </one>\n"
                    "</r>\n");
}

static void test_cut(void) {
    XmlViewInfo info;
    /* In a tag, an attribute value, a comment, after some text */
    g_autofree gchar* in_tag = pretty_with("<a><b>1</b><c x=\"lo", TRUE, NULL, &info);
    g_assert_cmpstr(in_tag, ==, "<a>\n  <b>1</b>\n…\n");
    g_assert_true(info.cut);
    g_autofree gchar* in_comment = pretty_with("<a><b>1</b><!-- long", TRUE, NULL, NULL);
    g_assert_cmpstr(in_comment, ==, "<a>\n  <b>1</b>\n…\n");
    g_autofree gchar* in_text = pretty_with("<a><b>some te", TRUE, NULL, NULL);
    g_assert_cmpstr(in_text, ==, "<a>\n  <b>\n    some te\n  …\n…\n");
    g_autofree gchar* in_end_tag = pretty_with("<a><b>1</b", TRUE, NULL, NULL);
    g_assert_cmpstr(in_end_tag, ==, "<a>\n  <b>\n    1\n  …\n…\n");
    g_autofree gchar* empty = pretty_with("<a>", TRUE, NULL, NULL);
    g_assert_cmpstr(empty, ==, "<a>…\n");
    /* A whole document read: nothing cut; a real error is still one */
    g_autofree gchar* whole = pretty_with("<a>x</a>", TRUE, NULL, &info);
    g_assert_cmpstr(whole, ==, "<a>x</a>\n");
    g_assert_false(info.cut);
    g_autoptr(PreviewDoc) bad = preview_doc_new();
    g_assert_false(xml_view_append(bad, "<a></b><c>", 10, TRUE, NULL, NULL, NULL));
    g_autoptr(PreviewDoc) nothing = preview_doc_new();
    g_assert_false(xml_view_append(nothing, "<?xml version=\"1.0\"?><a", 23, TRUE, NULL, NULL, NULL));
}

static void test_auto_level(void) {
    /* 2,000 items of 3 levels: 12,002 lines open, 8,002 with two levels open */
    GString* big = g_string_new("<list>");
    for (gint i = 0; i < XML_VIEW_MAX_LINES / 5; i++) {
        g_string_append_printf(big, "<item id=\"%d\"><a>1</a><b><c>2</c></b></item>", i);
    }
    g_string_append(big, "</list>");
    XmlViewInfo info;
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(xml_view_append(doc, big->str, big->len, FALSE, NULL, &info, NULL));
    g_assert_cmpint(info.level, ==, 2);
    g_assert_true(g_str_has_prefix(doc->text->str, "Long document"));
    g_assert_false(preview_doc_fold_find(doc, "/list/item[7]")->folded);
    g_assert_true(preview_doc_fold_find(doc, "/list/item[7]/b")->folded);
    g_string_free(big, TRUE);
}

static void test_build(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(xml_view_build(doc, "<a><b/><b/></a>", 15, FALSE, NULL, "Note.", NULL));
    g_assert_cmpstr(doc->text->str, ==, "XML · <a> · 3 elements\nNote.\n\n<a>\n  <b/>\n  <b/>\n</a>\n");
    g_assert_true(doc->monospace);
    g_assert_nonnull(preview_doc_fold_find(doc, "/a"));
    g_assert_cmpstr(preview_doc_path_at(doc, (gint)g_utf8_strlen(doc->text->str, -1) - 2), ==, "/a");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xml_view/pretty", test_pretty);
    g_test_add_func("/xml_view/sections", test_sections);
    g_test_add_func("/xml_view/styles", test_styles);
    g_test_add_func("/xml_view/errors", test_errors);
    g_test_add_func("/xml_view/paths_and_folds", test_paths_and_folds);
    g_test_add_func("/xml_view/cut", test_cut);
    g_test_add_func("/xml_view/auto_level", test_auto_level);
    g_test_add_func("/xml_view/build", test_build);
    return g_test_run();
}
