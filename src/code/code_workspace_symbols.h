#ifndef CODE_WORKSPACE_SYMBOLS_H
#define CODE_WORKSPACE_SYMBOLS_H

/* Project-wide symbol search dialog (docs/code-nav.md): LSP workspace/symbol
 * when supported by a running server, else ctags or the built-in scanners in a
 * worker thread. */
#include "core/app_types.h"
#include "code/code_nav.h"

/* Open the Workspace Symbols picker for the project (git root, else the
 * folder) of tab's file, or of app->base_dir when tab is NULL or has no file.
 * message may be NULL. */
void code_workspace_symbols_show(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* CODE_WORKSPACE_SYMBOLS_H */
