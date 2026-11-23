#ifndef LATEX_BUILD_H
#define LATEX_BUILD_H

/* The PDF of LaTeX documents in the preview. They are compiled by the
 * build tasks (F7, task_latex.h), and when saved while Preferences →
 * Editor → Compile LaTeX documents when saved is on (the default): a save
 * emits APP_EVENT_LATEX_SAVED. After a compilation, its PDF is shown at the
 * line of the editor's cursor (latex_sync.h). */
#include "core/app_types.h"

/* The file at path was saved from an editor tab: when it is a .tex file
 * and compiling on save is on, app->latex_saved_path = path and
 * APP_EVENT_LATEX_SAVED (task_run.c compiles it) */
void latex_build_saved(AppData* app, const gchar* path);

/* The tab of the .tex file at path was selected: show the PDF of its main
 * document in the preview when it was built (found in a worker) */
void latex_build_show_pdf(AppData* app, const gchar* path);

/* A compilation made pdf: show it in the preview (reloaded where it was
 * when shown already), at the place of the editor's cursor */
void latex_build_show_built(AppData* app, const gchar* pdf);

/* Stop the search of a PDF (at exit) */
void latex_build_free(AppData* app);

#endif /* LATEX_BUILD_H */
