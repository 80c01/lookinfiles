/* The commands of TeX math that stand for symbols (see tex_symbols.h). */
#include "markup/tex_symbols.h"

#include <string.h>

/* Commands that stand for one character */
static const TexSymbol SYMBOLS[] = {
    /* Greek */
    {"alpha", "α", TEX_ORD, TRUE}, {"beta", "β", TEX_ORD, TRUE}, {"gamma", "γ", TEX_ORD, TRUE},
    {"delta", "δ", TEX_ORD, TRUE}, {"epsilon", "ϵ", TEX_ORD, TRUE}, {"varepsilon", "ε", TEX_ORD, TRUE},
    {"zeta", "ζ", TEX_ORD, TRUE}, {"eta", "η", TEX_ORD, TRUE}, {"theta", "θ", TEX_ORD, TRUE},
    {"vartheta", "ϑ", TEX_ORD, TRUE}, {"iota", "ι", TEX_ORD, TRUE}, {"kappa", "κ", TEX_ORD, TRUE},
    {"varkappa", "ϰ", TEX_ORD, TRUE}, {"lambda", "λ", TEX_ORD, TRUE}, {"mu", "μ", TEX_ORD, TRUE},
    {"nu", "ν", TEX_ORD, TRUE}, {"xi", "ξ", TEX_ORD, TRUE}, {"omicron", "ο", TEX_ORD, TRUE},
    {"pi", "π", TEX_ORD, TRUE}, {"varpi", "ϖ", TEX_ORD, TRUE}, {"rho", "ρ", TEX_ORD, TRUE},
    {"varrho", "ϱ", TEX_ORD, TRUE}, {"sigma", "σ", TEX_ORD, TRUE}, {"varsigma", "ς", TEX_ORD, TRUE},
    {"tau", "τ", TEX_ORD, TRUE}, {"upsilon", "υ", TEX_ORD, TRUE}, {"phi", "ϕ", TEX_ORD, TRUE},
    {"varphi", "φ", TEX_ORD, TRUE}, {"chi", "χ", TEX_ORD, TRUE}, {"psi", "ψ", TEX_ORD, TRUE},
    {"omega", "ω", TEX_ORD, TRUE}, {"digamma", "ϝ", TEX_ORD, TRUE},
    {"Gamma", "Γ", TEX_ORD, FALSE}, {"Delta", "Δ", TEX_ORD, FALSE}, {"Theta", "Θ", TEX_ORD, FALSE},
    {"Lambda", "Λ", TEX_ORD, FALSE}, {"Xi", "Ξ", TEX_ORD, FALSE}, {"Pi", "Π", TEX_ORD, FALSE},
    {"Sigma", "Σ", TEX_ORD, FALSE}, {"Upsilon", "Υ", TEX_ORD, FALSE}, {"Phi", "Φ", TEX_ORD, FALSE},
    {"Psi", "Ψ", TEX_ORD, FALSE}, {"Omega", "Ω", TEX_ORD, FALSE},
    {"varGamma", "Γ", TEX_ORD, TRUE}, {"varDelta", "Δ", TEX_ORD, TRUE}, {"varTheta", "Θ", TEX_ORD, TRUE},
    {"varLambda", "Λ", TEX_ORD, TRUE}, {"varPi", "Π", TEX_ORD, TRUE}, {"varSigma", "Σ", TEX_ORD, TRUE},
    {"varPhi", "Φ", TEX_ORD, TRUE}, {"varPsi", "Ψ", TEX_ORD, TRUE}, {"varOmega", "Ω", TEX_ORD, TRUE},
    /* Letter-like and other ordinary symbols */
    {"infty", "∞", TEX_ORD, FALSE}, {"partial", "∂", TEX_ORD, FALSE}, {"nabla", "∇", TEX_ORD, FALSE},
    {"forall", "∀", TEX_ORD, FALSE}, {"exists", "∃", TEX_ORD, FALSE}, {"nexists", "∄", TEX_ORD, FALSE},
    {"emptyset", "∅", TEX_ORD, FALSE}, {"varnothing", "∅", TEX_ORD, FALSE}, {"neg", "¬", TEX_ORD, FALSE},
    {"lnot", "¬", TEX_ORD, FALSE}, {"hbar", "ℏ", TEX_ORD, FALSE}, {"hslash", "ℏ", TEX_ORD, FALSE},
    {"ell", "ℓ", TEX_ORD, FALSE}, {"Re", "ℜ", TEX_ORD, FALSE}, {"Im", "ℑ", TEX_ORD, FALSE},
    {"aleph", "ℵ", TEX_ORD, FALSE}, {"beth", "ℶ", TEX_ORD, FALSE}, {"wp", "℘", TEX_ORD, FALSE},
    {"angle", "∠", TEX_ORD, FALSE}, {"measuredangle", "∡", TEX_ORD, FALSE}, {"triangle", "△", TEX_ORD, FALSE},
    {"triangledown", "▽", TEX_ORD, FALSE}, {"square", "□", TEX_ORD, FALSE}, {"Box", "□", TEX_ORD, FALSE},
    {"blacksquare", "■", TEX_ORD, FALSE}, {"Diamond", "◇", TEX_ORD, FALSE}, {"lozenge", "◊", TEX_ORD, FALSE},
    {"prime", "′", TEX_ORD, FALSE}, {"backprime", "‵", TEX_ORD, FALSE}, {"degree", "°", TEX_ORD, FALSE},
    {"top", "⊤", TEX_ORD, FALSE}, {"bot", "⊥", TEX_ORD, FALSE}, {"flat", "♭", TEX_ORD, FALSE},
    {"natural", "♮", TEX_ORD, FALSE}, {"sharp", "♯", TEX_ORD, FALSE}, {"clubsuit", "♣", TEX_ORD, FALSE},
    {"diamondsuit", "♢", TEX_ORD, FALSE}, {"heartsuit", "♡", TEX_ORD, FALSE}, {"spadesuit", "♠", TEX_ORD, FALSE},
    {"checkmark", "✓", TEX_ORD, FALSE}, {"S", "§", TEX_ORD, FALSE}, {"P", "¶", TEX_ORD, FALSE},
    {"copyright", "©", TEX_ORD, FALSE}, {"imath", "ı", TEX_ORD, TRUE}, {"jmath", "ȷ", TEX_ORD, TRUE},
    {"eth", "ð", TEX_ORD, FALSE}, {"mho", "℧", TEX_ORD, FALSE}, {"complement", "∁", TEX_ORD, FALSE},
    {"surd", "√", TEX_ORD, FALSE}, {"backslash", "\\", TEX_ORD, FALSE}, {"vert", "|", TEX_ORD, FALSE},
    {"Vert", "‖", TEX_ORD, FALSE}, {"ldots", "…", TEX_INNER, FALSE}, {"dots", "…", TEX_INNER, FALSE},
    {"dotsc", "…", TEX_INNER, FALSE}, {"dotso", "…", TEX_INNER, FALSE}, {"cdots", "⋯", TEX_INNER, FALSE},
    {"dotsb", "⋯", TEX_INNER, FALSE}, {"dotsm", "⋯", TEX_INNER, FALSE}, {"dotsi", "⋯", TEX_INNER, FALSE},
    {"vdots", "⋮", TEX_ORD, FALSE}, {"ddots", "⋱", TEX_INNER, FALSE}, {"colon", ":", TEX_PUNCT, FALSE},
    {"cdotp", "⋅", TEX_PUNCT, FALSE}, {"ldotp", ".", TEX_PUNCT, FALSE},
    /* Binary operators */
    {"pm", "±", TEX_BIN, FALSE}, {"mp", "∓", TEX_BIN, FALSE}, {"times", "×", TEX_BIN, FALSE},
    {"div", "÷", TEX_BIN, FALSE}, {"cdot", "⋅", TEX_BIN, FALSE}, {"centerdot", "⋅", TEX_BIN, FALSE},
    {"ast", "∗", TEX_BIN, FALSE}, {"star", "⋆", TEX_BIN, FALSE}, {"circ", "∘", TEX_BIN, FALSE},
    {"bullet", "∙", TEX_BIN, FALSE}, {"cap", "∩", TEX_BIN, FALSE}, {"cup", "∪", TEX_BIN, FALSE},
    {"wedge", "∧", TEX_BIN, FALSE}, {"land", "∧", TEX_BIN, FALSE}, {"vee", "∨", TEX_BIN, FALSE},
    {"lor", "∨", TEX_BIN, FALSE}, {"setminus", "∖", TEX_BIN, FALSE}, {"smallsetminus", "∖", TEX_BIN, FALSE},
    {"oplus", "⊕", TEX_BIN, FALSE}, {"ominus", "⊖", TEX_BIN, FALSE}, {"otimes", "⊗", TEX_BIN, FALSE},
    {"oslash", "⊘", TEX_BIN, FALSE}, {"odot", "⊙", TEX_BIN, FALSE}, {"uplus", "⊎", TEX_BIN, FALSE},
    {"sqcap", "⊓", TEX_BIN, FALSE}, {"sqcup", "⊔", TEX_BIN, FALSE}, {"triangleleft", "◃", TEX_BIN, FALSE},
    {"triangleright", "▹", TEX_BIN, FALSE}, {"wr", "≀", TEX_BIN, FALSE}, {"amalg", "⨿", TEX_BIN, FALSE},
    {"bigcirc", "◯", TEX_BIN, FALSE}, {"diamond", "⋄", TEX_BIN, FALSE}, {"dagger", "†", TEX_BIN, FALSE},
    {"ddagger", "‡", TEX_BIN, FALSE}, {"ltimes", "⋉", TEX_BIN, FALSE}, {"rtimes", "⋊", TEX_BIN, FALSE},
    {"intercal", "⊺", TEX_BIN, FALSE}, {"dotplus", "∔", TEX_BIN, FALSE}, {"barwedge", "⊼", TEX_BIN, FALSE},
    /* Relations and arrows */
    {"leq", "≤", TEX_REL, FALSE}, {"le", "≤", TEX_REL, FALSE}, {"geq", "≥", TEX_REL, FALSE},
    {"ge", "≥", TEX_REL, FALSE}, {"neq", "≠", TEX_REL, FALSE}, {"ne", "≠", TEX_REL, FALSE},
    {"equiv", "≡", TEX_REL, FALSE}, {"approx", "≈", TEX_REL, FALSE}, {"approxeq", "≊", TEX_REL, FALSE},
    {"sim", "∼", TEX_REL, FALSE}, {"simeq", "≃", TEX_REL, FALSE}, {"cong", "≅", TEX_REL, FALSE},
    {"propto", "∝", TEX_REL, FALSE}, {"varpropto", "∝", TEX_REL, FALSE}, {"ll", "≪", TEX_REL, FALSE},
    {"gg", "≫", TEX_REL, FALSE}, {"lll", "⋘", TEX_REL, FALSE}, {"ggg", "⋙", TEX_REL, FALSE},
    {"subset", "⊂", TEX_REL, FALSE}, {"supset", "⊃", TEX_REL, FALSE}, {"subseteq", "⊆", TEX_REL, FALSE},
    {"supseteq", "⊇", TEX_REL, FALSE}, {"subsetneq", "⊊", TEX_REL, FALSE}, {"supsetneq", "⊋", TEX_REL, FALSE},
    {"sqsubset", "⊏", TEX_REL, FALSE}, {"sqsupset", "⊐", TEX_REL, FALSE}, {"sqsubseteq", "⊑", TEX_REL, FALSE},
    {"sqsupseteq", "⊒", TEX_REL, FALSE}, {"in", "∈", TEX_REL, FALSE}, {"notin", "∉", TEX_REL, FALSE},
    {"ni", "∋", TEX_REL, FALSE}, {"owns", "∋", TEX_REL, FALSE}, {"mid", "∣", TEX_REL, FALSE},
    {"nmid", "∤", TEX_REL, FALSE}, {"parallel", "∥", TEX_REL, FALSE}, {"nparallel", "∦", TEX_REL, FALSE},
    {"perp", "⊥", TEX_REL, FALSE}, {"to", "→", TEX_REL, FALSE}, {"rightarrow", "→", TEX_REL, FALSE},
    {"leftarrow", "←", TEX_REL, FALSE}, {"gets", "←", TEX_REL, FALSE}, {"leftrightarrow", "↔", TEX_REL, FALSE},
    {"Rightarrow", "⇒", TEX_REL, FALSE}, {"Leftarrow", "⇐", TEX_REL, FALSE},
    {"Leftrightarrow", "⇔", TEX_REL, FALSE}, {"iff", "⟺", TEX_REL, FALSE}, {"implies", "⟹", TEX_REL, FALSE},
    {"impliedby", "⟸", TEX_REL, FALSE}, {"mapsto", "↦", TEX_REL, FALSE}, {"longmapsto", "⟼", TEX_REL, FALSE},
    {"longrightarrow", "⟶", TEX_REL, FALSE}, {"longleftarrow", "⟵", TEX_REL, FALSE},
    {"longleftrightarrow", "⟷", TEX_REL, FALSE}, {"Longrightarrow", "⟹", TEX_REL, FALSE},
    {"Longleftarrow", "⟸", TEX_REL, FALSE}, {"Longleftrightarrow", "⟺", TEX_REL, FALSE},
    {"uparrow", "↑", TEX_REL, FALSE}, {"downarrow", "↓", TEX_REL, FALSE}, {"updownarrow", "↕", TEX_REL, FALSE},
    {"Uparrow", "⇑", TEX_REL, FALSE}, {"Downarrow", "⇓", TEX_REL, FALSE}, {"nearrow", "↗", TEX_REL, FALSE},
    {"searrow", "↘", TEX_REL, FALSE}, {"swarrow", "↙", TEX_REL, FALSE}, {"nwarrow", "↖", TEX_REL, FALSE},
    {"hookrightarrow", "↪", TEX_REL, FALSE}, {"hookleftarrow", "↩", TEX_REL, FALSE},
    {"rightharpoonup", "⇀", TEX_REL, FALSE}, {"leftharpoonup", "↼", TEX_REL, FALSE},
    {"rightleftharpoons", "⇌", TEX_REL, FALSE}, {"leftrightarrows", "⇆", TEX_REL, FALSE},
    {"rightrightarrows", "⇉", TEX_REL, FALSE}, {"leadsto", "⇝", TEX_REL, FALSE},
    {"dashrightarrow", "⇢", TEX_REL, FALSE}, {"multimap", "⊸", TEX_REL, FALSE},
    {"prec", "≺", TEX_REL, FALSE}, {"succ", "≻", TEX_REL, FALSE}, {"preceq", "⪯", TEX_REL, FALSE},
    {"succeq", "⪰", TEX_REL, FALSE}, {"models", "⊨", TEX_REL, FALSE}, {"vdash", "⊢", TEX_REL, FALSE},
    {"dashv", "⊣", TEX_REL, FALSE}, {"vDash", "⊨", TEX_REL, FALSE}, {"asymp", "≍", TEX_REL, FALSE},
    {"doteq", "≐", TEX_REL, FALSE}, {"bowtie", "⋈", TEX_REL, FALSE}, {"Join", "⋈", TEX_REL, FALSE},
    {"smile", "⌣", TEX_REL, FALSE}, {"frown", "⌢", TEX_REL, FALSE}, {"leqslant", "⩽", TEX_REL, FALSE},
    {"geqslant", "⩾", TEX_REL, FALSE}, {"lesssim", "≲", TEX_REL, FALSE}, {"gtrsim", "≳", TEX_REL, FALSE},
    {"lessgtr", "≶", TEX_REL, FALSE}, {"coloneqq", "≔", TEX_REL, FALSE}, {"eqqcolon", "≕", TEX_REL, FALSE},
    {"triangleq", "≜", TEX_REL, FALSE}, {"circeq", "≗", TEX_REL, FALSE}, {"nleq", "≰", TEX_REL, FALSE},
    {"ngeq", "≱", TEX_REL, FALSE}, {"nless", "≮", TEX_REL, FALSE}, {"ngtr", "≯", TEX_REL, FALSE},
    {"nsim", "≁", TEX_REL, FALSE}, {"ncong", "≇", TEX_REL, FALSE}, {"nsubseteq", "⊈", TEX_REL, FALSE},
    {"nsupseteq", "⊉", TEX_REL, FALSE}, {"therefore", "∴", TEX_REL, FALSE}, {"because", "∵", TEX_REL, FALSE},
    /* Delimiters */
    {"langle", "⟨", TEX_OPEN, FALSE}, {"rangle", "⟩", TEX_CLOSE, FALSE}, {"lbrace", "{", TEX_OPEN, FALSE},
    {"rbrace", "}", TEX_CLOSE, FALSE}, {"lbrack", "[", TEX_OPEN, FALSE}, {"rbrack", "]", TEX_CLOSE, FALSE},
    {"lfloor", "⌊", TEX_OPEN, FALSE}, {"rfloor", "⌋", TEX_CLOSE, FALSE}, {"lceil", "⌈", TEX_OPEN, FALSE},
    {"rceil", "⌉", TEX_CLOSE, FALSE}, {"lvert", "|", TEX_OPEN, FALSE}, {"rvert", "|", TEX_CLOSE, FALSE},
    {"lVert", "‖", TEX_OPEN, FALSE}, {"rVert", "‖", TEX_CLOSE, FALSE}, {"ulcorner", "⌜", TEX_OPEN, FALSE},
    {"urcorner", "⌝", TEX_CLOSE, FALSE}, {"llcorner", "⌞", TEX_OPEN, FALSE}, {"lrcorner", "⌟", TEX_CLOSE, FALSE},
};

/* Big operators */
static const struct {
    const gchar* name;
    const gchar* text;
    TexLimits limits;
} BIG_OPS[] = {
    {"sum", "∑", TEX_LIMITS_DISPLAY},     {"prod", "∏", TEX_LIMITS_DISPLAY},
    {"coprod", "∐", TEX_LIMITS_DISPLAY},  {"int", "∫", TEX_LIMITS_SIDE},
    {"iint", "∬", TEX_LIMITS_SIDE},       {"iiint", "∭", TEX_LIMITS_SIDE},
    {"oint", "∮", TEX_LIMITS_SIDE},       {"oiint", "∯", TEX_LIMITS_SIDE},
    {"intop", "∫", TEX_LIMITS_DISPLAY},   {"bigcup", "⋃", TEX_LIMITS_DISPLAY},
    {"bigcap", "⋂", TEX_LIMITS_DISPLAY},  {"bigvee", "⋁", TEX_LIMITS_DISPLAY},
    {"bigwedge", "⋀", TEX_LIMITS_DISPLAY}, {"bigoplus", "⨁", TEX_LIMITS_DISPLAY},
    {"bigotimes", "⨂", TEX_LIMITS_DISPLAY}, {"bigodot", "⨀", TEX_LIMITS_DISPLAY},
    {"biguplus", "⨄", TEX_LIMITS_DISPLAY}, {"bigsqcup", "⨆", TEX_LIMITS_DISPLAY},
};

/* Function names, written upright */
static const struct {
    const gchar* name;
    const gchar* text;
    TexLimits limits;
} FUNCTIONS[] = {
    {"lim", "lim", TEX_LIMITS_DISPLAY},       {"liminf", "lim inf", TEX_LIMITS_DISPLAY},
    {"limsup", "lim sup", TEX_LIMITS_DISPLAY}, {"max", "max", TEX_LIMITS_DISPLAY},
    {"min", "min", TEX_LIMITS_DISPLAY},       {"sup", "sup", TEX_LIMITS_DISPLAY},
    {"inf", "inf", TEX_LIMITS_DISPLAY},       {"det", "det", TEX_LIMITS_DISPLAY},
    {"gcd", "gcd", TEX_LIMITS_DISPLAY},       {"Pr", "Pr", TEX_LIMITS_DISPLAY},
    {"argmax", "arg max", TEX_LIMITS_DISPLAY}, {"argmin", "arg min", TEX_LIMITS_DISPLAY},
    {"injlim", "inj lim", TEX_LIMITS_DISPLAY}, {"projlim", "proj lim", TEX_LIMITS_DISPLAY},
    {"sin", "sin", TEX_LIMITS_SIDE},          {"cos", "cos", TEX_LIMITS_SIDE},
    {"tan", "tan", TEX_LIMITS_SIDE},          {"cot", "cot", TEX_LIMITS_SIDE},
    {"sec", "sec", TEX_LIMITS_SIDE},          {"csc", "csc", TEX_LIMITS_SIDE},
    {"arcsin", "arcsin", TEX_LIMITS_SIDE},    {"arccos", "arccos", TEX_LIMITS_SIDE},
    {"arctan", "arctan", TEX_LIMITS_SIDE},    {"sinh", "sinh", TEX_LIMITS_SIDE},
    {"cosh", "cosh", TEX_LIMITS_SIDE},        {"tanh", "tanh", TEX_LIMITS_SIDE},
    {"coth", "coth", TEX_LIMITS_SIDE},        {"sech", "sech", TEX_LIMITS_SIDE},
    {"csch", "csch", TEX_LIMITS_SIDE},        {"log", "log", TEX_LIMITS_SIDE},
    {"ln", "ln", TEX_LIMITS_SIDE},            {"lg", "lg", TEX_LIMITS_SIDE},
    {"exp", "exp", TEX_LIMITS_SIDE},          {"dim", "dim", TEX_LIMITS_SIDE},
    {"ker", "ker", TEX_LIMITS_SIDE},          {"deg", "deg", TEX_LIMITS_SIDE},
    {"hom", "hom", TEX_LIMITS_SIDE},          {"arg", "arg", TEX_LIMITS_SIDE},
};

static const struct {
    const gchar* name;
    TexAccent kind;
    const gchar* glyph;
} ACCENTS[] = {
    {"hat", TEX_ACCENT_GLYPH, "ˆ"},          {"widehat", TEX_ACCENT_WIDE, "ˆ"},
    {"check", TEX_ACCENT_GLYPH, "ˇ"},        {"widecheck", TEX_ACCENT_WIDE, "ˇ"},
    {"tilde", TEX_ACCENT_GLYPH, "˜"},        {"widetilde", TEX_ACCENT_WIDE, "˜"},
    {"acute", TEX_ACCENT_GLYPH, "´"},        {"grave", TEX_ACCENT_GLYPH, "`"},
    {"dot", TEX_ACCENT_GLYPH, "˙"},          {"ddot", TEX_ACCENT_GLYPH, "¨"},
    {"breve", TEX_ACCENT_GLYPH, "˘"},        {"mathring", TEX_ACCENT_GLYPH, "˚"},
    {"bar", TEX_ACCENT_OVERLINE, NULL},      {"overline", TEX_ACCENT_OVERLINE, NULL},
    {"underline", TEX_ACCENT_UNDERLINE, NULL}, {"vec", TEX_ACCENT_VEC, NULL},
    {"overrightarrow", TEX_ACCENT_RIGHTARROW, NULL}, {"overleftarrow", TEX_ACCENT_LEFTARROW, NULL},
    {"overleftrightarrow", TEX_ACCENT_RIGHTARROW, NULL}, {"overbrace", TEX_ACCENT_OVERBRACE, NULL},
    {"underbrace", TEX_ACCENT_UNDERBRACE, NULL},
};

/* Negated relations for \not */
static const struct {
    const gchar* rel;
    const gchar* negated;
} NEGATED[] = {
    {"=", "≠"}, {"∈", "∉"}, {"≡", "≢"}, {"<", "≮"}, {">", "≯"}, {"≤", "≰"}, {"≥", "≱"}, {"⊂", "⊄"},
    {"⊃", "⊅"}, {"⊆", "⊈"}, {"⊇", "⊉"}, {"∼", "≁"}, {"≈", "≉"}, {"≅", "≇"}, {"∣", "∤"}, {"∥", "∦"},
    {"∃", "∄"}, {"≃", "≄"}, {"≺", "⊀"}, {"≻", "⊁"},
};

const TexSymbol* tex_symbol_find(const gchar* name) {
    for (gsize i = 0; i < G_N_ELEMENTS(SYMBOLS); i++) {
        if (strcmp(SYMBOLS[i].name, name) == 0) return &SYMBOLS[i];
    }
    return NULL;
}

const TexSymbol* tex_symbol_find_text(const gchar* text) {
    for (gsize i = 0; i < G_N_ELEMENTS(SYMBOLS); i++) {
        if (strcmp(SYMBOLS[i].text, text) == 0) return &SYMBOLS[i];
    }
    return NULL;
}

gboolean tex_symbol_big_op(const gchar* name, const gchar** text, TexLimits* limits) {
    for (gsize i = 0; i < G_N_ELEMENTS(BIG_OPS); i++) {
        if (strcmp(BIG_OPS[i].name, name) != 0) continue;
        *text = BIG_OPS[i].text;
        *limits = BIG_OPS[i].limits;
        return TRUE;
    }
    return FALSE;
}

gboolean tex_symbol_function(const gchar* name, const gchar** text, TexLimits* limits) {
    for (gsize i = 0; i < G_N_ELEMENTS(FUNCTIONS); i++) {
        if (strcmp(FUNCTIONS[i].name, name) != 0) continue;
        *text = FUNCTIONS[i].text;
        *limits = FUNCTIONS[i].limits;
        return TRUE;
    }
    return FALSE;
}

gboolean tex_symbol_accent(const gchar* name, TexAccent* kind, const gchar** glyph) {
    for (gsize i = 0; i < G_N_ELEMENTS(ACCENTS); i++) {
        if (strcmp(ACCENTS[i].name, name) != 0) continue;
        *kind = ACCENTS[i].kind;
        *glyph = ACCENTS[i].glyph;
        return TRUE;
    }
    return FALSE;
}

const gchar* tex_symbol_negated(const gchar* rel) {
    for (gsize i = 0; i < G_N_ELEMENTS(NEGATED); i++) {
        if (strcmp(NEGATED[i].rel, rel) == 0) return NEGATED[i].negated;
    }
    return NULL;
}
