#ifndef CODE_SYMBOLS_H
#define CODE_SYMBOLS_H

/* Symbols of the current file (TASK_CODE_NAV.md, phase 3): an AppDialog
 * listing the functions, types, fields... of the editor tab (the language
 * server's document symbols, else the built-in scanner on the buffer), with
 * a filter entry. Enter, a double click or "Go To" jumps there (remembered
 * in the code navigation history). */
#include "app_types.h"
#include "code_nav.h"

void code_symbols_show(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* CODE_SYMBOLS_H */
