#ifndef LATEX_EDIT_H
#define LATEX_EDIT_H

/* The environments of a LaTeX text for the editor's LaTeX keys
 * (editor_latex.h): its \begin{name} and \end{name} outside comments (%)
 * and inside verbatim environments only their own \end, each \begin paired
 * with the \end of the same name that closes it (an \end without its
 * \begin, or a \begin left open, has no partner).
 *
 * Offsets are byte offsets in the text. Pure GLib, unit-tested. */
#include <glib.h>

typedef struct {
    gboolean begin;   /* \begin, else \end */
    gsize start;      /* the backslash */
    gsize end;        /* after the closing brace */
    gsize name_start; /* the name between the braces */
    gsize name_end;
} LatexEnvToken;

/* The \begin{..} or \end{..} at offset: with name_only, offset between
 * the braces (name_start to name_end, both included), else anywhere from
 * its backslash to its closing brace (included). FALSE when none. */
gboolean latex_edit_token_at(const gchar* text, gsize len, gsize offset, gboolean name_only, LatexEnvToken* token);

/* The \end of a \begin or the \begin of an \end; FALSE when it has none */
gboolean latex_edit_partner(const gchar* text, gsize len, const LatexEnvToken* token, LatexEnvToken* partner);

/* The innermost environment around offset (after its \begin, before the end
 * of its \end): its \begin, and its \end when it is closed (*closed).
 * FALSE when offset is in no environment. */
gboolean latex_edit_enclosing(const gchar* text, gsize len, gsize offset, LatexEnvToken* begin, LatexEnvToken* end,
                              gboolean* closed);

/* The name of the environment to close when a new line is made at offset:
 * the line before offset ends with \begin{name} (then optional arguments:
 * [..] {..}) and that \begin is still open. NULL otherwise. g_free(). */
gchar* latex_edit_env_to_close(const gchar* text, gsize len, gsize offset);

/* TRUE when name can be an environment name: letters, digits, '*', '@',
 * '-', ':' and '_', 1 to 64 bytes */
gboolean latex_edit_valid_name(const gchar* name, gssize len);

#endif /* LATEX_EDIT_H */
