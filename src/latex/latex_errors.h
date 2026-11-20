#ifndef LATEX_ERRORS_H
#define LATEX_ERRORS_H

/* The LaTeX page of the preview notebook (after the Git and result tabs):
 * the errors, warnings and bad boxes of the last compilation, one row each
 * (file, line, message, the source of an error). Enter or a double click
 * opens the location in an editor tab; the tab's close button removes it. */
#include "core/app_types.h"

/* The messages (LatexMessage*, files relative to the directory of root) of
 * the compilation of root by program. The page is made when there are
 * errors or warnings; otherwise an open page is only updated. */
void latex_errors_show(AppData* app, const gchar* root, const GPtrArray* msgs, const gchar* program);

/* Put the page's header to "Compiling root…" when the page is open */
void latex_errors_compiling(AppData* app, const gchar* root);

/* Each workspace has its own LaTeX page: set aside in ws when ws is hidden,
 * put back when ws is shown, dropped with ws. A compilation ending in
 * another workspace shows there, as its PDF does. The page moves like an
 * editor tab (drag, tab menu). */
void latex_errors_workspace_hidden(AppData* app, VirtualLayout* ws);
void latex_errors_workspace_shown(AppData* app, VirtualLayout* ws);
void latex_errors_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* LATEX_ERRORS_H */
