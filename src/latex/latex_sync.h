#ifndef LATEX_SYNC_H
#define LATEX_SYNC_H

/* The PDF preview follows the cursor of a LaTeX source (SyncTeX forward
 * search, as Overleaf): when the preview shows a PDF that has SyncTeX data
 * and the cursor of a focused .tex editor tab moves, "synctex view" is run
 * (asynchronously, after a short pause) and the preview scrolled to the
 * place of the line, at the height of the cursor in the editor. And back
 * (inverse search): Ctrl+Click on a page of such a PDF runs "synctex edit"
 * and opens the source at the line of the place clicked. */
#include "core/app_types.h"

/* Follow the editor tabs focused in the window */
void latex_sync_init(AppData* app);
/* Follow the Ctrl+Clicks on the preview (app->text_view, once set) */
void latex_sync_attach_preview(AppData* app);

/* A compilation showed its PDF: bring the cursor's line into it once its
 * pages are in the preview */
void latex_sync_after_build(AppData* app);

void latex_sync_free(AppData* app);

#endif /* LATEX_SYNC_H */
