#ifndef CODE_REFS_H
#define CODE_REFS_H

/* References of a symbol (docs/code-nav.md): the language server's
 * references when one serves the file, else every whole-word occurrence in
 * the project (code_index_references(), in a worker). They are listed in a
 * "References" tab of the preview notebook, after the Git tab: one row per
 * file then one per occurrence (line number, line, the name in bold).
 * Enter or a double click opens the location in an editor tab; the tab's
 * close button removes it. */
#include "app_types.h"
#include "code_nav.h"

/* References of the identifier at the cursor of tab. FALSE (and a message)
 * when the tab is not code or the cursor is not on an identifier. */
gboolean code_refs_find(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

/* The References page, or NULL when it is not open */
GtkWidget* code_refs_page(AppData* app);

/* The Search page (text search results, Ctrl+Shift+F) works the same way,
 * in its own tab next to References. */
GtkWidget* code_refs_search_page(AppData* app);
/* Show the Search page, emptied, while query is searched for under root */
void code_refs_show_searching(AppData* app, const gchar* query, const gchar* root);
/* Show the occurrences of query (CodeReference*, grouped by file); how
 * describes the search options ("any case"...), note (may be NULL) why it
 * stopped early or what it skipped */
void code_refs_show_search(AppData* app, const gchar* query, GPtrArray* refs, const gchar* root,
                           const gchar* how, const gchar* note);
/* The same results put in a Search page that is not shown (a hidden
 * workspace's, see code_refs_detach_search()) */
void code_refs_set_search(GtkWidget* page, const gchar* query, GPtrArray* refs, const gchar* root,
                          const gchar* how, const gchar* note);

/* Each workspace has its own Search page: take it out of the notebook with
 * its tab (an owned ref, or NULL when it is not open) and put it back */
GtkWidget* code_refs_detach_search(AppData* app);
void code_refs_attach_search(AppData* app, GtkWidget* page); /* takes the ref */

#endif /* CODE_REFS_H */
