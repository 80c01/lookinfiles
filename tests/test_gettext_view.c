/* Unit tests for src/gettext_view.c (.po and .mo catalogs). */
#include "gettext_view.h"

#include <string.h>

static const GettextMessage* message(const GettextCatalog* c, guint i) {
    return &g_array_index(c->messages, GettextMessage, i);
}

static void test_po(void) {
    gchar* text = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/preview/fr.po", &text, &len, NULL));
    GError* error = NULL;
    g_autoptr(GettextCatalog) c = gettext_parse_po(text, len, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(c->messages->len, ==, 5);
    g_assert_cmpstr(message(c, 0)->id, ==, "Open");
    g_assert_cmpstr(message(c, 0)->translation, ==, "Ouvrir");
    g_assert_cmpstr(message(c, 1)->context, ==, "menu");
    g_assert_cmpstr(message(c, 1)->translation, ==, "Fichier");
    g_assert_true(message(c, 2)->fuzzy);
    g_assert_false(message(c, 3)->fuzzy);
    g_assert_cmpstr(message(c, 3)->plural, ==, "%d files");
    g_assert_cmpstr(message(c, 3)->translation, ==, "%d fichier");
    g_assert_cmpstr(message(c, 4)->id, ==, "A long message on two lines");
    g_assert_cmpstr(message(c, 4)->translation, ==, "");
    gchar* lang = gettext_header_field(c, "Language");
    g_assert_cmpstr(lang, ==, "fr");
    g_free(lang);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    gettext_view_append(doc, c);
    g_assert_nonnull(strstr(doc->text->str, "Translator     Ada Lovelace <ada@example.org>"));
    g_assert_nonnull(strstr(doc->text->str, "3 translated, 1 fuzzy, 1 untranslated"));
    g_assert_nonnull(strstr(doc->text->str, "File [menu]"));
    g_assert_nonnull(strstr(doc->text->str, "Aperçu (fuzzy)"));
    g_free(text);
}

static void test_mo(void) {
    gchar* data = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/preview/fr.mo", &data, &len, NULL));
    GError* error = NULL;
    g_autoptr(GettextCatalog) c = gettext_parse_mo((const guchar*)data, len, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(c->messages->len, ==, 3);
    g_assert_cmpstr(message(c, 0)->id, ==, "%d file");
    g_assert_cmpstr(message(c, 0)->plural, ==, "%d files");
    g_assert_cmpstr(message(c, 0)->translation, ==, "%d fichier");
    g_assert_cmpstr(message(c, 2)->context, ==, "menu");
    gchar* plural = gettext_header_field(c, "Plural-Forms");
    g_assert_cmpstr(plural, ==, "nplurals=2; plural=(n > 1);");
    g_free(plural);
    g_assert_null(gettext_parse_mo((const guchar*)"nope", 4, NULL));
    /* Truncated tables stop early */
    g_autoptr(GettextCatalog) cut = gettext_parse_mo((const guchar*)data, 40, NULL);
    g_assert_cmpuint(cut->messages->len, ==, 0);
    g_free(data);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/gettext_view/po", test_po);
    g_test_add_func("/gettext_view/mo", test_mo);
    return g_test_run();
}
