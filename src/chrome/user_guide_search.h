#ifndef USER_GUIDE_SEARCH_H
#define USER_GUIDE_SEARCH_H

/* Search the user guide (user_guide.h): every page, in the language it is
 * shown in, by section (a heading and the text under it). A query is words
 * that must all be in a section, in any order, ignoring case and accents
 * ("editeur" finds "éditeur"). Pure GLib, unit-tested; the Help tab
 * (help_guide.h) shows the results as a page of the guide and highlights
 * the words in the page a result opens. */
#include <glib.h>

/* Name of the results page of query, a page name the Help tab knows
 * ("search:<query>"); its links are page names from docs/ */
#define USER_GUIDE_SEARCH_PREFIX "search:"

/* At most this many results are listed */
#define USER_GUIDE_SEARCH_MAX_HITS 100

typedef struct {
    gchar* page;       /* page name ("user-guide/editor.md") */
    gchar* anchor;     /* anchor of the section's heading, NULL: the page's top */
    gchar* page_title; /* title of the page */
    gchar* heading;    /* the section's heading as plain text, NULL: the top */
    gchar* excerpt;    /* Markdown: the best line, matches in bold, or NULL */
    gint score;
} UserGuideHit;

void user_guide_hit_free(gpointer hit);

/* Character offsets [start, end) of every match of a word of query in
 * text, sorted, overlaps merged (pairs of gint in the array). Offsets count
 * characters of text, as GtkTextIter offsets do. */
GArray* user_guide_match_ranges(const gchar* text, const gchar* query);

/* TRUE when text holds every word of query, case and accents ignored (a
 * query without words matches everything) */
gboolean user_guide_matches_all(const gchar* text, const gchar* query);

/* A line of Markdown as the reader sees it: no list marker, heading marks,
 * emphasis or code marks, links reduced to their text, table cells joined
 * with " · ". Caller frees. */
gchar* user_guide_plain(const gchar* line);

/* text with the characters Markdown would read as syntax escaped. Caller
 * frees. */
gchar* user_guide_md_escape(const gchar* text);

/* Append to hits (UserGuideHit*) the sections of page (its Markdown text)
 * that hold every word of query */
void user_guide_search_text(const gchar* page, const gchar* markdown, const gchar* query, GPtrArray* hits);

/* Every page of the guide (user_guide_read_localized()) searched for query:
 * hits best first, at most USER_GUIDE_SEARCH_MAX_HITS. Free with
 * g_ptr_array_unref(). */
GPtrArray* user_guide_search(const gchar* query);

/* The results page of query: Markdown, a title and one link per hit, to
 * "page#anchor". Caller frees. */
gchar* user_guide_search_page(const gchar* query);

#endif /* USER_GUIDE_SEARCH_H */
