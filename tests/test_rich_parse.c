/* Unit tests for the HTML and Org parsers into MdOut (src/markup/html.c,
 * src/org/org_render.c): text, tags, objects and Org preview lines */
#include <string.h>
#include "markup/html.h"
#include "org/org_render.h"

/* TRUE if out tags exactly the text expected with tag */
static gboolean has_span(const MdOut* out, const gchar* tag, const gchar* expected) {
    for (guint i = 0; i < out->spans->len; i++) {
        const MdSpan* s = &g_array_index(out->spans, MdSpan, i);
        if (strcmp(s->tag, tag) != 0) {
            continue;
        }
        const gchar* a = g_utf8_offset_to_pointer(out->text->str, s->start);
        const gchar* b = g_utf8_offset_to_pointer(out->text->str, s->end);
        if ((gsize)(b - a) == strlen(expected) && strncmp(a, expected, (gsize)(b - a)) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void test_html(void) {
    g_autoptr(MdOut) out = html_parse("<h1>Titre</h1><p>Un <b>gras</b> et <i>é</i>.</p>"
                                      "<img src=\"/no/such.png\" alt=\"pic\">",
                                      NULL, FALSE, NULL, 0);
    g_assert_nonnull(strstr(out->text->str, "Titre"));
    g_assert_true(has_span(out, "html_bold", "gras"));
    g_assert_nonnull(strstr(out->text->str, "[pic]"));
    /* The missing picture is a glyph object on its own character */
    g_assert_cmpuint(out->objects->len, ==, 1);
    const MdObject* glyph = &g_array_index(out->objects, MdObject, 0);
    g_assert_cmpint(glyph->kind, ==, MD_OBJECT_GLYPH);
    const gchar* at = g_utf8_offset_to_pointer(out->text->str, glyph->offset);
    g_assert_true(g_str_has_prefix(at, MD_OUT_OBJECT_CHAR));
}

static void test_org_lines(void) {
    const gchar* org = "#+TITLE: Doc\n"
                       "* TODO [#A] Tâche :work:\n"
                       "Texte é *gras* fin.\n"
                       "- [X] fait\n"
                       "** Sous-titre\n"
                       "corps\n";
    GPtrArray* lines = NULL;
    g_autoptr(MdOut) out = org_render_parse(org, NULL, FALSE, NULL, 0, NULL, 0, &lines);
    g_assert_true(has_span(out, "org_title", "Doc\n"));
    g_assert_true(has_span(out, "org_todo", " TODO "));
    g_assert_true(has_span(out, "org_prio_a", "[#A] "));
    g_assert_true(has_span(out, "org_tag", ":work:"));
    g_assert_true(has_span(out, "org_bold", "gras"));
    g_assert_true(has_span(out, "org_checkbox_checked", "☑ "));
    /* Multi-byte characters are kept whole */
    g_assert_nonnull(strstr(out->text->str, "  Texte é gras fin.\n"));
    g_assert_true(g_utf8_validate(out->text->str, -1, NULL));

    /* Preview lines of the heading, text, item, subheading, body and end */
    gchar** shown = g_strsplit(out->text->str, "\n", -1);
    gint expected_files[] = {1, 2, 3, 4, 5, 6}; /* 6: the empty last line */
    g_assert_cmpuint(lines->len, ==, G_N_ELEMENTS(expected_files));
    for (guint i = 0; i < lines->len; i++) {
        const OrgLineInfo* info = g_ptr_array_index(lines, i);
        g_assert_cmpint(info->file_line, ==, expected_files[i]);
        g_assert_cmpint(info->buffer_line, <, (gint)g_strv_length(shown));
    }
    const OrgLineInfo* heading = g_ptr_array_index(lines, 0);
    g_assert_cmpint(heading->heading_level, ==, 1);
    g_assert_cmpstr(heading->todo_state, ==, "TODO");
    g_assert_true(g_str_has_prefix(shown[heading->buffer_line], "▼  TODO  [#A] Tâche  :work:"));
    const OrgLineInfo* item = g_ptr_array_index(lines, 2);
    g_assert_true(item->is_checkbox && item->checkbox_done);
    g_assert_true(g_str_has_prefix(shown[item->buffer_line], "  ☑ fait"));
    g_strfreev(shown);
    g_ptr_array_unref(lines);
}

static void test_org_fold(void) {
    const gchar* org = "* Un\ncorps\n** Deux\n* Trois\n";
    GHashTable* folded = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_hash_table_add(folded, GINT_TO_POINTER(0));
    GPtrArray* lines = NULL;
    g_autoptr(MdOut) out = org_render_parse(org, NULL, FALSE, NULL, 0, folded, ORG_GLOBAL_SHOW_ALL, &lines);
    g_assert_cmpstr(out->text->str, ==, "▶ Un ...\n▼ Trois\n  \n"); /* last: the empty end line */
    g_assert_true(has_span(out, "org_fold_cookie", " ..."));
    g_assert_cmpuint(lines->len, ==, 3);
    g_assert_cmpint(((OrgLineInfo*)g_ptr_array_index(lines, 1))->buffer_line, ==, 1);
    g_assert_cmpint(((OrgLineInfo*)g_ptr_array_index(lines, 1))->file_line, ==, 3);
    g_ptr_array_unref(lines);
    g_hash_table_unref(folded);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/rich_parse/html", test_html);
    g_test_add_func("/rich_parse/org_lines", test_org_lines);
    g_test_add_func("/rich_parse/org_fold", test_org_fold);
    return g_test_run();
}
