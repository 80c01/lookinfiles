#ifndef TEX_SYMBOLS_H
#define TEX_SYMBOLS_H

/* The commands of TeX math that stand for one symbol, for tex_math: Greek
 * letters, operators, relations, arrows, delimiters (\alpha: α), big
 * operators, function names, accents and the negated relations of \not.
 *
 * Pure GLib, tested through tex_math. */
#include "markup/tex_math.h"

typedef struct {
    const gchar* name; /* without its backslash */
    const gchar* text;
    TexClass cls;
    gboolean italic; /* lowercase Greek: italic, as TeX sets it */
} TexSymbol;

/* The symbol \name, or NULL */
const TexSymbol* tex_symbol_find(const gchar* name);
/* The symbol written text ("≤" typed as such), or NULL */
const TexSymbol* tex_symbol_find_text(const gchar* text);
/* \sum, \int...: its glyph and where its scripts go */
gboolean tex_symbol_big_op(const gchar* name, const gchar** text, TexLimits* limits);
/* \sin, \lim...: its upright text and where its scripts go */
gboolean tex_symbol_function(const gchar* name, const gchar** text, TexLimits* limits);
/* \hat, \vec...: its kind and glyph (NULL for drawn accents) */
gboolean tex_symbol_accent(const gchar* name, TexAccent* kind, const gchar** glyph);
/* "≠" for "=", or NULL */
const gchar* tex_symbol_negated(const gchar* rel);

#endif /* TEX_SYMBOLS_H */
