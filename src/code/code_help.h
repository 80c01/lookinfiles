#ifndef CODE_HELP_H
#define CODE_HELP_H

/* Help cards in editor tabs (docs/code-nav.md): what the
 * identifier at the cursor (or under the mouse, after a short rest) is —
 * signature, documentation, location — and the signature of the call being
 * typed, with the current parameter in bold. A language server answers when
 * one serves the file (code_intel), else the built-in index (code_nav_find():
 * the declaration line or the ctags signature, and the comment above it).
 *
 * Cards are GtkPopovers on the text view that never take the focus: typing
 * goes on in the editor. A help card closes on the next key, a click or when
 * the mouse leaves the word; the signature card follows the typing and closes
 * outside the call, on Escape or when the view loses the focus. In Emacs mode
 * the signature goes to the echo area (eldoc). */
#include "core/app_types.h"
#include "code/code_nav.h"

/* Install the mouse, key and buffer handlers on a new tab (once); message
 * shows one-line messages of the tab's mode */
void code_help_attach(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

/* Help card for the identifier at the cursor */
void code_help_show(AppData* app, EditorTab* tab);
/* Signature help for the call at the cursor */
void code_help_signature(AppData* app, EditorTab* tab);
/* A key reached the tab (editor_modes_handle_key(), before the mode): help
 * cards close, Escape closes the signature card too */
void code_help_on_key(EditorTab* tab, guint keyval);
/* Close any card of the tab */
void code_help_hide(EditorTab* tab);
/* Stop following tab's buffer (shared by a split view): the tab is closing */
void code_help_detach(EditorTab* tab);

#endif /* CODE_HELP_H */
