/* Messages of a TeX log (see latex_log.h) */
#include "latex/latex_log.h"

#include <stdlib.h>
#include <string.h>

/* TeX wraps the lines of its log at max_print_line (79 by default) */
#define LOG_WRAP 79
/* Lines after an error where its "l.12" line is looked for */
#define CONTEXT_LINES 12
/* Lines of box content shown after an overfull / underfull \hbox */
#define BADBOX_LINES 12

static void message_free(gpointer p) {
    LatexMessage* m = p;
    g_free(m->file);
    g_free(m->text);
    g_free(m->context);
    g_free(m);
}

/* The lines of the log, those wrapped by TeX joined again */
static GPtrArray* unwrapped_lines(const gchar* log, gssize len) {
    GPtrArray* lines = g_ptr_array_new_with_free_func(g_free);
    gsize n = len < 0 ? strlen(log) : (gsize)len;
    const gchar* end = log + n;
    GString* cur = NULL;
    for (const gchar* p = log; p < end;) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        const gchar* stop = nl != NULL ? nl : end;
        gsize l = (gsize)(stop - p);
        if (l > 0 && p[l - 1] == '\r') l--;
        if (cur == NULL) cur = g_string_new(NULL);
        g_string_append_len(cur, p, (gssize)l);
        /* A full line (bytes for pdfTeX, characters for XeTeX / LuaTeX)
         * continues on the next one */
        gboolean wrapped = nl != NULL &&
                           (l == LOG_WRAP || (g_utf8_validate(p, (gssize)l, NULL) && g_utf8_strlen(p, (gssize)l) == LOG_WRAP));
        if (!wrapped) {
            g_ptr_array_add(lines, g_string_free(cur, FALSE));
            cur = NULL;
        }
        p = nl != NULL ? nl + 1 : end;
    }
    if (cur != NULL) g_ptr_array_add(lines, g_string_free(cur, FALSE));
    return lines;
}

static gboolean has_suffix_ci(const gchar* s, gsize len, const gchar* suffix) {
    gsize n = strlen(suffix);
    return len >= n && g_ascii_strncasecmp(s + len - n, suffix, n) == 0;
}

/* Whether the len bytes at s, written after "(" in the log, name a file */
static gboolean looks_like_file(const gchar* s, gsize len) {
    static const gchar* const exts[] = {
        ".tex", ".sty", ".cls", ".clo", ".cfg", ".def", ".fd", ".aux", ".toc", ".lof", ".lot",
        ".bbl", ".out", ".ltx", ".dtx", ".ldf", ".bib", ".nav", ".snm", ".lua", ".mkii", ".ind",
        ".gls", ".idx", ".dict", ".tikz", ".pgf", ".pdf_tex", ".code.tex",
    };
    if (len == 0) return FALSE;
    if (s[0] == '/' || s[0] == '~' || (len > 1 && s[0] == '.' && (s[1] == '/' || s[1] == '.'))) return TRUE;
    if (len > 2 && g_ascii_isalpha(s[0]) && s[1] == ':' && (s[2] == '/' || s[2] == '\\')) return TRUE;
    for (gsize i = 0; i < G_N_ELEMENTS(exts); i++) {
        if (has_suffix_ci(s, len, exts[i])) return TRUE;
    }
    return FALSE;
}

/* Follow the files opened ("(./file.tex") and closed (")") on a line. The
 * stack holds a file name, or NULL for a parenthesis that is not a file. */
static void track_files(GPtrArray* stack, const gchar* line) {
    for (const gchar* p = line; *p != '\0'; p++) {
        if (*p == '(') {
            const gchar* s = p + 1;
            const gchar* e = s;
            /* A quoted name ("(\"./my file.tex\"") may hold spaces */
            if (*s == '"') {
                const gchar* q = strchr(s + 1, '"');
                if (q != NULL) {
                    g_ptr_array_add(stack, g_strndup(s + 1, (gsize)(q - s - 1)));
                    p = q;
                    continue;
                }
            }
            while (*e != '\0' && *e != ' ' && *e != '(' && *e != ')' && *e != '[' && *e != '{' && *e != '<') e++;
            g_ptr_array_add(stack, looks_like_file(s, (gsize)(e - s)) ? g_strndup(s, (gsize)(e - s)) : NULL);
            p = e - 1;
        } else if (*p == ')' && stack->len > 0) {
            g_ptr_array_remove_index(stack, stack->len - 1);
        }
    }
}

/* The innermost file open, or NULL */
static const gchar* current_file(GPtrArray* stack) {
    for (guint i = stack->len; i > 0; i--) {
        const gchar* f = g_ptr_array_index(stack, i - 1);
        if (f != NULL) return f;
    }
    return NULL;
}

/* A message already found (hyperref repeats its warnings), or NULL */
static LatexMessage* find_message(GPtrArray* msgs, LatexMsgKind kind, const gchar* file, gint line, const gchar* text) {
    for (guint i = 0; i < msgs->len; i++) {
        LatexMessage* m = g_ptr_array_index(msgs, i);
        if (m->kind == kind && m->line == line && g_strcmp0(m->file, file) == 0 && strcmp(m->text, text) == 0) return m;
    }
    return NULL;
}

/* The message, NULL when it was found already */
static LatexMessage* add_message(GPtrArray* msgs, LatexMsgKind kind, const gchar* file, gint line, const gchar* text) {
    gchar* stripped = g_strstrip(g_strdup(text));
    if (find_message(msgs, kind, file, line, stripped) != NULL) {
        g_free(stripped);
        return NULL;
    }
    LatexMessage* m = g_new0(LatexMessage, 1);
    m->kind = kind;
    m->file = g_strdup(file);
    m->line = line;
    m->text = stripped;
    g_ptr_array_add(msgs, m);
    return m;
}

/* "./file.tex:12: message" (-file-line-error): the length of the file name,
 * its line and the message; 0 when the line is not one */
static gsize file_line_error(const gchar* line, gint* lineno, const gchar** text) {
    if (line[0] == '\0' || line[0] == ' ' || line[0] == '!') return 0;
    for (const gchar* c = strchr(line, ':'); c != NULL; c = strchr(c + 1, ':')) {
        const gchar* d = c + 1;
        if (!g_ascii_isdigit(*d)) continue;
        while (g_ascii_isdigit(*d)) d++;
        if (d[0] != ':' || d[1] != ' ') continue;
        gsize flen = (gsize)(c - line);
        /* The name has an extension (after its last directory) */
        const gchar* dot = g_strrstr_len(line, (gssize)flen, ".");
        const gchar* slash = g_strrstr_len(line, (gssize)flen, "/");
        if (dot == NULL || (slash != NULL && dot < slash)) return 0;
        *lineno = atoi(c + 1);
        *text = d + 2;
        return flen;
    }
    return 0;
}

/* After an error at lines[i]: its "l.12 source" line within the next lines,
 * as the line number and the source; returns the index of the last line of
 * the error (the l. line and the rest of the source after it), or i */
static guint error_context(GPtrArray* lines, guint i, gint* lineno, gchar** context) {
    for (guint j = i + 1; j < lines->len && j <= i + CONTEXT_LINES; j++) {
        const gchar* l = g_ptr_array_index(lines, j);
        if (l[0] == '!' || file_line_error(l, &(gint){0}, &(const gchar*){NULL}) > 0) break;
        if (l[0] != 'l' || l[1] != '.' || !g_ascii_isdigit(l[2])) continue;
        gchar* rest = NULL;
        gint n = (gint)g_ascii_strtoll(l + 2, &rest, 10);
        if (lineno != NULL) *lineno = n;
        /* TeX breaks the source line at the error: the end is on the next line */
        gchar* src = g_strstrip(g_strdup(rest));
        if (j + 1 < lines->len) {
            const gchar* next = g_ptr_array_index(lines, j + 1);
            gchar* tail = g_strstrip(g_strdup(next));
            if (tail[0] != '\0') {
                gchar* joined = g_strconcat(src, " ", tail, NULL);
                g_free(src);
                src = joined;
            }
            g_free(tail);
            j++;
        }
        *context = src;
        return j;
    }
    return i;
}

/* "Package hyperref Warning: text": the name ("hyperref", "LaTeX", "Font")
 * whose continuation lines start with "(name)", and the text */
static gboolean warning_start(const gchar* line, gchar** name, const gchar** text) {
    static const gchar* const plain[][2] = {
        {"LaTeX Warning: ", "LaTeX"},
        {"LaTeX Font Warning: ", "Font"},
        {"LaTeX3 Warning: ", "LaTeX3"},
        {"pdfTeX warning: ", "pdfTeX"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(plain); i++) {
        if (g_str_has_prefix(line, plain[i][0])) {
            *name = g_strdup(plain[i][1]);
            *text = line + strlen(plain[i][0]);
            return TRUE;
        }
    }
    const gchar* rest = g_str_has_prefix(line, "Package ") ? line + 8 : g_str_has_prefix(line, "Class ") ? line + 6 : NULL;
    if (rest == NULL) return FALSE;
    const gchar* sp = strchr(rest, ' ');
    if (sp == NULL || sp == rest || !g_str_has_prefix(sp, " Warning: ")) return FALSE;
    *name = g_strndup(rest, (gsize)(sp - rest));
    *text = sp + strlen(" Warning: ");
    return TRUE;
}

/* The line of "... on input line 12." at the end of text (removed from it) */
static gint input_line(gchar* text) {
    gchar* at = g_strrstr(text, " on input line ");
    if (at == NULL) return 0;
    gint n = atoi(at + strlen(" on input line "));
    if (n > 0) *at = '\0';
    return n;
}

/* "Overfull \hbox (12.0pt too wide) in paragraph at lines 3--5": the
 * message without its place, and its first line */
static gchar* badbox_text(const gchar* line, gint* lineno) {
    static const gchar* const places[] = {
        " in paragraph at lines ", " in alignment at lines ", " detected at line ", " at lines ", " at line ",
    };
    const gchar* cut = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(places); i++) {
        const gchar* at = strstr(line, places[i]);
        if (at != NULL && (cut == NULL || at < cut)) {
            cut = at;
            *lineno = atoi(at + strlen(places[i]));
        }
    }
    if (cut == NULL) {
        const gchar* at = strstr(line, " has occurred while");
        cut = at != NULL ? at : line + strlen(line);
    }
    return g_strndup(line, (gsize)(cut - line));
}

GPtrArray* latex_log_parse(const gchar* log, gssize len) {
    GPtrArray* msgs = g_ptr_array_new_with_free_func(message_free);
    if (log == NULL) return msgs;
    GPtrArray* lines = unwrapped_lines(log, len);
    GPtrArray* stack = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < lines->len; i++) {
        const gchar* line = g_ptr_array_index(lines, i);
        gint lineno = 0;
        const gchar* text = NULL;
        gchar* name = NULL;
        gsize flen = file_line_error(line, &lineno, &text);
        if (flen > 0 || line[0] == '!') {
            gchar* file = flen > 0 ? g_strndup(line, flen) : g_strdup(current_file(stack));
            gchar* context = NULL;
            gint at = 0;
            guint last = error_context(lines, i, &at, &context);
            if (lineno == 0) lineno = at;
            LatexMessage* m = add_message(msgs, LATEX_MSG_ERROR, file, lineno, flen > 0 ? text : line + 1);
            if (m != NULL) {
                m->context = context;
            } else {
                g_free(context);
            }
            g_free(file);
            i = last;
        } else if (warning_start(line, &name, &text)) {
            /* Continuation lines: "(hyperref)   more text" */
            GString* all = g_string_new(text);
            gchar* prefix = g_strdup_printf("(%s)", name);
            while (i + 1 < lines->len && g_str_has_prefix(g_ptr_array_index(lines, i + 1), prefix)) {
                const gchar* more = (const gchar*)g_ptr_array_index(lines, i + 1) + strlen(prefix);
                while (*more == ' ') more++;
                g_string_append_c(all, ' ');
                g_string_append(all, more);
                i++;
            }
            gint n = input_line(all->str);
            add_message(msgs, LATEX_MSG_WARNING, current_file(stack), n, all->str);
            g_string_free(all, TRUE);
            g_free(prefix);
            g_free(name);
        } else if (g_str_has_prefix(line, "Overfull \\") || g_str_has_prefix(line, "Underfull \\")) {
            gchar* t = badbox_text(line, &lineno);
            add_message(msgs, LATEX_MSG_BADBOX, current_file(stack), lineno, t);
            g_free(t);
            /* The box content follows (its parentheses are not files), up to a blank line */
            if (strstr(line, "\\hbox") != NULL) {
                for (guint j = i + 1; j < lines->len && j <= i + BADBOX_LINES; j++) {
                    if (((const gchar*)g_ptr_array_index(lines, j))[0] == '\0') {
                        i = j;
                        break;
                    }
                }
            }
        } else {
            track_files(stack, line);
        }
    }
    g_ptr_array_free(stack, TRUE);
    g_ptr_array_free(lines, TRUE);
    return msgs;
}

guint latex_log_count(const GPtrArray* msgs, LatexMsgKind kind) {
    guint n = 0;
    for (guint i = 0; msgs != NULL && i < msgs->len; i++) {
        if (((const LatexMessage*)g_ptr_array_index(msgs, i))->kind == kind) n++;
    }
    return n;
}
