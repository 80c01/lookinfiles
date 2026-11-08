#ifndef VIM_REGEX_H
#define VIM_REGEX_H

/* Vim regular expressions on top of GRegex (PCRE): pattern translation,
 * :substitute replacement strings and searching a GtkTextBuffer. */
#include <gtk/gtk.h>

/* Translate a Vim pattern to PCRE syntax. Handles 'magic' (default), \v,
 * \m, \M, \V, \c / \C, \< \>, \( \) \| \{n,m} \{-}, \+ \= \?, \zs \ze,
 * character classes (\s \d \w \a \l \u \x \h and uppercase complements).
 * caseless: in: the case rule ('ignorecase' + 'smartcase' applied by the
 * caller), out: the rule after \c / \C. */
gchar* vim_regex_translate(const gchar* pattern, gboolean* caseless);

/* Compile a Vim pattern; case follows ignorecase / smartcase (an uppercase
 * letter in the pattern makes it case sensitive when smartcase is on) */
GRegex* vim_regex_compile(const gchar* pattern, gboolean ignorecase,
                          gboolean smartcase, GError** error);

/* Expand a :s replacement for one match: & and \0 the match, \1..\9
 * groups, \r \n newline, \t tab, \u \l \U \L \e \E case, \& and \\ literal */
gchar* vim_regex_expand(const gchar* replacement, const GMatchInfo* match);

/* Search buf for re, from the position after (forward) or before
 * (backward) `from`. wrap: continue at the other end. Sets *wrapped when
 * the match was found after wrapping. Match bounds as char iters. */
gboolean vim_regex_search(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* from,
                          gboolean forward, gboolean wrap, gboolean* wrapped,
                          GtkTextIter* match_start, GtkTextIter* match_end);

/* Same, with the match allowed to start at `from` itself (forward only) */
gboolean vim_regex_search_at(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* from,
                             GtkTextIter* match_start, GtkTextIter* match_end);

/* Number of matches in the buffer and the 1-based index of the match
 * starting at `at` (0 when none starts there) */
gint vim_regex_count(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* at, gint* index);

/* Apply tag to every match (removed everywhere first). Stops after
 * max_matches (0: no limit). */
void vim_regex_tag_matches(GtkTextBuffer* buf, GRegex* re, GtkTextTag* tag, gint max_matches);

#endif /* VIM_REGEX_H */
