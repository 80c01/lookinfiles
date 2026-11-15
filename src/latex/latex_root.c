/* The main document of a LaTeX file (see latex_root.h) */
#include "latex/latex_root.h"

#include <string.h>

/* Magic comments are read in the first lines only */
#define MAGIC_LINES 20

/* The value of "% !TEX key = value" in the first lines of text, or NULL */
static gchar* magic_value(const gchar* text, const gchar* key) {
    if (text == NULL) return NULL;
    const gchar* p = text;
    for (gint n = 0; n < MAGIC_LINES && *p != '\0'; n++) {
        const gchar* nl = strchr(p, '\n');
        gsize len = nl != NULL ? (gsize)(nl - p) : strlen(p);
        gchar* line = g_strndup(p, len);
        const gchar* s = line;
        while (*s == ' ' || *s == '\t') s++;
        gchar* found = NULL;
        if (*s == '%') {
            s++;
            while (*s == ' ' || *s == '\t') s++;
            if (*s == '!') {
                s++;
                while (*s == ' ' || *s == '\t') s++;
                if (g_ascii_strncasecmp(s, "TEX", 3) == 0) {
                    s += 3;
                    while (*s == ' ' || *s == '\t') s++;
                    gsize klen = strlen(key);
                    if (g_ascii_strncasecmp(s, key, klen) == 0) {
                        s += klen;
                        while (*s == ' ' || *s == '\t') s++;
                        if (*s == '=') found = g_strstrip(g_strdup(s + 1));
                    }
                }
            }
        }
        g_free(line);
        if (found != NULL) {
            if (found[0] != '\0') return found;
            g_free(found);
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
    return NULL;
}

gchar* latex_root_magic(const gchar* text) {
    return magic_value(text, "root");
}

/* text without its comments (from an unescaped % to the end of the line) */
static gchar* uncommented(const gchar* text) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = text; p != NULL && *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            g_string_append_c(out, *p++);
            g_string_append_c(out, *p);
        } else if (*p == '%') {
            while (*p != '\0' && *p != '\n') p++;
            if (*p == '\0') break;
            g_string_append_c(out, '\n');
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

/* Whether the command cmd ("\\documentclass") is used in t: not followed
 * by a letter (\documentclassfoo is another command) */
static gboolean uses_command(const gchar* t, const gchar* cmd) {
    gsize n = strlen(cmd);
    for (const gchar* at = strstr(t, cmd); at != NULL; at = strstr(at + 1, cmd)) {
        if (!g_ascii_isalpha(at[n])) return TRUE;
    }
    return FALSE;
}

gboolean latex_root_is_main(const gchar* text) {
    if (text == NULL) return FALSE;
    gchar* t = uncommented(text);
    gboolean found = uses_command(t, "\\documentclass") || uses_command(t, "\\documentstyle");
    g_free(t);
    return found;
}

/* Whether t loads package (in \usepackage{a,package,b} or \RequirePackage) */
static gboolean uses_package(const gchar* t, const gchar* package) {
    static const gchar* const cmds[] = {"\\usepackage", "\\RequirePackage"};
    gsize plen = strlen(package);
    for (gsize c = 0; c < G_N_ELEMENTS(cmds); c++) {
        for (const gchar* at = strstr(t, cmds[c]); at != NULL; at = strstr(at + 1, cmds[c])) {
            const gchar* p = at + strlen(cmds[c]);
            while (*p == ' ' || *p == '\n') p++;
            if (*p == '[') {
                p = strchr(p, ']');
                if (p == NULL) break;
                p++;
                while (*p == ' ' || *p == '\n') p++;
            }
            if (*p != '{') continue;
            const gchar* end = strchr(p, '}');
            if (end == NULL) break;
            /* One name of the list */
            for (const gchar* s = p + 1; s < end;) {
                while (s < end && (*s == ' ' || *s == ',' || *s == '\n')) s++;
                const gchar* e = s;
                while (e < end && *e != ',' && *e != ' ' && *e != '\n') e++;
                if ((gsize)(e - s) == plen && strncmp(s, package, plen) == 0) return TRUE;
                s = e;
            }
        }
    }
    return FALSE;
}

LatexEngine latex_root_engine(const gchar* text) {
    gchar* program = magic_value(text, "program");
    if (program == NULL) program = magic_value(text, "TS-program");
    if (program != NULL) {
        gchar* p = g_ascii_strdown(program, -1);
        g_free(program);
        LatexEngine e = strstr(p, "xe") != NULL    ? LATEX_ENGINE_XELATEX
                        : strstr(p, "lua") != NULL ? LATEX_ENGINE_LUALATEX
                                                   : LATEX_ENGINE_PDFLATEX;
        g_free(p);
        return e;
    }
    if (text == NULL) return LATEX_ENGINE_PDFLATEX;
    gchar* t = uncommented(text);
    LatexEngine e = LATEX_ENGINE_PDFLATEX;
    if (uses_package(t, "luacode") || uses_package(t, "luatexbase") || uses_package(t, "luaotfload") ||
        uses_command(t, "\\directlua")) {
        e = LATEX_ENGINE_LUALATEX;
    } else if (uses_package(t, "fontspec") || uses_package(t, "unicode-math") || uses_package(t, "polyglossia") ||
               uses_package(t, "xeCJK")) {
        e = LATEX_ENGINE_XELATEX;
    }
    g_free(t);
    return e;
}

const gchar* latex_root_engine_name(LatexEngine engine) {
    switch (engine) {
    case LATEX_ENGINE_XELATEX: return "xelatex";
    case LATEX_ENGINE_LUALATEX: return "lualatex";
    case LATEX_ENGINE_PDFLATEX: break;
    }
    return "pdflatex";
}

/* Whether t includes stem: \input{stem}, \include{stem.tex}, \subfile{./stem}, \input stem */
static gboolean includes(const gchar* t, const gchar* stem) {
    static const gchar* const cmds[] = {"\\input", "\\include", "\\subfile", "\\InputIfFileExists"};
    gsize slen = strlen(stem);
    for (gsize c = 0; c < G_N_ELEMENTS(cmds); c++) {
        gsize clen = strlen(cmds[c]);
        for (const gchar* at = strstr(t, cmds[c]); at != NULL; at = strstr(at + 1, cmds[c])) {
            const gchar* p = at + clen;
            if (g_ascii_isalpha(*p)) continue;
            while (*p == ' ') p++;
            gboolean brace = *p == '{';
            if (brace) p++;
            while (*p == ' ') p++;
            if (g_str_has_prefix(p, "./")) p += 2;
            if (strncmp(p, stem, slen) != 0) continue;
            p += slen;
            if (g_str_has_prefix(p, ".tex")) p += 4;
            if (brace ? (*p == '}' || *p == ' ') : (*p == '\0' || *p == ' ' || *p == '\n' || *p == '\\')) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

gint latex_root_choose(const gchar* const* names, const gchar* const* texts, const gchar* const* stems, guint n) {
    gint including = -1;
    gint main_tex = -1;
    for (guint i = 0; i < n; i++) {
        if (texts[i] == NULL) continue;
        gchar* t = uncommented(texts[i]);
        if (including < 0 && stems != NULL && stems[i] != NULL && includes(t, stems[i])) including = (gint)i;
        g_free(t);
        gchar* base = g_path_get_basename(names[i]);
        if (main_tex < 0 && strcmp(base, "main.tex") == 0) main_tex = (gint)i;
        g_free(base);
    }
    if (including >= 0) return including;
    if (n == 1) return 0;
    return main_tex;
}
