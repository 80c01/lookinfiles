#ifndef CODE_SYMBOLS_H
#define CODE_SYMBOLS_H

/* Symbols of the current file (docs/code-nav.md): an AppDialog
 * listing the functions, types, fields... of the editor tab, or the
 * headings of a Markdown file (code_outline.h), with
 * a filter entry. Enter, a double click or "Go To" jumps there (remembered
 * in the code navigation history). */
#include "core/app_types.h"
#include "code/code_nav.h"

void code_symbols_show(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* CODE_SYMBOLS_H */
