/* Unit tests for src/chrome/user_guide_search.c: searching the user guide
 * by section, matches without case or accents, the excerpts and the
 * results page. Headless. */
#include "chrome/user_guide_search.h"
#include "chrome/user_guide.h"

#include <string.h>

static void test_plain(void) {
    struct {
        const gchar* in;
        const gchar* out;
    } cases[] = {
        {"## The *editor*: tabs", "The editor: tabs"},
        {"- Press `Ctrl+F` to **search**", "Press Ctrl+F to search"},
        {"1. See [the explorer](explorer.md#keys).", "See the explorer."},
        {"| `F1` | Open the *guide* |", "F1 · Open the guide"},
        {"|---|:--:|", ""},
        {"Reads only PREVIEW_BYTES, _not_ more", "Reads only PREVIEW_BYTES, not more"},
        {"> Note: a \\* star", "Note: a * star"},
        {"Line with a break\\", "Line with a break"},
        {"Go to <https://example.org> now", "Go to https://example.org now"},
        {"![logo](logo.png) text", "logo text"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar* got = user_guide_plain(cases[i].in);
        g_assert_cmpstr(got, ==, cases[i].out);
        g_free(got);
    }
}

static void test_escape(void) {
    gchar* got = user_guide_md_escape("a*b_[c](d) $x$ `e`");
    g_assert_cmpstr(got, ==, "a\\*b\\_\\[c](d) \\$x\\$ \\`e\\`");
    g_free(got);
}

static void test_ranges(void) {
    /* Case, accents, typographic apostrophe; offsets in characters */
    GArray* r = user_guide_match_ranges("L’Éditeur et l'éditeur", "l'editeur");
    g_assert_cmpuint(r->len, ==, 2);
    g_assert_cmpint(g_array_index(r, gint, 0), ==, 0);
    g_assert_cmpint(g_array_index(r, gint, 1), ==, 9);
    g_assert_cmpint(g_array_index(r, gint, 2), ==, 13);
    g_assert_cmpint(g_array_index(r, gint, 3), ==, 22);
    g_array_unref(r);
    /* Overlapping words merge */
    r = user_guide_match_ranges("settings", "set settings");
    g_assert_cmpuint(r->len, ==, 1);
    g_assert_cmpint(g_array_index(r, gint, 1), ==, 8);
    g_array_unref(r);
    r = user_guide_match_ranges("anything", "   ");
    g_assert_cmpuint(r->len, ==, 0);
    g_array_unref(r);
}

static void test_matches_all(void) {
    g_assert_true(user_guide_matches_all("Bookmark selected entry · Ctrl+D", "ctrl+d BOOKMARK"));
    g_assert_true(user_guide_matches_all("Créer un signet", "creer"));
    g_assert_false(user_guide_matches_all("Bookmark selected entry", "bookmark folder"));
    g_assert_true(user_guide_matches_all("anything", "  "));
    g_assert_true(user_guide_matches_all(NULL, NULL));
}

static const gchar* DOC =
    "# The Editor\n"
    "\n"
    "Intro about tabs.\n"
    "\n"
    "## Vim mode\n"
    "\n"
    "Press `i` to insert, *Escape* to leave.\n"
    "```\n"
    "## not a heading: vim\n"
    "```\n"
    "## Saving files\n"
    "\n"
    "Ctrl+S saves the file; vim users type :w to save.\n";

static UserGuideHit* hit_at(GPtrArray* hits, const gchar* heading) {
    for (guint i = 0; i < hits->len; i++) {
        UserGuideHit* h = g_ptr_array_index(hits, i);
        if (g_strcmp0(h->heading, heading) == 0) return h;
    }
    return NULL;
}

static void test_sections(void) {
    GPtrArray* hits = g_ptr_array_new_with_free_func(user_guide_hit_free);
    user_guide_search_text("user-guide/editor.md", DOC, "vim", hits);
    g_assert_cmpuint(hits->len, ==, 2);
    UserGuideHit* vim = hit_at(hits, "Vim mode");
    g_assert_nonnull(vim);
    g_assert_cmpstr(vim->anchor, ==, "vim-mode");
    g_assert_cmpstr(vim->page_title, ==, "The Editor");
    g_assert_cmpstr(vim->page, ==, "user-guide/editor.md");
    UserGuideHit* save = hit_at(hits, "Saving files");
    g_assert_nonnull(save);
    g_assert_cmpstr(save->excerpt, ==, "Ctrl+S saves the file; **vim** users type :w to save.");
    /* The heading counts more than the text */
    g_assert_cmpint(vim->score, >, save->score);
    g_ptr_array_set_size(hits, 0);

    /* Every word, in any order; the page title for the top section */
    user_guide_search_text("user-guide/editor.md", DOC, "SAVE ctrl+s", hits);
    g_assert_cmpuint(hits->len, ==, 1);
    g_ptr_array_set_size(hits, 0);
    user_guide_search_text("user-guide/editor.md", DOC, "editor tabs", hits);
    g_assert_cmpuint(hits->len, ==, 1);
    g_assert_null(((UserGuideHit*)g_ptr_array_index(hits, 0))->heading);
    g_assert_null(((UserGuideHit*)g_ptr_array_index(hits, 0))->anchor);
    g_ptr_array_set_size(hits, 0);
    user_guide_search_text("user-guide/editor.md", DOC, "vim nowhere", hits);
    g_assert_cmpuint(hits->len, ==, 0);
    g_ptr_array_unref(hits);
}

static void test_long_excerpt(void) {
    GString* doc = g_string_new("# T\n\n");
    for (gint i = 0; i < 60; i++) g_string_append(doc, "word ");
    g_string_append(doc, "needle");
    for (gint i = 0; i < 60; i++) g_string_append(doc, " word");
    g_string_append_c(doc, '\n');
    GPtrArray* hits = g_ptr_array_new_with_free_func(user_guide_hit_free);
    user_guide_search_text("p.md", doc->str, "needle", hits);
    g_assert_cmpuint(hits->len, ==, 1);
    const gchar* ex = ((UserGuideHit*)g_ptr_array_index(hits, 0))->excerpt;
    g_assert_true(g_str_has_prefix(ex, "…"));
    g_assert_true(g_str_has_suffix(ex, "…"));
    g_assert_nonnull(strstr(ex, "**needle**"));
    g_assert_cmpint(g_utf8_strlen(ex, -1), <=, 200 + 2 + 4);
    g_ptr_array_unref(hits);
    g_string_free(doc, TRUE);
}

/* The built-in guide: results open real pages and anchors */
static void test_guide(void) {
    GPtrArray* hits = user_guide_search("bookmark");
    g_assert_cmpuint(hits->len, >, 0);
    g_assert_cmpuint(hits->len, <=, USER_GUIDE_SEARCH_MAX_HITS);
    for (guint i = 0; i < hits->len; i++) {
        UserGuideHit* h = g_ptr_array_index(hits, i);
        if (i > 0) g_assert_cmpint(((UserGuideHit*)g_ptr_array_index(hits, i - 1))->score, >=, h->score);
        gchar* text = user_guide_read(h->page);
        g_assert_nonnull(text);
        g_free(text);
    }
    g_ptr_array_unref(hits);
    hits = user_guide_search("zzqqxx-not-in-the-guide");
    g_assert_cmpuint(hits->len, ==, 0);
    g_ptr_array_unref(hits);

    gchar* page = user_guide_search_page("bookmark");
    g_assert_true(g_str_has_prefix(page, "# "));
    g_assert_nonnull(strstr(page, "](user-guide/bookmarks.md"));
    g_free(page);
    page = user_guide_search_page("a*b");
    g_assert_nonnull(strstr(page, "a\\*b"));
    g_free(page);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/user_guide_search/plain", test_plain);
    g_test_add_func("/user_guide_search/escape", test_escape);
    g_test_add_func("/user_guide_search/ranges", test_ranges);
    g_test_add_func("/user_guide_search/matches_all", test_matches_all);
    g_test_add_func("/user_guide_search/sections", test_sections);
    g_test_add_func("/user_guide_search/long_excerpt", test_long_excerpt);
    g_test_add_func("/user_guide_search/guide", test_guide);
    return g_test_run();
}
