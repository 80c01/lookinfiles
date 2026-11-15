/* Unit tests for src/formats/toml_view.c (TOML preview). */
#include "formats/toml_view.h"

#include <string.h>

static PreviewDoc* view(const gchar* toml, gboolean cut, const PreviewFold* fold, TomlViewInfo* info) {
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = toml_view_append(doc, toml, strlen(toml), cut, fold, info, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    return doc;
}

/* Path of the value on the line holding text */
static const gchar* path_of(const PreviewDoc* doc, const gchar* text) {
    const gchar* at = strstr(doc->text->str, text);
    g_assert_nonnull(at);
    return preview_doc_path_at(doc, (gint)g_utf8_pointer_to_offset(doc->text->str, at));
}

/* The text styled with style, joined by "|" */
static gchar* styled(const PreviewDoc* doc, PreviewStyle style) {
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        if (span->style == style) {
            const gchar* a = g_utf8_offset_to_pointer(doc->text->str, span->start);
            const gchar* b = g_utf8_offset_to_pointer(doc->text->str, span->end);
            g_string_append_printf(out, "%s%.*s", out->len > 0 ? "|" : "", (gint)(b - a), a);
        }
    }
    return g_string_free(out, FALSE);
}

static const gchar* SAMPLE =
    "# top\n"
    "title = \"demo\" # name\n"
    "when = 1979-05-27 07:32:00Z\n"
    "\n"
    "[servers]\n"
    "on = true\n"
    "[servers.alpha]\n"
    "ports = [ 1,\n"
    "  2 ]\n"
    "[[plugins]]\n"
    "name = 'é'\n"
    "[[plugins]]\n"
    "[plugins.options]\n"
    "\"odd key\".x = { a = 1, b = [2] }\n"
    "notes = \"\"\"\n"
    "two\n"
    "lines\"\"\"\n";

static void test_text_and_styles(void) {
    TomlViewInfo info;
    g_autoptr(PreviewDoc) doc = view(SAMPLE, FALSE, NULL, &info);
    g_assert_cmpstr(doc->text->str, ==, SAMPLE); /* as written */
    g_assert_cmpuint(info.n_tables, ==, 5);
    g_assert_cmpuint(info.n_keys, ==, 7);
    g_assert_false(info.cut);
    g_autofree gchar* keys = styled(doc, PREVIEW_STYLE_KEY);
    g_assert_cmpstr(keys, ==, "title|when|servers|on|servers.alpha|ports|plugins|name|plugins|plugins.options|"
                              "\"odd key\".x|a|b|notes");
    g_autofree gchar* strings = styled(doc, PREVIEW_STYLE_STRING);
    g_assert_cmpstr(strings, ==, "\"demo\"|'é'|\"\"\"|two|lines\"\"\"");
    g_autofree gchar* numbers = styled(doc, PREVIEW_STYLE_NUMBER);
    g_assert_cmpstr(numbers, ==, "1979-05-27 07:32:00Z|1|2|1|2");
    g_autofree gchar* comments = styled(doc, PREVIEW_STYLE_COMMENT);
    g_assert_cmpstr(comments, ==, "# top|# name");
    g_autofree gchar* keywords = styled(doc, PREVIEW_STYLE_KEYWORD);
    g_assert_cmpstr(keywords, ==, "[|]|true|[|]|[[|]]|[[|]]|[|]");
    /* Headers in the tagbar, by depth */
    g_autofree gchar* outline1 = styled(doc, PREVIEW_STYLE_OUTLINE1);
    g_autofree gchar* outline2 = styled(doc, PREVIEW_STYLE_OUTLINE2);
    g_assert_cmpstr(outline1, ==, "servers|plugins|plugins");
    g_assert_cmpstr(outline2, ==, "servers.alpha|plugins.options");
}

static void test_paths_and_folds(void) {
    g_autoptr(PreviewDoc) doc = view(SAMPLE, FALSE, NULL, NULL);
    g_assert_null(path_of(doc, "# top"));
    g_assert_cmpstr(path_of(doc, "title"), ==, "title");
    g_assert_cmpstr(path_of(doc, "[servers]"), ==, "servers");
    g_assert_cmpstr(path_of(doc, "on ="), ==, "servers.on");
    g_assert_cmpstr(path_of(doc, "ports"), ==, "servers.alpha.ports");
    g_assert_cmpstr(path_of(doc, "  2 ]"), ==, "servers.alpha.ports[1]");
    g_assert_cmpstr(path_of(doc, "name = 'é'"), ==, "plugins[0].name");
    g_assert_cmpstr(path_of(doc, "[plugins.options]"), ==, "plugins[1].options");
    g_assert_cmpstr(path_of(doc, "\"odd key\""), ==, "plugins[1].options.\"odd key\".x");
    g_assert_cmpstr(path_of(doc, "lines"), ==, "plugins[1].options.notes");
    const gchar* ids[] = { "servers.alpha.ports", "servers.alpha", "servers", "plugins[0]",
                           "plugins[1].options", "plugins[1]" };
    g_assert_cmpuint(doc->folds->len, ==, G_N_ELEMENTS(ids));
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
        g_assert_cmpstr(g_array_index(doc->folds, PreviewFoldPoint, i).id, ==, ids[i]);
    }

    /* Folded: the header line and its count; an array keeps its first line */
    g_autoptr(PreviewFold) fold = preview_fold_new(1);
    preview_fold_toggle(fold, "servers");
    preview_fold_toggle(fold, "servers.alpha"); /* the array stays folded by the level */
    g_autoptr(PreviewDoc) folded = view(SAMPLE, FALSE, fold, NULL);
    g_assert_cmpstr(folded->text->str, ==,
                    "# top\n"
                    "title = \"demo\" # name\n"
                    "when = 1979-05-27 07:32:00Z\n"
                    "\n"
                    "[servers]\n"
                    "on = true\n"
                    "[servers.alpha]\n"
                    "ports = [ 1,…  2 items\n"
                    "[[plugins]]  1 key\n"
                    "[[plugins]]  1 key\n");
    g_assert_true(preview_doc_fold_find(folded, "plugins[1]")->folded);
    g_assert_false(preview_doc_fold_find(folded, "servers")->folded);
    g_assert_null(preview_doc_fold_find(folded, "plugins[1].options"));
}

static void test_errors(void) {
    const gchar* bad[] = {
        "", "\n\n", "key: value\n", "key = some words\n", "[section\n", "a = \"open\n", "a = [1, 2\n",
        "a =\n", "a = 1 2\n", "[[a]\n", "= 1\n", "a = {b = 1\n", "a = yes\n", "plain text\n",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(PreviewDoc) doc = preview_doc_new();
        GError* error = NULL;
        g_assert_false(toml_view_append(doc, bad[i], strlen(bad[i]), FALSE, NULL, NULL, &error));
        g_assert_nonnull(error);
        g_assert_cmpuint(doc->text->len, ==, 0);
        g_error_free(error);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_false(toml_view_append(doc, "a = 1\nb = 2 3\n", 14, FALSE, NULL, NULL, &error));
    g_assert_cmpstr(error->message, ==, "line 2, column 7: expected the end of the line");
    g_error_free(error);
    /* Comments only, CRLF line ends, no final line break */
    g_autoptr(PreviewDoc) comments = view("# nothing yet\n", FALSE, NULL, NULL);
    g_autoptr(PreviewDoc) crlf = view("[a]\r\nb = 1\r\n", FALSE, NULL, NULL);
    g_assert_cmpstr(crlf->text->str, ==, "[a]\nb = 1\n");
    g_autoptr(PreviewDoc) last = view("a = 1", FALSE, NULL, NULL);
    g_assert_cmpstr(last->text->str, ==, "a = 1\n");
}

static void test_cut(void) {
    TomlViewInfo info;
    /* The incomplete last line is left out */
    g_autoptr(PreviewDoc) line = view("[a]\nb = 1\nc = \"lo", TRUE, NULL, &info);
    g_assert_cmpstr(line->text->str, ==, "[a]\nb = 1\n…\n");
    g_assert_false(info.cut);
    /* An array and a string still open at the cut */
    g_autoptr(PreviewDoc) array = view("[a]\nlist = [\n  1,\n  2,\n  3", TRUE, NULL, &info);
    g_assert_cmpstr(array->text->str, ==, "[a]\nlist = [\n  1,\n  2,\n…\n");
    g_assert_true(info.cut);
    g_assert_nonnull(preview_doc_fold_find(array, "a.list"));
    g_autoptr(PreviewDoc) text = view("s = \"\"\"\none\ntw", TRUE, NULL, &info);
    g_assert_cmpstr(text->text->str, ==, "s = \"\"\"\none\n…\n");
    g_assert_true(info.cut);
}

static void test_auto_level(void) {
    /* 2,000 tables of 6 lines: 12,000 lines open; the subtables folded: 6,000 */
    GString* big = g_string_new(NULL);
    for (gint i = 0; i < 2000; i++) {
        g_string_append_printf(big, "[t%d]\na = 1\n[t%d.sub]\nb = 2\nc = 3\nd = 4\n", i, i);
    }
    TomlViewInfo info;
    g_autoptr(PreviewDoc) doc = view(big->str, FALSE, NULL, &info);
    g_assert_cmpint(info.level, ==, 2);
    g_assert_true(g_str_has_prefix(doc->text->str, "Long document"));
    g_assert_true(preview_doc_fold_find(doc, "t7.sub")->folded);
    g_assert_false(preview_doc_fold_find(doc, "t7")->folded);
    g_string_free(big, TRUE);
}

static void test_build(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(toml_view_build(doc, "[a]\n[b]\n", 8, FALSE, NULL, NULL, NULL));
    g_assert_cmpstr(doc->text->str, ==, "TOML · 2 tables\n\n[a]\n[b]\n");
    g_assert_true(doc->monospace);
    g_assert_nonnull(preview_doc_fold_find(doc, "b"));
    g_autoptr(PreviewDoc) keys = preview_doc_new();
    g_assert_true(toml_view_build(keys, "a = 1\n", 6, FALSE, NULL, "Note.", NULL));
    g_assert_cmpstr(keys->text->str, ==, "TOML · 1 key\nNote.\n\na = 1\n");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/toml_view/text_and_styles", test_text_and_styles);
    g_test_add_func("/toml_view/paths_and_folds", test_paths_and_folds);
    g_test_add_func("/toml_view/errors", test_errors);
    g_test_add_func("/toml_view/cut", test_cut);
    g_test_add_func("/toml_view/auto_level", test_auto_level);
    g_test_add_func("/toml_view/build", test_build);
    return g_test_run();
}
