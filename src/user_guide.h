#ifndef USER_GUIDE_H
#define USER_GUIDE_H

/* The user guide built into the binary: the Markdown pages of
 * docs/user-guide/ and the reference pages they link to (docs/keyboard.md,
 * docs/previews.md, docs/configuration.md), compiled as a GResource by the
 * Makefile. A page is named by its path under docs/, such as
 * "user-guide/editor.md" or "keyboard.md". Pure, unit-tested; the Help tab
 * (help_guide.h) shows the pages. */
#include <glib.h>

/* The first page of the guide (its table of contents) */
#define USER_GUIDE_HOME "user-guide/README.md"

/* Markdown text of page (valid UTF-8), or NULL when the guide has no such
 * page. Caller frees. */
gchar* user_guide_read(const gchar* page);

/* Translation of page into lang ("fr", "pt_BR"), from
 * docs/l10n/<lang>/<page>, or NULL when it has none. Caller frees. */
gchar* user_guide_read_in(const gchar* page, const gchar* lang);

/* page in the first of the user's languages (g_get_language_names()) that
 * has a translation of it, else in English (user_guide_read()). Links and
 * page names stay the English ones. Caller frees. */
gchar* user_guide_read_localized(const gchar* page);

/* Text of the first "# " heading of markdown, or NULL. Caller frees. */
gchar* user_guide_title(const gchar* markdown);

/* Every page of the guide, sorted. Caller frees with g_strfreev(). */
gchar** user_guide_pages(void);

typedef enum {
    USER_GUIDE_LINK_NONE = 0, /* outside the guide (a source file, a missing page) */
    USER_GUIDE_LINK_PAGE,     /* a page of the guide, maybe at an anchor */
    USER_GUIDE_LINK_WEB       /* a web or mail address (http, https, mailto) */
} UserGuideLink;

/* What href, a link written in page from, points to. PAGE: *out_page is the
 * page ("#id" alone: from itself) and *out_anchor the anchor or NULL. NONE:
 * *out_page is the path it names under docs/ when it has one ("README.md"
 * for "../../README.md" from a guide page), for a message. Out values may
 * be NULL; the caller frees them. */
UserGuideLink user_guide_resolve_link(const gchar* from, const gchar* href, gchar** out_page, gchar** out_anchor);

/* Anchor of a heading, as GitHub makes it ("The editor: tabs" ->
 * "the-editor-tabs"): lower case, spaces to '-', other punctuation dropped.
 * Caller frees. */
gchar* user_guide_anchor(const gchar* heading);

#endif /* USER_GUIDE_H */
