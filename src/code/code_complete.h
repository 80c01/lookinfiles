#ifndef CODE_COMPLETE_H
#define CODE_COMPLETE_H

/* In-editor autocompletion popover (docs/code-nav.md): LSP
 * textDocument/completion when a language server serves the tab, with a
 * lightweight fallback over buffer identifiers otherwise. Non-focus-stealing
 * GtkPopover anchored at the cursor. */
#include "core/app_types.h"

/* Predicate: TRUE when auto-completion on typing is allowed in tab (e.g.
 * Notepad, Emacs, or Vim Insert mode; FALSE in Vim Normal/Visual or Org). */
typedef gboolean (*CodeCompleteCanAutoFunc)(EditorTab* tab);

void code_complete_attach(AppData* app, EditorTab* tab, CodeCompleteCanAutoFunc can_auto);
void code_complete_detach(EditorTab* tab);

/* Manually trigger completion at the cursor (Ctrl+Space, C-M-i / M-/) */
void code_complete_trigger(AppData* app, EditorTab* tab);

/* Dismiss the completion popover if visible */
void code_complete_hide(EditorTab* tab);

/* Handle navigation / accept / dismiss keys while the popover is visible.
 * Returns TRUE if the key was consumed. */
gboolean code_complete_handle_key(EditorTab* tab, guint keyval, GdkModifierType state);

#endif /* CODE_COMPLETE_H */
