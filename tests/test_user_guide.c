/* Unit tests for src/user_guide.c: the user guide pages built into the
 * binary (docs/user-guide/ and the reference pages) and their titles.
 * Headless. */
#include "user_guide.h"

#include <gio/gio.h>
#include <string.h>

static void test_home(void) {
    gchar* text = user_guide_read(USER_GUIDE_HOME);
    g_assert_nonnull(text);
    gchar* title = user_guide_title(text);
    g_assert_cmpstr(title, ==, "LookinFiles User Guide");
    g_free(title);
    g_free(text);
}

static void test_pages(void) {
    gchar** pages = user_guide_pages();
    g_assert_nonnull(pages);
    gboolean home = FALSE, keyboard = FALSE;
    guint n = 0;
    for (gint i = 0; pages[i] != NULL; i++, n++) {
        if (g_strcmp0(pages[i], USER_GUIDE_HOME) == 0) home = TRUE;
        if (g_strcmp0(pages[i], "keyboard.md") == 0) keyboard = TRUE;
        if (i > 0) g_assert_cmpint(g_strcmp0(pages[i - 1], pages[i]), <, 0);
        /* Every page reads and has a title */
        gchar* text = user_guide_read(pages[i]);
        g_assert_nonnull(text);
        gchar* title = user_guide_title(text);
        g_assert_nonnull(title);
        g_assert_cmpstr(title, !=, "");
        g_free(title);
        g_free(text);
    }
    g_assert_true(home);
    g_assert_true(keyboard);
    g_assert_cmpuint(n, >, 10);
    g_strfreev(pages);
}

static void test_missing(void) {
    g_assert_null(user_guide_read("user-guide/nope.md"));
    g_assert_null(user_guide_read("../README.md"));
    g_assert_null(user_guide_read("user-guide/../keyboard.md"));
    g_assert_null(user_guide_read("/org/lookin/guide/keyboard.md"));
    g_assert_null(user_guide_read(""));
    g_assert_null(user_guide_read(NULL));
}

static void test_title(void) {
    gchar* t = user_guide_title("Intro\n\n#Not a title\n# The  Title  \ntext\n# Second\n");
    g_assert_cmpstr(t, ==, "The  Title");
    g_free(t);
    /* Headings inside code blocks and deeper levels are skipped */
    t = user_guide_title("```sh\n# comment\n```\n## Sub\n# Real");
    g_assert_cmpstr(t, ==, "Real");
    g_free(t);
    g_assert_null(user_guide_title("no heading\n"));
    g_assert_null(user_guide_title(""));
    g_assert_null(user_guide_title(NULL));
}

static void check_link(const gchar* from, const gchar* href, UserGuideLink kind, const gchar* page,
                       const gchar* anchor) {
    gchar* p = NULL;
    gchar* a = NULL;
    g_assert_cmpint(user_guide_resolve_link(from, href, &p, &a), ==, kind);
    g_assert_cmpstr(p, ==, page);
    g_assert_cmpstr(a, ==, anchor);
    g_free(p);
    g_free(a);
}

static void test_resolve_link(void) {
    const gchar* from = "user-guide/editor.md";
    check_link(from, "vim.md", USER_GUIDE_LINK_PAGE, "user-guide/vim.md", NULL);
    check_link(from, "./vim.md#options", USER_GUIDE_LINK_PAGE, "user-guide/vim.md", "options");
    check_link(from, "../keyboard.md#editor-tabs", USER_GUIDE_LINK_PAGE, "keyboard.md", "editor-tabs");
    check_link(from, "#splitting", USER_GUIDE_LINK_PAGE, from, "splitting");
    check_link(from, "README.md", USER_GUIDE_LINK_PAGE, USER_GUIDE_HOME, NULL);
    check_link("keyboard.md", "user-guide/README.md", USER_GUIDE_LINK_PAGE, USER_GUIDE_HOME, NULL);
    check_link("keyboard.md", "configuration.md#language-servers", USER_GUIDE_LINK_PAGE, "configuration.md",
               "language-servers");
    /* Outside the guide: named for a message */
    check_link(from, "../../README.md#requirements", USER_GUIDE_LINK_NONE, "README.md", NULL);
    check_link("keyboard.md", "architecture.md", USER_GUIDE_LINK_NONE, "architecture.md", NULL);
    check_link(from, "nope.md", USER_GUIDE_LINK_NONE, "user-guide/nope.md", NULL);
    check_link(from, "/etc/passwd", USER_GUIDE_LINK_NONE, NULL, NULL);
    check_link(from, "file:///etc/passwd", USER_GUIDE_LINK_NONE, NULL, NULL);
    check_link(from, "", USER_GUIDE_LINK_NONE, NULL, NULL);
    check_link(from, NULL, USER_GUIDE_LINK_NONE, NULL, NULL);
    /* Web */
    check_link(from, "https://ctags.io", USER_GUIDE_LINK_WEB, NULL, NULL);
    check_link(from, "mailto:a@b.c", USER_GUIDE_LINK_WEB, NULL, NULL);
}

static void test_anchor(void) {
    const struct { const gchar* heading; const gchar* anchor; } cases[] = {
        {"The global mode: Notepad, Emacs or Vim", "the-global-mode-notepad-emacs-or-vim"},
        {"Go to a file: `Ctrl+P`", "go-to-a-file-ctrlp"},
        {"Saved layouts: `Alt+L`", "saved-layouts-altl"},
        {"Org-mode editing", "org-mode-editing"},
        {"snake_case and Été", "snake_case-and-été"},
        {"", ""},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar* a = user_guide_anchor(cases[i].heading);
        g_assert_cmpstr(a, ==, cases[i].anchor);
        g_free(a);
    }
}

/* TRUE when markdown has a heading whose anchor is anchor */
static gboolean has_anchor(const gchar* markdown, const gchar* anchor) {
    gboolean found = FALSE, in_code = FALSE;
    gchar** lines = g_strsplit(markdown, "\n", -1);
    for (gint i = 0; lines[i] != NULL && !found; i++) {
        if (g_str_has_prefix(lines[i], "```")) in_code = !in_code;
        if (in_code || lines[i][0] != '#') continue;
        const gchar* t = lines[i];
        while (*t == '#') t++;
        if (*t != ' ') continue;
        gchar* a = user_guide_anchor(t + 1);
        found = strcmp(a, anchor) == 0;
        g_free(a);
    }
    g_strfreev(lines);
    return found;
}

/* Every link of the guide's own pages leads to a page of the guide (and
 * its anchor) or to the web: none is broken in the Help tab */
static void test_guide_links(void) {
    gchar** pages = user_guide_pages();
    GRegex* re = g_regex_new("\\]\\(([^)\\s]+)\\)", 0, 0, NULL);
    guint checked = 0;
    for (gint i = 0; pages[i] != NULL; i++) {
        if (!g_str_has_prefix(pages[i], "user-guide/")) continue;
        gchar* text = user_guide_read(pages[i]);
        GMatchInfo* m = NULL;
        g_regex_match(re, text, 0, &m);
        while (g_match_info_matches(m)) {
            gchar* href = g_match_info_fetch(m, 1);
            gchar* page = NULL;
            gchar* anchor = NULL;
            UserGuideLink kind = user_guide_resolve_link(pages[i], href, &page, &anchor);
            if (kind == USER_GUIDE_LINK_NONE) {
                g_error("%s: link %s leads outside the guide", pages[i], href);
            }
            if (kind == USER_GUIDE_LINK_PAGE && anchor != NULL) {
                gchar* target = user_guide_read(page);
                if (!has_anchor(target, anchor)) {
                    g_error("%s: link %s: no heading with that anchor", pages[i], href);
                }
                g_free(target);
            }
            checked++;
            g_free(page);
            g_free(anchor);
            g_free(href);
            g_match_info_next(m, NULL);
        }
        g_match_info_free(m);
        g_free(text);
    }
    g_assert_cmpuint(checked, >, 50);
    g_regex_unref(re);
    g_strfreev(pages);
}

/* The links of the lang translation, checked as /user_guide/guide_links
 * checks the English ones (every page must be translated). Anchors are
 * looked up in the translated target page, or in the English one when the
 * target has no translation (the reference pages). */
static guint check_links(const gchar* lang) {
    gchar** pages = user_guide_pages();
    GRegex* re = g_regex_new("\\]\\(([^)\\s]+)\\)", 0, 0, NULL);
    guint checked = 0;
    for (gint i = 0; pages[i] != NULL; i++) {
        if (!g_str_has_prefix(pages[i], "user-guide/")) continue;
        gchar* text = user_guide_read_in(pages[i], lang);
        if (text == NULL) {
            g_error("%s: no %s translation of the page", pages[i], lang);
        }
        gchar* title = user_guide_title(text);
        g_assert_nonnull(title);
        g_free(title);
        GMatchInfo* m = NULL;
        g_regex_match(re, text, 0, &m);
        while (g_match_info_matches(m)) {
            gchar* href = g_match_info_fetch(m, 1);
            gchar* page = NULL;
            gchar* anchor = NULL;
            UserGuideLink kind = user_guide_resolve_link(pages[i], href, &page, &anchor);
            if (kind == USER_GUIDE_LINK_NONE) {
                g_error("%s (%s): link %s leads outside the guide", pages[i], lang, href);
            }
            if (kind == USER_GUIDE_LINK_PAGE && anchor != NULL) {
                gchar* target = user_guide_read_in(page, lang);
                if (target == NULL) target = user_guide_read(page);
                if (!has_anchor(target, anchor)) {
                    g_error("%s (%s): link %s: no heading with that anchor", pages[i], lang, href);
                }
                g_free(target);
            }
            checked++;
            g_free(page);
            g_free(anchor);
            g_free(href);
            g_match_info_next(m, NULL);
        }
        g_match_info_free(m);
        g_free(text);
    }
    g_regex_unref(re);
    g_strfreev(pages);
    return checked;
}

/* Every translation covers every page of the guide, has no page the guide
 * lacks, and its links and anchors all lead somewhere */
static void test_translations(void) {
    gchar** langs = g_resources_enumerate_children("/org/lookin/guide-l10n/", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    g_assert_nonnull(langs); /* French at least */
    for (gint i = 0; langs[i] != NULL; i++) {
        gchar* lang = g_strndup(langs[i], strlen(langs[i]) - 1); /* "fr/" */
        gchar* dir = g_strdup_printf("/org/lookin/guide-l10n/%s/user-guide/", lang);
        gchar** files = g_resources_enumerate_children(dir, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        g_assert_nonnull(files);
        for (gint j = 0; files[j] != NULL; j++) {
            gchar* page = g_strconcat("user-guide/", files[j], NULL);
            gchar* english = user_guide_read(page);
            if (english == NULL) {
                g_error("%s: %s translates no page of the guide", lang, page);
            }
            g_free(english);
            g_free(page);
        }
        g_assert_cmpuint(check_links(lang), >, 50);
        g_strfreev(files);
        g_free(dir);
        g_free(lang);
    }
    g_strfreev(langs);
    g_assert_null(user_guide_read_in("user-guide/README.md", "xx"));
    g_assert_null(user_guide_read_in("user-guide/README.md", "../fr"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/user_guide/home", test_home);
    g_test_add_func("/user_guide/pages", test_pages);
    g_test_add_func("/user_guide/missing", test_missing);
    g_test_add_func("/user_guide/title", test_title);
    g_test_add_func("/user_guide/resolve_link", test_resolve_link);
    g_test_add_func("/user_guide/anchor", test_anchor);
    g_test_add_func("/user_guide/guide_links", test_guide_links);
    g_test_add_func("/user_guide/translations", test_translations);
    return g_test_run();
}
