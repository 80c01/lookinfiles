#ifndef LATEX_ROOT_H
#define LATEX_ROOT_H

/* The main document of a LaTeX file and how to compile it, from the text of
 * the files: the "% !TEX root = main.tex" and "% !TEX program = xelatex"
 * magic comments (TeXShop, TeXstudio, VS Code), \documentclass, the
 * packages that need XeLaTeX or LuaLaTeX, and the files that \input or
 * \include a chapter.
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    LATEX_ENGINE_PDFLATEX,
    LATEX_ENGINE_XELATEX,
    LATEX_ENGINE_LUALATEX,
} LatexEngine;

/* The file named by "% !TEX root = file" in the first lines of text
 * (relative to the file's directory, as written), or NULL */
gchar* latex_root_magic(const gchar* text);

/* Whether text is a main document: \documentclass outside comments */
gboolean latex_root_is_main(const gchar* text);

/* The engine of a main document: "% !TEX program = ..." or else what its
 * packages need (fontspec, unicode-math, polyglossia: XeLaTeX; luacode,
 * luatexbase: LuaLaTeX), else pdfLaTeX */
LatexEngine latex_root_engine(const gchar* text);

/* Command name of an engine ("pdflatex") */
const gchar* latex_root_engine_name(LatexEngine engine);

/* Among n candidate main documents (texts[i] of names[i], names relative
 * to the directory of the edited file, e.g. "main.tex" or "../thesis.tex"),
 * the one that includes the edited file (its path without ".tex" relative
 * to each candidate's directory is given by stems[i]), else the only one,
 * else "main.tex"; -1 when none fits */
gint latex_root_choose(const gchar* const* names, const gchar* const* texts, const gchar* const* stems, guint n);

#endif /* LATEX_ROOT_H */
