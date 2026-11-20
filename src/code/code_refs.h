#ifndef CODE_REFS_H
#define CODE_REFS_H

/* References of a symbol (docs/code-nav.md): the language server's
 * references when one serves the file, else every whole-word occurrence in
 * the project (code_index_references(), in a worker). They are listed in a
 * "References" tab of the preview notebook, after the Git tab: one row per
 * file then one per occurrence (line number, line, the name in bold).
 * Enter or a double click opens the location in an editor tab; the tab's
 * close button removes it. */
#include "core/app_types.h"
#include "code/code_nav.h"

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
 * stopped early or what it skipped; area (may be NULL, kept: a static
 * table) shows and opens the files its own way (the notes) */
void code_refs_show_search(AppData* app, const gchar* query, GPtrArray* refs, const gchar* root,
                           const gchar* how, const gchar* note, const SearchArea* area);
/* The same results put in a Search page that is not shown (a hidden
 * workspace's, see code_refs_detach_search()) */
void code_refs_set_search(GtkWidget* page, const gchar* query, GPtrArray* refs, const gchar* root,
                          const gchar* how, const gchar* note, const SearchArea* area);

/* Each workspace has its own Search page: take it out of the notebook with
 * its tab (an owned ref, or NULL when it is not open) and put it back */
GtkWidget* code_refs_detach_search(AppData* app);
void code_refs_attach_search(AppData* app, GtkWidget* page); /* takes the ref */

/* Each workspace has its own References page too: set aside in ws when ws is
 * hidden (a lookup still running is dropped), put back when ws is shown,
 * dropped with ws. Both pages move like editor tabs (drag, tab menu). */
void code_refs_workspace_hidden(AppData* app, VirtualLayout* ws);
void code_refs_workspace_shown(AppData* app, VirtualLayout* ws);
void code_refs_workspace_removed(AppData* app, VirtualLayout* ws);

/* Close the References or Search tab if it has the focus or is the current
 * page of app->notebook (Ctrl+W). Returns TRUE when closed. */
gboolean code_refs_close_shown(AppData* app);

#endif /* CODE_REFS_H */
