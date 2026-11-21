/* The filter of the process list (see proc_query.h) */
#include "core/proc_query.h"
#include <string.h>

typedef enum {
    FIELD_CPU,
    FIELD_MEM,
    FIELD_THREADS,
} Field;

typedef enum {
    OP_GT,
    OP_GE,
    OP_LT,
    OP_LE,
} Op;

typedef struct {
    Field field;
    Op op;
    gdouble value; /* percent, bytes or threads */
} Condition;

struct ProcQuery {
    GPtrArray* words; /* casefolded */
    GArray* conds;    /* Condition */
};

static const struct {
    const gchar* name;
    Field field;
} FIELDS[] = {
    {"cpu", FIELD_CPU},
    {"mem", FIELD_MEM},
    {"threads", FIELD_THREADS},
};

/* Multiplier of a memory unit (the rest of the token, casefolded), 0 when
 * unknown: K, M, G, T, then an optional B or o; no unit at all is MB */
static gdouble mem_unit(const gchar* unit) {
    if (*unit == '\0') return 1e6;
    gdouble mult = 1.0;
    switch (*unit) {
    case 'k': mult = 1e3; break;
    case 'm': mult = 1e6; break;
    case 'g': mult = 1e9; break;
    case 't': mult = 1e12; break;
    default: break;
    }
    if (mult != 1.0) unit++;
    if (*unit == 'b' || *unit == 'o') unit++;
    return *unit == '\0' ? mult : 0.0;
}

typedef enum {
    TOKEN_WORD,      /* not a condition */
    TOKEN_CONDITION, /* *cond filled */
    TOKEN_PARTIAL,   /* a condition being typed: left out */
} TokenKind;

/* Read a token (casefolded) as a condition */
static TokenKind parse_condition(const gchar* token, Condition* cond) {
    const gchar* p = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(FIELDS) && p == NULL; i++) {
        gsize len = strlen(FIELDS[i].name);
        if (strncmp(token, FIELDS[i].name, len) == 0 && (token[len] == '<' || token[len] == '>')) {
            cond->field = FIELDS[i].field;
            p = token + len;
        }
    }
    if (p == NULL) return TOKEN_WORD;
    gboolean greater = *p == '>';
    p++;
    gboolean equal = *p == '=';
    if (equal) p++;
    cond->op = greater ? (equal ? OP_GE : OP_GT) : (equal ? OP_LE : OP_LT);

    /* The number: digits, one decimal point or comma */
    gchar* num = g_strdup(p);
    gsize n = 0;
    gboolean point = FALSE;
    for (; num[n] != '\0'; n++) {
        if (num[n] == ',') num[n] = '.';
        if (num[n] == '.' && !point) {
            point = TRUE;
        } else if (!g_ascii_isdigit(num[n])) {
            break;
        }
    }
    const gchar* rest = p + n;
    num[n] = '\0';
    gboolean digits = strpbrk(num, "0123456789") != NULL;
    gdouble value = digits ? g_ascii_strtod(num, NULL) : 0.0;
    g_free(num);
    if (!digits) return TOKEN_PARTIAL;

    switch (cond->field) {
    case FIELD_CPU:
        if (*rest == '%') rest++;
        if (*rest != '\0') return TOKEN_PARTIAL;
        break;
    case FIELD_MEM: {
        gdouble mult = mem_unit(rest);
        if (mult == 0.0) return TOKEN_PARTIAL;
        value *= mult;
        break;
    }
    case FIELD_THREADS:
        if (*rest != '\0') return TOKEN_PARTIAL;
        break;
    }
    cond->value = value;
    return TOKEN_CONDITION;
}

ProcQuery* proc_query_parse(const gchar* text) {
    if (text == NULL) return NULL;
    gchar* folded = g_utf8_casefold(text, -1);
    gchar** tokens = g_strsplit_set(folded, " \t\n", -1);
    g_free(folded);
    ProcQuery* q = g_new0(ProcQuery, 1);
    q->words = g_ptr_array_new_with_free_func(g_free);
    q->conds = g_array_new(FALSE, FALSE, sizeof(Condition));
    for (gchar** t = tokens; *t != NULL; t++) {
        if ((*t)[0] == '\0') continue;
        Condition cond;
        switch (parse_condition(*t, &cond)) {
        case TOKEN_WORD: g_ptr_array_add(q->words, g_strdup(*t)); break;
        case TOKEN_CONDITION: g_array_append_val(q->conds, cond); break;
        case TOKEN_PARTIAL: break;
        }
    }
    g_strfreev(tokens);
    if (q->words->len == 0 && q->conds->len == 0) {
        proc_query_free(q);
        return NULL;
    }
    return q;
}

void proc_query_free(ProcQuery* q) {
    if (q == NULL) return;
    g_ptr_array_unref(q->words);
    g_array_unref(q->conds);
    g_free(q);
}

gboolean proc_query_has_conditions(const ProcQuery* q) {
    return q != NULL && q->conds->len > 0;
}

gboolean proc_query_match_words(const ProcQuery* q, const ProcQueryRow* row) {
    if (q == NULL || q->words->len == 0) return TRUE;
    gchar* text = g_strdup_printf("%s %s %d", row->name != NULL ? row->name : "", row->user != NULL ? row->user : "",
                                  row->pid);
    gchar* folded = g_utf8_casefold(text, -1);
    gboolean all = TRUE;
    for (guint i = 0; i < q->words->len && all; i++) all = strstr(folded, g_ptr_array_index(q->words, i)) != NULL;
    g_free(folded);
    g_free(text);
    return all;
}

gboolean proc_query_match_values(const ProcQuery* q, const ProcQueryRow* row) {
    if (q == NULL) return TRUE;
    for (guint i = 0; i < q->conds->len; i++) {
        const Condition* c = &g_array_index(q->conds, Condition, i);
        gdouble v = c->field == FIELD_CPU ? row->cpu : c->field == FIELD_MEM ? (gdouble)row->rss : (gdouble)row->threads;
        gboolean ok = FALSE;
        switch (c->op) {
        case OP_GT: ok = v > c->value; break;
        case OP_GE: ok = v >= c->value; break;
        case OP_LT: ok = v < c->value; break;
        case OP_LE: ok = v <= c->value; break;
        }
        if (!ok) return FALSE;
    }
    return TRUE;
}
