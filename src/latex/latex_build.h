#ifndef LATEX_BUILD_H
#define LATEX_BUILD_H

/* LaTeX documents compiled when they are saved, as in Overleaf: the main
 * document of the saved .tex file (latex_root.h) is compiled in a worker
 * by latexmk, else Tectonic, else the engine it needs (pdflatex, xelatex,
 * lualatex), with SyncTeX data. Its PDF is then shown in the preview at
 * the line of the editor's cursor (latex_sync.h), and the messages of its
 * log go to the LaTeX page (latex_errors.h). A save during a compilation
 * compiles again once it ends. */
#include "core/app_types.h"

/* The file at path was saved from an editor tab: compile it when it is a
 * .tex file */
void latex_build_saved(AppData* app, const gchar* path);

/* Stop a running compilation (at exit) */
void latex_build_free(AppData* app);

#endif /* LATEX_BUILD_H */
