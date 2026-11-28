#ifndef CODE_SCAN_TEX_H
#define CODE_SCAN_TEX_H

/* The built-in scanner of LaTeX and BibTeX (docs/code-nav.md), outside
 * comments (%) and verbatim environments (verbatim, lstlisting, minted,
 * comment...):
 * - symbols: \label{key} (labels), \newcommand{\name}, \def\name,
 *   \DeclareMathOperator... (macros, named without the backslash),
 *   \newenvironment{name}, \newtheorem{name} (types), \bibitem{key} and
 *   the "@article{key," entries of a .bib file (bibliography entries);
 * - the outline: those symbols and the headings, \part (level 1),
 *   \chapter (2), \section (3) ... \subparagraph (7), and beamer frames
 *   with a title (8: \begin{frame}{Title} or \frametitle{Title}).
 *
 * Pure GLib, unit-tested; safe to call from worker threads. */
#include "code/code_outline_tree.h"
#include "code/code_symbol.h"

/* Symbols of text (CodeSymbol*, freed with the array), in file order: all
 * of them, or only those named only_name when it is not NULL */
GPtrArray* code_scan_tex(const gchar* text, gsize len, const gchar* only_name);

/* The outline of text: headings and symbols (CodeOutlineEntry*), in file
 * order */
GPtrArray* code_outline_tex(const gchar* text, gsize len);

#endif /* CODE_SCAN_TEX_H */
