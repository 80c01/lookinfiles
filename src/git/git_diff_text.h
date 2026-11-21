#ifndef GIT_DIFF_TEXT_H
#define GIT_DIFF_TEXT_H

#include <gtk/gtk.h>

/* The diff of a commit or of a changed file is laid out line by line on the
 * UI thread: a huge one (a large file added) shows its first lines only */
#define GIT_DIFF_MAX_LINES 5000

/* text cut after max_lines lines, with a note of how many are left out
 * (takes text, returns it or a new string) */
gchar* git_diff_text_cut(gchar* text, guint max_lines);

/* Replace the text of buffer with raw, a patch as git prints it (`git show`,
 * `git diff`), in the colours of the diff preview: added, removed, hunk and
 * header lines. The +/- lines before the first "diff --git" (a commit
 * message, the diffstat) stay plain. */
void git_diff_text_show(GtkTextBuffer* buffer, const gchar* raw);

#endif /* GIT_DIFF_TEXT_H */
