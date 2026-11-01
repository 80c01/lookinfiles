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

/* Text of the first "# " heading of markdown, or NULL. Caller frees. */
gchar* user_guide_title(const gchar* markdown);

/* Every page of the guide, sorted. Caller frees with g_strfreev(). */
gchar** user_guide_pages(void);

#endif /* USER_GUIDE_H */
