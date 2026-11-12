#ifndef TEX_MATH_H
#define TEX_MATH_H

/* TeX math (the LaTeX subset of MathJax and KaTeX that notebooks and
 * Markdown documents use) parsed into a tree of boxes to lay out:
 * math_paintable.h draws it. Symbols become their Unicode characters
 * (\alpha: α, \mathbb{R}: ℝ), letters are italic, function names upright;
 * fractions, roots, scripts, big operators with limits, \left \right
 * delimiters, accents, matrices and the aligned / cases environments are
 * structures. An unknown command is kept as its name (upright).
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    TEX_ROW,     /* items: the nodes in a row */
    TEX_ATOM,    /* text, in font, of class cls */
    TEX_FRAC,    /* body over under (style: TexFracStyle) */
    TEX_SQRT,    /* body under the root sign, under: the index or NULL */
    TEX_SCRIPTS, /* body with under (subscript) and over (superscript), either NULL */
    TEX_FENCED,  /* body between the delimiters left and right ("": none) */
    TEX_ACCENT,  /* body with an accent (style: TexAccent; text: its glyph) */
    TEX_TABLE,   /* items: rows (GPtrArray of TexNode* cells); text: column alignments */
    TEX_SPACE,   /* em wide (may be negative) */
    TEX_BOX,     /* body framed (style TEX_BOX_FRAME) or invisible (TEX_BOX_PHANTOM) */
} TexKind;

/* Spacing classes of TeX */
typedef enum {
    TEX_ORD,
    TEX_OP,
    TEX_BIN,
    TEX_REL,
    TEX_OPEN,
    TEX_CLOSE,
    TEX_PUNCT,
    TEX_INNER,
} TexClass;

typedef enum {
    TEX_FONT_ITALIC = 1 << 0,
    TEX_FONT_BOLD = 1 << 1,
    TEX_FONT_MONO = 1 << 2,
} TexFont;

/* Where the scripts of an operator go */
typedef enum {
    TEX_LIMITS_SIDE,    /* beside (integrals, sin) */
    TEX_LIMITS_DISPLAY, /* above and below in display math (sum, lim) */
    TEX_LIMITS_ALWAYS,  /* always above and below (\overset, \underbrace, \limits) */
} TexLimits;

typedef enum {
    TEX_FRAC_AUTO,    /* \frac */
    TEX_FRAC_DISPLAY, /* \dfrac, \cfrac */
    TEX_FRAC_TEXT,    /* \tfrac */
    TEX_FRAC_NO_BAR,  /* \binom (inside parentheses), \substack */
} TexFracStyle;

typedef enum {
    TEX_ACCENT_GLYPH,      /* text over the body (\hat, \dot...) */
    TEX_ACCENT_WIDE,       /* text stretched over the body (\widehat) */
    TEX_ACCENT_OVERLINE,   /* \bar, \overline */
    TEX_ACCENT_UNDERLINE,  /* \underline */
    TEX_ACCENT_VEC,        /* \vec: a small arrow */
    TEX_ACCENT_RIGHTARROW, /* \overrightarrow */
    TEX_ACCENT_LEFTARROW,  /* \overleftarrow */
    TEX_ACCENT_OVERBRACE,
    TEX_ACCENT_UNDERBRACE,
} TexAccent;

typedef enum {
    TEX_BOX_FRAME,   /* \boxed */
    TEX_BOX_PHANTOM, /* \phantom: room, nothing drawn */
} TexBoxStyle;

typedef enum {
    TEX_TABLE_MATRIX,  /* cells centred, 1 em between columns */
    TEX_TABLE_ALIGNED, /* columns in pairs (right, left), 2 em between pairs; display style */
    TEX_TABLE_CASES,   /* left aligned, 1 em between columns */
} TexTableStyle;

typedef struct TexNode TexNode;
struct TexNode {
    TexKind kind;
    TexClass cls;     /* ATOM: its spacing class (tex_node_class() for the others) */
    guint font;       /* ATOM: TexFont flags */
    gchar* text;      /* ATOM: its text; ACCENT: the glyph; TABLE: alignments ("c", "rl", "lcr") */
    gchar* left;      /* FENCED */
    gchar* right;     /* FENCED */
    TexNode* body;    /* FRAC numerator; SQRT, SCRIPTS base, FENCED, ACCENT, BOX content */
    TexNode* under;   /* FRAC denominator, subscript, SQRT index */
    TexNode* over;    /* superscript */
    GPtrArray* items; /* ROW: TexNode*; TABLE: rows (GPtrArray of TexNode*) */
    gint big;         /* ATOM: 1 for a big operator (sum, integral); 1-4 for a \big... delimiter */
    gint delim;       /* ATOM: TRUE for a \big... delimiter (size big) */
    TexLimits limits; /* ATOM of class OP, ACCENT braces */
    gint style;       /* FRAC: TexFracStyle; ACCENT: TexAccent; BOX: TexBoxStyle; TABLE: TexTableStyle */
    gdouble em;       /* SPACE: width in em */
};

/* The formula in tex (never NULL: what cannot be parsed is kept as text) */
TexNode* tex_math_parse(const gchar* tex);
void tex_node_free(TexNode* node);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(TexNode, tex_node_free)

/* The spacing class of a node: an atom's, a base's for scripts, INNER for
 * fractions and delimited groups, else ORD */
TexClass tex_node_class(const TexNode* node);

/* The formula as linear text ("x^2", "(a+b)/c", "√x"), for copying and tests */
gchar* tex_math_to_text(const TexNode* node);

#endif /* TEX_MATH_H */
