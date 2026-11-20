#ifndef CODE_RENAME_H
#define CODE_RENAME_H

/* Multi-file symbol rename via LSP textDocument/prepareRename and
 * textDocument/rename (docs/code-nav.md). */
#include "core/app_types.h"
#include "code/code_nav.h"

/* Start renaming the symbol under the cursor of tab. Requires a running
 * language server with renameProvider support. */
void code_rename_start(AppData* app, EditorTab* tab, CodeNavMessageFunc message);

#endif /* CODE_RENAME_H */
