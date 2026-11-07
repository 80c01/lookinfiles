/* Ex command line parsing */
#include "vim_ex.h"
#include <string.h>

typedef struct {
    const gchar* name;
    gint min;       /* shortest accepted abbreviation */
    const gchar* full;  /* canonical name when the entry is an alias */
} ExName;

/* Order matters when abbreviations overlap: the first match wins */
static const ExName EX_NAMES[] = {
    {"substitute", 1, NULL}, {"set", 2, NULL}, {"setlocal", 4, "set"}, {"saveas", 3, NULL},
    {"sort", 3, NULL},
    {"write", 1, NULL}, {"wq", 2, NULL}, {"wall", 2, NULL}, {"wqall", 3, NULL},
    {"xit", 1, NULL}, {"xall", 2, NULL}, {"exit", 3, "xit"},
    {"quit", 1, NULL}, {"qall", 2, NULL}, {"quitall", 5, "qall"},
    {"edit", 1, NULL}, {"enew", 3, NULL},
    {"update", 2, NULL}, {"undo", 1, NULL},
    {"delete", 1, NULL}, {"display", 2, "registers"},
    {"yank", 1, NULL},
    {"print", 1, NULL}, {"put", 2, NULL},
    {"move", 1, NULL}, {"mark", 2, NULL}, {"marks", 4, NULL},
    {"copy", 2, NULL}, {"t", 1, "copy"}, {"center", 2, NULL},
    {"join", 1, NULL}, {"jumps", 2, NULL},
    {"global", 1, NULL}, {"vglobal", 1, NULL},
    {"normal", 4, NULL}, {"nohlsearch", 3, NULL},
    {"registers", 3, NULL}, {"redo", 3, NULL}, {"read", 1, NULL}, {"retab", 3, NULL}, {"right", 2, NULL},
    {"left", 2, NULL},
    {"k", 1, "mark"},
    {"tabnext", 4, NULL}, {"tabprevious", 4, NULL}, {"tabNext", 4, "tabprevious"},
    {"bnext", 2, "tabnext"}, {"bprevious", 2, "tabprevious"}, {"bNext", 2, "tabprevious"},
    {"file", 1, NULL}, {"help", 1, NULL}, {"tag", 2, NULL},
    {"Symbols", 3, NULL}, {"References", 3, NULL}, {"Tagbar", 6, "TagbarToggle"}, {"TagbarToggle", 7, NULL},
    {"split", 2, NULL}, {"vsplit", 2, NULL}, {"only", 2, NULL}, {"close", 3, NULL},
};

static const gchar* const COMMAND_NAMES[] = {
    "substitute", "set", "saveas", "sort", "write", "wq", "wall", "wqall", "xit", "xall",
    "quit", "qall", "edit", "enew", "update", "undo", "delete", "yank", "print", "put",
    "move", "mark", "marks", "copy", "center", "join", "jumps", "global", "vglobal",
    "normal", "nohlsearch", "registers", "redo", "read", "retab", "right", "left", "tabnext",
    "tabprevious", "file", "help", "tag", "Symbols", "References", "TagbarToggle", "split", "vsplit", "only",
    "close", NULL,
};

const gchar* const* vim_ex_command_names(void) {
    return COMMAND_NAMES;
}

static const gchar* expand_name(const gchar* typed) {
    gsize len = strlen(typed);
    for (gsize i = 0; i < G_N_ELEMENTS(EX_NAMES); i++) {
        if (strcmp(EX_NAMES[i].name, typed) == 0) {
            return EX_NAMES[i].full != NULL ? EX_NAMES[i].full : EX_NAMES[i].name;
        }
    }
    for (gsize i = 0; i < G_N_ELEMENTS(EX_NAMES); i++) {
        if ((gint)len >= EX_NAMES[i].min && g_str_has_prefix(EX_NAMES[i].name, typed)) {
            return EX_NAMES[i].full != NULL ? EX_NAMES[i].full : EX_NAMES[i].name;
        }
    }
    return NULL;
}

static const gchar* skip_blanks(const gchar* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Text up to the unescaped delimiter; "\delim" becomes delim */
static const gchar* read_delimited(const gchar* p, gchar delim, gchar** out) {
    GString* s = g_string_new(NULL);
    while (*p != '\0' && *p != delim) {
        if (*p == '\\' && p[1] != '\0') {
            if (p[1] == delim) {
                g_string_append_c(s, delim);
            } else {
                g_string_append_c(s, '\\');
                g_string_append_c(s, p[1]);
            }
            p += 2;
            continue;
        }
        g_string_append_c(s, *p);
        p++;
    }
    *out = g_string_free(s, FALSE);
    if (*p == delim) p++;
    return p;
}

static const gchar* parse_offsets(const gchar* p, VimAddr* a, gboolean* any) {
    for (;;) {
        const gchar* q = skip_blanks(p);
        if (*q == '+' || *q == '-') {
            gint sign = *q == '-' ? -1 : 1;
            q++;
            gint n = 1;
            if (g_ascii_isdigit(*q)) {
                n = 0;
                while (g_ascii_isdigit(*q)) n = n * 10 + (*q++ - '0');
            }
            a->offset += sign * n;
            *any = TRUE;
            p = q;
            continue;
        }
        if (g_ascii_isdigit(*q) && *any) {
            /* ".5" means ".+5" */
            gint n = 0;
            while (g_ascii_isdigit(*q)) n = n * 10 + (*q++ - '0');
            a->offset += n;
            p = q;
            continue;
        }
        return p;
    }
}

static const gchar* parse_address(const gchar* p, VimAddr* a, gboolean* found) {
    memset(a, 0, sizeof(*a));
    *found = FALSE;
    p = skip_blanks(p);
    if (g_ascii_isdigit(*p)) {
        a->kind = VIM_ADDR_NUMBER;
        while (g_ascii_isdigit(*p)) a->number = a->number * 10 + (*p++ - '0');
        *found = TRUE;
    } else if (*p == '.') {
        a->kind = VIM_ADDR_CURRENT;
        p++;
        *found = TRUE;
    } else if (*p == '$') {
        a->kind = VIM_ADDR_LAST;
        p++;
        *found = TRUE;
    } else if (*p == '\'' && p[1] != '\0') {
        a->kind = VIM_ADDR_MARK;
        a->mark = (gunichar)(guchar)p[1];
        p += 2;
        *found = TRUE;
    } else if (*p == '/' || *p == '?') {
        a->kind = *p == '/' ? VIM_ADDR_SEARCH_FWD : VIM_ADDR_SEARCH_BWD;
        gchar delim = *p;
        p = read_delimited(p + 1, delim, &a->pattern);
        if (a->pattern[0] == '\0') {
            g_free(a->pattern);
            a->pattern = NULL;
        }
        *found = TRUE;
    } else if (*p == '\\' && (p[1] == '/' || p[1] == '?')) {
        a->kind = p[1] == '/' ? VIM_ADDR_SEARCH_NEXT : VIM_ADDR_SEARCH_PREV;
        p += 2;
        *found = TRUE;
    } else if (*p == '+' || *p == '-') {
        a->kind = VIM_ADDR_CURRENT;
        *found = TRUE;
    }
    if (*found) p = parse_offsets(p, a, found);
    return p;
}

gboolean vim_ex_parse(const gchar* line, VimExCmd* cmd, gchar** error) {
    memset(cmd, 0, sizeof(*cmd));
    if (error != NULL) *error = NULL;
    const gchar* p = line != NULL ? line : "";
    /* Leading colons and blanks are ignored (":::s" is ":s") */
    while (*p == ':' || *p == ' ' || *p == '\t') p++;

    if (*p == '%') {
        cmd->whole = TRUE;
        cmd->naddr = 2;
        cmd->addr[0].kind = VIM_ADDR_NUMBER;
        cmd->addr[0].number = 1;
        cmd->addr[1].kind = VIM_ADDR_LAST;
        p++;
    } else if (*p == '*') {
        cmd->naddr = 2;
        cmd->addr[0].kind = VIM_ADDR_MARK;
        cmd->addr[0].mark = '<';
        cmd->addr[1].kind = VIM_ADDR_MARK;
        cmd->addr[1].mark = '>';
        p++;
    } else {
        gboolean found = FALSE;
        p = parse_address(p, &cmd->addr[0], &found);
        if (found) cmd->naddr = 1;
        for (;;) {
            const gchar* q = skip_blanks(p);
            if (*q != ',' && *q != ';') break;
            if (cmd->naddr == 0) {
                cmd->addr[0].kind = VIM_ADDR_CURRENT;
                cmd->naddr = 1;
            }
            cmd->semicolon = (*q == ';');
            if (cmd->naddr == 2) {
                g_free(cmd->addr[0].pattern);
                cmd->addr[0] = cmd->addr[1];
                memset(&cmd->addr[1], 0, sizeof(cmd->addr[1]));
            }
            VimAddr a;
            p = parse_address(q + 1, &a, &found);
            if (!found) a.kind = VIM_ADDR_CURRENT;
            cmd->addr[1] = a;
            cmd->naddr = 2;
        }
    }
    p = skip_blanks(p);

    /* Command name */
    if (*p == '\0' || *p == '|' || *p == '"') {
        cmd->name = g_strdup("");
        cmd->typed = g_strdup("");
        cmd->args = g_strdup("");
        return TRUE;
    }
    const gchar* name_start = p;
    if (*p == '&') {
        p++;
        if (*p == '&') p++;
    } else if (*p == '<' || *p == '>' || *p == '=' || *p == '!' || *p == '~' || *p == '#') {
        p++;
    } else {
        while (g_ascii_isalpha(*p)) p++;
    }
    gchar* typed = g_strndup(name_start, p - name_start);
    const gchar* full = NULL;
    if (typed[0] == '&' || typed[0] == '<' || typed[0] == '>' || typed[0] == '=' || typed[0] == '~') {
        full = typed;
    } else if (typed[0] != '\0') {
        full = expand_name(typed);
        if (full == NULL && typed[0] == 'k' && typed[1] != '\0' && typed[2] == '\0') {
            /* :ka sets mark a */
            full = "mark";
            p = name_start + 1;
            typed[1] = '\0';
        }
        if (full == NULL && typed[0] == 's' && strspn(typed + 1, "gcieInp#lr&") == strlen(typed + 1)) {
            /* :sg, :si... are :s with flags */
            full = "substitute";
            p = name_start + 1;
            typed[1] = '\0';
        }
    }
    if (full == NULL) {
        if (error != NULL) {
            *error = g_strdup_printf("E492: Not an editor command: %s", skip_blanks(line != NULL ? line : ""));
        }
        g_free(typed);
        vim_ex_cmd_clear(cmd);
        return FALSE;
    }
    cmd->name = g_strdup(full);
    cmd->typed = typed;
    if (*p == '!' && strcmp(cmd->name, "!") != 0) {
        cmd->bang = TRUE;
        p++;
    }
    p = skip_blanks(p);
    cmd->args = g_strdup(p);
    /* Trailing blanks are not part of the arguments (except for :normal) */
    if (strcmp(cmd->name, "normal") != 0) g_strchomp(cmd->args);
    return TRUE;
}

void vim_ex_cmd_clear(VimExCmd* cmd) {
    g_free(cmd->addr[0].pattern);
    g_free(cmd->addr[1].pattern);
    g_free(cmd->name);
    g_free(cmd->typed);
    g_free(cmd->args);
    memset(cmd, 0, sizeof(*cmd));
}

gboolean vim_ex_split_pattern(const gchar* args, gchar** pattern, gchar** replacement,
                              gchar** rest, gboolean with_replacement) {
    if (pattern) *pattern = NULL;
    if (replacement) *replacement = NULL;
    if (rest) *rest = NULL;
    const gchar* p = args != NULL ? args : "";
    gchar delim = *p;
    if (delim == '\0' || g_ascii_isalnum(delim) || delim == ' ' || delim == '\\' ||
        delim == '"' || delim == '|') {
        if (rest) *rest = g_strdup(p);
        return FALSE;
    }
    gchar* pat = NULL;
    p = read_delimited(p + 1, delim, &pat);
    gchar* rep = NULL;
    if (with_replacement) {
        /* The replacement keeps its backslashes (\1, \r, \&...) except before delim */
        p = read_delimited(p, delim, &rep);
    }
    if (pattern) *pattern = pat; else g_free(pat);
    if (replacement) *replacement = rep; else g_free(rep);
    if (rest) *rest = g_strdup(p);
    return TRUE;
}
