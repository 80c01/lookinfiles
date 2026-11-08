/* Manual page formatting (see man_view.h). */
#include "formats/man_view.h"
#include "core/i18n.h"

#include <string.h>

/* Special characters: \(xx and \[name] */
static const struct {
    const gchar* name;
    const gchar* text;
} SPECIALS[] = {
    { "em", "—" }, { "en", "–" }, { "hy", "-" }, { "bu", "•" }, { "co", "©" }, { "rg", "®" },
    { "tm", "™" }, { "aq", "'" }, { "dq", "\"" }, { "lq", "“" }, { "rq", "”" }, { "oq", "‘" },
    { "cq", "’" }, { "ga", "`" }, { "ha", "^" }, { "ti", "~" }, { "rs", "\\" }, { "mi", "−" },
    { "pl", "+" }, { "mu", "×" }, { "di", "÷" }, { "+-", "±" }, { "<=", "≤" }, { ">=", "≥" },
    { "!=", "≠" }, { "->", "→" }, { "<-", "←" }, { "de", "°" }, { "sc", "§" }, { "ps", "¶" },
    { "Fo", "«" }, { "Fc", "»" }, { "lB", "[" }, { "rB", "]" }, { "lC", "{" }, { "rC", "}" },
    { "ba", "|" }, { "or", "|" }, { "at", "@" }, { "sh", "#" }, { "Eu", "€" }, { "eu", "€" },
    { "a'", "á" }, { "e'", "é" }, { "e`", "è" }, { "a`", "à" }, { "o:", "ö" }, { "u:", "ü" },
    { "a:", "ä" }, { "ss", "ß" }, { ",c", "ç" }, { "~n", "ñ" }, { "sl", "/" }, { "fm", "′" },
};

static const gchar* special(const gchar* name, gsize n) {
    for (gsize i = 0; i < G_N_ELEMENTS(SPECIALS); i++) {
        if (strlen(SPECIALS[i].name) == n && strncmp(SPECIALS[i].name, name, n) == 0) {
            return SPECIALS[i].text;
        }
    }
    return NULL;
}

typedef enum { FONT_R, FONT_B, FONT_I } Font;

typedef struct {
    PreviewDoc* doc;
    Font font;             /* current inline font */
    Font prev_font;        /* for \fP */
    gint font_start;       /* offset where the current B / I run started */
    gint par_start;        /* offset of the current paragraph, -1 if none */
    gint par_indent;       /* indent level of the current paragraph */
    gint base;             /* indent level of body text (sections: 1, .RS adds) */
    gint body;             /* indent level of the next paragraph text */
    gboolean nofill;
    gboolean tag_next;     /* .TP: the next text line is the tag */
    gboolean in_table;
    gboolean need_space;   /* fill mode: a space before the next text */
    gboolean any;          /* a request was seen */
    gboolean after_heading; /* no blank line right below a heading */
} Man;

static void set_font(Man* m, Font font) {
    gint now = preview_doc_offset(m->doc);
    if (m->font != FONT_R && now > m->font_start) {
        preview_doc_add_span(m->doc, m->font_start, now, m->font == FONT_B ? PREVIEW_STYLE_BOLD
                                                                           : PREVIEW_STYLE_ITALIC);
    }
    m->prev_font = m->font;
    m->font = font;
    m->font_start = now;
}

static const PreviewStyle INDENTS[] = { PREVIEW_STYLE_PLAIN, PREVIEW_STYLE_INDENT, PREVIEW_STYLE_INDENT2,
                                        PREVIEW_STYLE_INDENT3 };

/* End the current paragraph (line break and indentation span) */
static void end_par(Man* m) {
    if (m->par_start < 0) {
        return;
    }
    set_font(m, FONT_R);
    preview_doc_append_len(m->doc, "\n", 1);
    gint level = CLAMP(m->par_indent, 0, (gint)G_N_ELEMENTS(INDENTS) - 1);
    preview_doc_add_span(m->doc, m->par_start, preview_doc_offset(m->doc), INDENTS[level]);
    m->par_start = -1;
    m->need_space = FALSE;
}

static void blank_line(Man* m) {
    end_par(m);
    if (m->after_heading) {
        return;
    }
    const GString* t = m->doc->text;
    if (t->len > 0 && !(t->len >= 2 && t->str[t->len - 1] == '\n' && t->str[t->len - 2] == '\n')) {
        preview_doc_append_len(m->doc, "\n", 1);
    }
}

static void start_par(Man* m, gint indent) {
    m->after_heading = FALSE;
    if (m->par_start < 0) {
        m->par_start = preview_doc_offset(m->doc);
        m->par_indent = indent;
        m->font_start = m->par_start;
        m->need_space = FALSE;
    }
}

/* Append text with its escapes resolved and font changes applied */
static void emit(Man* m, const gchar* s) {
    GString* out = g_string_new(NULL);
#define FLUSH() do { preview_doc_append_len(m->doc, out->str, (gssize)out->len); g_string_truncate(out, 0); } while (0)
    for (const gchar* p = s; *p != '\0'; p++) {
        if (*p != '\\') {
            g_string_append_c(out, *p);
            continue;
        }
        p++;
        switch (*p) {
        case '\0':
            p--;
            break;
        case 'f': {
            /* \fB \fI \fR \fP \f(BI \f[B] */
            gchar f = p[1];
            gsize skip = 1;
            if (f == '(') {
                f = p[2] == 'B' || p[3] == 'B' ? 'B' : p[2];
                skip = 3;
            } else if (f == '[') {
                const gchar* close = strchr(p, ']');
                f = (close != NULL && close > p + 2) ? p[2] : 'R';
                skip = close != NULL ? (gsize)(close - p) : 1;
            }
            FLUSH();
            set_font(m, f == 'B' ? FONT_B : f == 'I' ? FONT_I : f == 'P' ? m->prev_font : FONT_R);
            p += skip;
            break;
        }
        case '(': {
            const gchar* sp = p[1] != '\0' && p[2] != '\0' ? special(p + 1, 2) : NULL;
            g_string_append(out, sp != NULL ? sp : "");
            p += (p[1] != '\0') + (p[1] != '\0' && p[2] != '\0');
            break;
        }
        case '[': {
            const gchar* close = strchr(p, ']');
            if (close != NULL) {
                const gchar* sp = special(p + 1, (gsize)(close - p - 1));
                if (sp != NULL) {
                    g_string_append(out, sp);
                } else if (p[1] == 'u' && close - p > 2) {
                    /* \[u00E9] */
                    gunichar c = (gunichar)g_ascii_strtoull(p + 2, NULL, 16);
                    if (g_unichar_validate(c)) {
                        g_string_append_unichar(out, c);
                    }
                }
                p = close;
            }
            break;
        }
        case '*': {
            /* Strings: \*(lq, \*(rq, \*R ... */
            const gchar* sp = NULL;
            if (p[1] == '(' && p[2] != '\0' && p[3] != '\0') {
                sp = special(p + 2, 2);
                p += 3;
            } else if (p[1] == '[') {
                const gchar* close = strchr(p, ']');
                if (close != NULL) {
                    sp = special(p + 2, (gsize)(close - p - 2));
                    p = close;
                }
            } else if (p[1] != '\0') {
                sp = p[1] == 'R' ? "®" : NULL;
                p++;
            }
            g_string_append(out, sp != NULL ? sp : "");
            break;
        }
        case 's':
            /* \s+1 \s-1 \s0: size changes are dropped */
            if (p[1] == '+' || p[1] == '-') {
                p++;
            }
            while (g_ascii_isdigit(p[1])) {
                p++;
            }
            break;
        case 'e':
        case '\\':
            g_string_append_c(out, '\\');
            break;
        case '-':
            g_string_append_c(out, '-');
            break;
        case '~':
        case ' ':
        case '0':
            g_string_append_c(out, ' ');
            break;
        case '&':
        case '|':
        case '^':
        case ')':
        case 'c':
        case ':':
        case '/':
        case ',':
            break;
        case '"':
            /* comment to the end of the line */
            p += strlen(p) - 1;
            break;
        case '.':
            g_string_append_c(out, '.');
            break;
        case '\'':
            g_string_append(out, "´");
            break;
        case '`':
            g_string_append_c(out, '`');
            break;
        default:
            g_string_append_c(out, *p);
            break;
        }
    }
    FLUSH();
#undef FLUSH
    g_string_free(out, TRUE);
}

/* A text line in fill mode joins the paragraph; in no-fill mode it is a line */
static void text_line(Man* m, const gchar* line) {
    if (m->tag_next) {
        /* Tag of a .TP: on its own line, then the body below it */
        end_par(m);
        start_par(m, m->base);
        emit(m, line);
        end_par(m);
        m->tag_next = FALSE;
        m->body = m->base + 1;
        return;
    }
    if (m->nofill || m->in_table) {
        start_par(m, m->body);
        if (m->in_table) {
            gchar* row = g_strdup(line);
            g_strdelimit(row, "\t", ' ');
            emit(m, row);
            g_free(row);
        } else {
            emit(m, line);
        }
        set_font(m, m->font); /* close the styled run at the line end */
        preview_doc_append_len(m->doc, "\n", 1);
        return;
    }
    if (line[0] == '\0') {
        blank_line(m);
        return;
    }
    start_par(m, m->body);
    if (m->need_space) {
        preview_doc_append_len(m->doc, " ", 1);
    }
    emit(m, line);
    m->need_space = TRUE;
}

/* Arguments of a request: space separated, "quoted args" kept together */
static gchar** split_args(const gchar* s) {
    GPtrArray* args = g_ptr_array_new();
    const gchar* p = s;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        GString* arg = g_string_new(NULL);
        if (*p == '"') {
            p++;
            while (*p != '\0') {
                if (*p == '"' && p[1] == '"') {
                    g_string_append_c(arg, '"');
                    p += 2;
                } else if (*p == '"') {
                    p++;
                    break;
                } else {
                    g_string_append_c(arg, *p++);
                }
            }
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                g_string_append_c(arg, *p++);
            }
        }
        g_ptr_array_add(args, g_string_free(arg, FALSE));
    }
    g_ptr_array_add(args, NULL);
    return (gchar**)g_ptr_array_free(args, FALSE);
}

/* .BR, .IR... : arguments alternate between two fonts, without spaces */
static void alternate(Man* m, gchar** args, Font a, Font b) {
    start_par(m, m->body);
    if (m->need_space) {
        preview_doc_append_len(m->doc, " ", 1);
    }
    for (gint i = 0; args[i] != NULL; i++) {
        set_font(m, i % 2 == 0 ? a : b);
        emit(m, args[i]);
    }
    set_font(m, FONT_R);
    m->need_space = TRUE;
}

static void heading(Man* m, const gchar* text, gboolean sub) {
    blank_line(m);
    gchar* plain = man_view_plain(text);
    gint start = preview_doc_offset(m->doc);
    if (sub) {
        preview_doc_append_len(m->doc, "   ", 3);
    }
    preview_doc_append(m->doc, PREVIEW_STYLE_SECTION, plain);
    preview_doc_append_len(m->doc, "\n", 1);
    (void)start;
    g_free(plain);
    m->base = 1;
    m->body = 1;
    m->tag_next = FALSE;
    m->after_heading = TRUE;
}

static gchar* join_args(gchar** args) {
    return g_strjoinv(" ", args);
}

static void request(Man* m, const gchar* name, const gchar* rest) {
    gchar** args = split_args(rest);
    gchar* all = join_args(args);
    m->any = TRUE;
    if (strcmp(name, "TH") == 0 || strcmp(name, "Dt") == 0) {
        /* Title line: NAME(section) ... NAME(section) */
        gchar* title = args[0] != NULL ? man_view_plain(args[0]) : g_strdup("");
        gchar* head = g_strdup_printf("%s(%s)", title, args[0] != NULL && args[1] != NULL ? args[1] : "");
        preview_doc_append(m->doc, PREVIEW_STYLE_TITLE, head);
        if (args[0] != NULL && args[1] != NULL && args[2] != NULL && args[3] != NULL && args[4] != NULL &&
            args[4][0] != '\0') {
            gchar* manual = man_view_plain(args[4]);
            preview_doc_appendf(m->doc, PREVIEW_STYLE_DIM, "   %s", manual);
            g_free(manual);
        }
        preview_doc_append_len(m->doc, "\n", 1);
        if (args[0] != NULL && args[1] != NULL && args[2] != NULL && args[2][0] != '\0') {
            gchar* date = man_view_plain(args[2]);
            gchar* source = args[3] != NULL ? man_view_plain(args[3]) : g_strdup("");
            preview_doc_appendf(m->doc, PREVIEW_STYLE_DIM, "%s%s%s\n", source, source[0] != '\0' ? " · " : "", date);
            g_free(source);
            g_free(date);
        }
        g_free(head);
        g_free(title);
    } else if (strcmp(name, "SH") == 0 || strcmp(name, "Sh") == 0) {
        heading(m, all, FALSE);
    } else if (strcmp(name, "SS") == 0 || strcmp(name, "Ss") == 0) {
        heading(m, all, TRUE);
    } else if (strcmp(name, "PP") == 0 || strcmp(name, "LP") == 0 || strcmp(name, "P") == 0 ||
               strcmp(name, "Pp") == 0 || strcmp(name, "sp") == 0) {
        blank_line(m);
        m->body = m->base;
        m->tag_next = FALSE;
    } else if (strcmp(name, "TP") == 0 || strcmp(name, "TQ") == 0) {
        if (strcmp(name, "TP") == 0) {
            blank_line(m);
        } else {
            end_par(m);
        }
        m->tag_next = TRUE;
    } else if (strcmp(name, "IP") == 0 || strcmp(name, "It") == 0) {
        blank_line(m);
        if (args[0] != NULL && args[0][0] != '\0') {
            start_par(m, m->base);
            emit(m, strcmp(name, "It") == 0 ? all : args[0]);
            end_par(m);
        }
        m->body = m->base + 1;
        m->tag_next = FALSE;
    } else if (strcmp(name, "RS") == 0) {
        end_par(m);
        m->base = MIN(m->base + 1, 3);
        m->body = m->base;
    } else if (strcmp(name, "RE") == 0) {
        end_par(m);
        m->base = MAX(m->base - 1, 1);
        m->body = m->base;
    } else if (strcmp(name, "br") == 0) {
        end_par(m);
    } else if (strcmp(name, "nf") == 0 || strcmp(name, "EX") == 0 || strcmp(name, "Bd") == 0) {
        end_par(m);
        m->nofill = TRUE;
    } else if (strcmp(name, "fi") == 0 || strcmp(name, "EE") == 0 || strcmp(name, "Ed") == 0) {
        end_par(m);
        m->nofill = FALSE;
    } else if (strcmp(name, "TS") == 0) {
        end_par(m);
        m->in_table = TRUE;
    } else if (strcmp(name, "TE") == 0) {
        end_par(m);
        m->in_table = FALSE;
    } else if (strcmp(name, "B") == 0 || strcmp(name, "SB") == 0 || strcmp(name, "Fl") == 0 ||
               strcmp(name, "Nm") == 0 || strcmp(name, "Cm") == 0) {
        start_par(m, m->body);
        if (m->need_space) {
            preview_doc_append_len(m->doc, " ", 1);
        }
        if (strcmp(name, "Fl") == 0) {
            preview_doc_append_len(m->doc, "-", 1);
        }
        set_font(m, FONT_B);
        emit(m, all);
        set_font(m, FONT_R);
        m->need_space = TRUE;
    } else if (strcmp(name, "I") == 0 || strcmp(name, "Ar") == 0 || strcmp(name, "Pa") == 0 ||
               strcmp(name, "Em") == 0) {
        start_par(m, m->body);
        if (m->need_space) {
            preview_doc_append_len(m->doc, " ", 1);
        }
        set_font(m, FONT_I);
        emit(m, all);
        set_font(m, FONT_R);
        m->need_space = TRUE;
    } else if (strcmp(name, "BR") == 0) {
        alternate(m, args, FONT_B, FONT_R);
    } else if (strcmp(name, "RB") == 0) {
        alternate(m, args, FONT_R, FONT_B);
    } else if (strcmp(name, "IR") == 0) {
        alternate(m, args, FONT_I, FONT_R);
    } else if (strcmp(name, "RI") == 0) {
        alternate(m, args, FONT_R, FONT_I);
    } else if (strcmp(name, "BI") == 0) {
        alternate(m, args, FONT_B, FONT_I);
    } else if (strcmp(name, "IB") == 0) {
        alternate(m, args, FONT_I, FONT_B);
    } else if (strcmp(name, "Nd") == 0) {
        text_line(m, "—");
        text_line(m, all);
    } else if (strcmp(name, "Xr") == 0 && args[0] != NULL) {
        gchar* ref = g_strdup_printf("\\fB%s\\fR(%s)", args[0], args[1] != NULL ? args[1] : "");
        text_line(m, ref);
        g_free(ref);
    } else if (strcmp(name, "UR") == 0 || strcmp(name, "MT") == 0) {
        text_line(m, all);
    } else if (strcmp(name, "so") == 0) {
        gchar* see = g_strdup_printf(_("See %s."), all);
        text_line(m, see);
        g_free(see);
    } else if (strcmp(name, "SY") == 0) {
        blank_line(m);
        start_par(m, m->base);
        set_font(m, FONT_B);
        emit(m, all);
        set_font(m, FONT_R);
        m->need_space = TRUE;
    } else if (strcmp(name, "OP") == 0) {
        start_par(m, m->body);
        preview_doc_append_len(m->doc, m->need_space ? " [" : "[", -1);
        set_font(m, FONT_B);
        emit(m, args[0] != NULL ? args[0] : "");
        set_font(m, FONT_R);
        if (args[0] != NULL && args[1] != NULL) {
            preview_doc_append_len(m->doc, " ", 1);
            set_font(m, FONT_I);
            emit(m, args[1]);
            set_font(m, FONT_R);
        }
        preview_doc_append_len(m->doc, "]", 1);
        m->need_space = TRUE;
    } else if (strcmp(name, "YS") == 0 || strcmp(name, "Op") == 0 || strcmp(name, "Dl") == 0 ||
               strcmp(name, "Li") == 0 || strcmp(name, "Va") == 0 || strcmp(name, "Ev") == 0 ||
               strcmp(name, "Dv") == 0 || strcmp(name, "Er") == 0 || strcmp(name, "Ic") == 0 ||
               strcmp(name, "Sx") == 0 || strcmp(name, "Ql") == 0) {
        if (all[0] != '\0') {
            text_line(m, all);
        }
    }
    /* Other requests (.ad, .na, .hy, .ds, .de, .if, .ie, .ne, .PD, .UE...) are ignored */
    g_free(all);
    g_strfreev(args);
}

gboolean man_view_append(PreviewDoc* doc, const gchar* text, gsize len) {
    PreviewDoc* body = preview_doc_new();
    Man m = { .doc = body, .font = FONT_R, .prev_font = FONT_R, .par_start = -1, .base = 0, .body = 0 };
    gchar* copy = g_strndup(text, len);
    gchar** lines = g_strsplit(copy, "\n", -1);
    g_free(copy);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = lines[i];
        gsize n = strlen(line);
        if (n > 0 && line[n - 1] == '\r') {
            line[--n] = '\0';
        }
        if ((line[0] == '.' || line[0] == '\'') && !m.in_table) {
            const gchar* p = line + 1;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (p[0] == '\\' && p[1] == '"') {
                continue; /* comment */
            }
            gsize name_len = strcspn(p, " \t");
            gchar* name = g_strndup(p, name_len);
            request(&m, name, p + name_len);
            g_free(name);
            continue;
        }
        if (line[0] == '.' && m.in_table) {
            if (g_str_has_prefix(line, ".TE")) {
                request(&m, "TE", "");
            }
            continue;
        }
        if (m.in_table && (strchr(line, ';') == line + n - 1 || g_str_has_suffix(line, "."))) {
            /* tbl options and format lines ("allbox;", "l l l.") */
            gboolean format = TRUE;
            for (const gchar* c = line; *c != '\0'; c++) {
                format &= strchr("lrcnsaLRCNSA|^_=.; \tbBiIfwe()0123456789", *c) != NULL;
            }
            if (format || strchr(line, ';') == line + n - 1) {
                continue;
            }
        }
        text_line(&m, line);
    }
    end_par(&m);
    g_strfreev(lines);
    if (!m.any) {
        preview_doc_free(body);
        return FALSE;
    }
    /* Append the body to doc */
    gint base = preview_doc_offset(doc);
    preview_doc_append_len(doc, body->text->str, (gssize)body->text->len);
    for (guint i = 0; i < body->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(body->spans, PreviewSpan, i);
        preview_doc_add_span(doc, base + s->start, base + s->end, s->style);
    }
    preview_doc_free(body);
    return TRUE;
}

gchar* man_view_plain(const gchar* line) {
    PreviewDoc* doc = preview_doc_new();
    Man m = { .doc = doc, .font = FONT_R, .prev_font = FONT_R, .par_start = -1 };
    emit(&m, line);
    gchar* out = g_strdup(doc->text->str);
    preview_doc_free(doc);
    return out;
}
