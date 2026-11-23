#ifndef TASK_LATEX_H
#define TASK_LATEX_H

/* The LaTeX tasks of a .tex file (F7 from its editor tab, and its saves
 * when Preferences → Editor → Compile LaTeX documents when saved is on):
 * its main document (latex_root.h: "% !TEX root", \documentclass, the
 * document of its folder or of the one above that includes it), compiled
 * by latexmk, else Tectonic, else the engine it needs, with SyncTeX data
 * and file:line errors; then its auxiliary files removed (latexmk -C).
 * On a remote machine (vfs.h) the files are read over SFTP and latexmk is
 * run there.
 *
 * Reads files: worker threads. GLib and vfs, unit-tested. */
#include <glib.h>

#include "tasks/task_def.h"

/* The main document of the .tex file at path, NULL when none is found */
gchar* task_latex_root(const gchar* path);

/* The tasks of the main document root (TaskDef*: the compilation first,
 * its tex_root set; empty when no LaTeX program is installed here) */
GPtrArray* task_latex_tasks(const gchar* root);

/* The file of root with extension ext (".pdf", ".log") */
gchar* task_latex_output(const gchar* root, const gchar* ext);

#endif /* TASK_LATEX_H */
