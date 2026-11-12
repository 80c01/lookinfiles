/* Unit tests for src/markup/md_out.c and the Markdown parser
 * (src/markup/markdown_parse.c): text, tag ranges, links and objects */
#include <string.h>
#include "markup/md_out.h"
#include "markup/markdown_parse.h"

/* The text of out from start to end (characters) */
static gchar* range_text(const MdOut* out, gint start, gint end) {
    const gchar* a = g_utf8_offset_to_pointer(out->text->str, start);
    const gchar* b = g_utf8_offset_to_pointer(out->text->str, end);
    return g_strndup(a, b - a);
}

/* TRUE if out tags exactly the text expected with tag */
static gboolean has_span(const MdOut* out, const gchar* tag, const gchar* expected) {
    for (guint i = 0; i < out->spans->len; i++) {
        const MdSpan* s = &g_array_index(out->spans, MdSpan, i);
        if (strcmp(s->tag, tag) == 0) {
            gchar* text = range_text(out, s->start, s->end);
            gboolean same = (strcmp(text, expected) == 0);
            g_free(text);
            if (same) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static const MdObject* find_object(const MdOut* out, MdObjectKind kind) {
    for (guint i = 0; i < out->objects->len; i++) {
        const MdObject* o = &g_array_index(out->objects, MdObject, i);
        if (o->kind == kind) {
            return o;
        }
    }
    return NULL;
}

static void test_offsets(void) {
    g_autoptr(MdOut) out = md_out_new();
    md_out_append(out, "é", -1);
    g_assert_cmpint(md_out_offset(out), ==, 1);
    md_out_append_tagged(out, "àb", "md_bold");
    md_out_tag_from(out, 3, "md_italic"); /* empty: ignored */
    md_out_add_glyph(out, "note", "md_alert_note");
    g_assert_cmpint(md_out_offset(out), ==, 5);
    g_assert_cmpstr(out->text->str, ==, "éàb" MD_OUT_OBJECT_CHAR " ");
    g_assert_cmpuint(out->spans->len, ==, 2);
    g_assert_true(has_span(out, "md_bold", "àb"));
    g_assert_true(has_span(out, "md_alert_note", MD_OUT_OBJECT_CHAR " "));
    const MdObject* glyph = find_object(out, MD_OBJECT_GLYPH);
    g_assert_nonnull(glyph);
    g_assert_cmpint(glyph->offset, ==, 3);
    g_assert_cmpstr(glyph->name, ==, "note");
}

static void test_inline(void) {
    g_autoptr(MdOut) out = markdown_parse("Un **gras *imbriqué* fin** et `c` et [lien](u.md) \\é &amp;.",
                                          NULL, FALSE, NULL, 0, TRUE);
    g_assert_cmpstr(out->text->str, ==, "Un gras imbriqué fin et c et lien é &.\n");
    g_assert_true(has_span(out, "md_bold", "gras imbriqué fin"));
    g_assert_true(has_span(out, "md_italic", "imbriqué"));
    g_assert_true(has_span(out, "md_inline_code", "c"));
    g_assert_true(has_span(out, "md_link", "lien"));
    g_assert_cmpuint(out->links->len, ==, 1);
    const MdLinkSpan* link = &g_array_index(out->links, MdLinkSpan, 0);
    g_assert_cmpstr(link->url, ==, "u.md");
    gchar* text = range_text(out, link->start, link->end);
    g_assert_cmpstr(text, ==, "lien");
    g_free(text);
    /* The whole text has the body tag */
    g_assert_true(has_span(out, "md_body", out->text->str));
}

static void test_blocks(void) {
    g_autoptr(MdOut) out = markdown_parse("# Titre\n\n- un\n2. deux\n\n> [!NOTE]\n> texte\n\n"
                                          "```c\nint x;\n```\n",
                                          NULL, FALSE, NULL, 0, TRUE);
    g_assert_true(has_span(out, "md_h1", "Titre\n"));
    g_assert_true(has_span(out, "md_rule", " \n"));
    g_assert_true(has_span(out, "md_li_ul_d0", "●\tun"));
    g_assert_true(has_span(out, "md_list_bullet", "2.\t"));
    g_assert_true(has_span(out, "md_alert_note", "Note\n"));
    g_assert_true(has_span(out, "md_code_block", "int x;\n"));
    const MdObject* code = find_object(out, MD_OBJECT_CODE);
    g_assert_nonnull(code);
    g_assert_cmpstr(code->name, ==, "c");
    g_assert_cmpstr(code->text, ==, "int x;\n");
    gchar* at = range_text(out, code->offset, code->offset + 6);
    g_assert_cmpstr(at, ==, "int x;");
    g_free(at);
    g_assert_nonnull(find_object(out, MD_OBJECT_GLYPH));
}

static void test_tables(void) {
    const gchar* md = "| a | b |\n|:--|--:|\n| 1 | **2** |\n";
    g_autoptr(MdOut) widgets = markdown_parse(md, NULL, FALSE, NULL, 0, TRUE);
    const MdObject* table = find_object(widgets, MD_OBJECT_TABLE);
    g_assert_nonnull(table);
    g_assert_cmpuint(table->cols, ==, 2);
    g_assert_cmpuint(table->rows->len, ==, 2);
    g_assert_cmpuint(table->aligns->len, ==, 2);
    gchar* at = range_text(widgets, table->offset, table->offset + 2);
    g_assert_cmpstr(at, ==, MD_OUT_OBJECT_CHAR "\n");
    g_free(at);

    g_autoptr(MdOut) text = markdown_parse(md, NULL, FALSE, NULL, 0, FALSE);
    g_assert_null(find_object(text, MD_OBJECT_TABLE));
    g_assert_nonnull(strstr(text->text->str, "│ 1   │ 2   │\n")); /* columns of at least 3 */
    g_assert_true(has_span(text, "md_table_header", "│ a   │ b   │\n"));
}

static void test_missing_image(void) {
    g_autoptr(MdOut) out = markdown_parse("![alt](/no/such/picture.png)", NULL, FALSE, NULL, 0, TRUE);
    g_assert_null(find_object(out, MD_OBJECT_IMAGE));
    g_assert_nonnull(find_object(out, MD_OBJECT_GLYPH));
    g_assert_true(has_span(out, "md_image", MD_OUT_OBJECT_CHAR " [alt]"));
}

static void test_preview_banner(void) {
    g_autoptr(MdOut) out = markdown_parse("x", NULL, TRUE, "big.md", 1234, TRUE);
    g_assert_true(g_str_has_prefix(out->text->str, "  [ PREVIEW ] big.md (1234 bytes total)"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/md_out/offsets", test_offsets);
    g_test_add_func("/md_out/inline", test_inline);
    g_test_add_func("/md_out/blocks", test_blocks);
    g_test_add_func("/md_out/tables", test_tables);
    g_test_add_func("/md_out/missing_image", test_missing_image);
    g_test_add_func("/md_out/preview_banner", test_preview_banner);
    return g_test_run();
}
