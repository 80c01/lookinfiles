#ifndef CODE_REFS_H
#define CODE_REFS_H

/* References of a symbol (TASK_CODE_NAV.md, phase 3): the language server's
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

#endif /* CODE_REFS_H */
