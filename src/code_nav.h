#ifndef CODE_NAV_H
#define CODE_NAV_H

/* Code navigation in editor tabs (TASK_CODE_NAV.md): go to the definition
 * or declaration of the identifier under the cursor, or of a name, and walk
 * back / forward through the jumps across files. The same functions serve
 * the three editor modes; each maps its own keys to them.
 *
 * Lookups use the built-in index (code_index) in a worker thread: a new
 * request replaces the previous one, and an answer for a tab closed or
 * changed meanwhile is dropped. Messages go through the caller's message
 * function (editor_mode_message()), to the tab of the result. */
#include "app_types.h"
#include "code_index.h"

typedef void (*CodeNavMessageFunc)(EditorTab* tab, const gchar* message);
/* Answer of code_nav_find(): the best symbol named so (NULL when none), its
 * documentation comment (or NULL) and the project root */
typedef void (*CodeNavFoundFunc)(AppData* app, EditorTab* tab, const CodeMatch* match, const gchar* doc,
                                 const gchar* root, gpointer data);

/* TRUE when the tab shows source code of a language the index knows */
gboolean code_nav_is_code(EditorTab* tab);

/* Look up the definition (or declaration) of the identifier at the cursor
 * and jump to it; repeated on a definition, it goes to the next candidate.
 * When nothing is found, the first use in the cursor's scope. FALSE (and a
 * message) when the tab is not code or the cursor is not on an identifier. */
gboolean code_nav_goto_definition(AppData* app, EditorTab* tab, gboolean declaration,
                                  CodeNavMessageFunc message);

/* Same for a name typed by the user (Vim :tag) */
gboolean code_nav_goto_symbol(AppData* app, EditorTab* tab, const gchar* name, gboolean declaration,
                              CodeNavMessageFunc message);

/* Look name up with the built-in index without jumping (help cards,
 * signature help); a newer code_nav_find() replaces this one (func is then
 * not called). FALSE when the tab is not code. */
gboolean code_nav_find(AppData* app, EditorTab* tab, const gchar* name, CodeNavFoundFunc func, gpointer data);

/* Backend used for the tab without a language server: "ctags" or
 * "built-in index"; NULL when the tab is not code */
const gchar* code_nav_backend_label(EditorTab* tab);

/* Back to where the last jump started / forward again. tab: where the
 * cursor is now (NULL: none). FALSE (and a message) at the end of the
 * history. */
gboolean code_nav_back(AppData* app, EditorTab* tab, CodeNavMessageFunc message);
gboolean code_nav_forward(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* CODE_NAV_H */
