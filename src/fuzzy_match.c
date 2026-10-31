/* Fuzzy matching of file paths (see fuzzy_match.h): a small dynamic
 * programming alignment in the spirit of fzy. */
#include "fuzzy_match.h"

#include <string.h>

#define MAX_QUERY 64
#define MAX_PATH 1024
#define NEG (G_MININT / 4)

#define SCORE_MATCH 16
#define BONUS_SLASH 40      /* first character of a path component */
#define BONUS_WORD 30       /* after _ - . or a space */
#define BONUS_CAMEL 25      /* lower-to-upper case change */
#define BONUS_BASENAME 20   /* inside the file name */
#define BONUS_CONSECUTIVE 40
#define GAP_PENALTY 1

typedef struct {
    gchar q[MAX_QUERY + 1];
    gint n;
    gboolean exact_case;
    const gchar* p;
    gint m;
    gint base;   /* offset of the file name in p */
    gint* match; /* n * m: best score with q[i] matched at p[j] */
    gint* best;  /* n * m: best score with q[0..i] matched in p[0..j] */
} Fuzzy;

/* Query without spaces; exact case when it has a capital letter */
static gboolean prepare_query(Fuzzy* f, const gchar* query) {
    f->n = 0;
    f->exact_case = FALSE;
    for (const gchar* c = query; *c != '\0'; c++) {
        if (*c == ' ') continue;
        if (f->n >= MAX_QUERY) return FALSE;
        if (g_ascii_isupper(*c)) f->exact_case = TRUE;
        f->q[f->n++] = *c;
    }
    f->q[f->n] = '\0';
    return TRUE;
}

static gboolean same_char(const Fuzzy* f, gchar a, gchar b) {
    return f->exact_case ? a == b : g_ascii_tolower(a) == g_ascii_tolower(b);
}

static gint bonus_at(const Fuzzy* f, gint j) {
    gint bonus = j >= f->base ? BONUS_BASENAME : 0;
    if (j == 0) return bonus + BONUS_SLASH;
    gchar prev = f->p[j - 1];
    gchar c = f->p[j];
    if (prev == '/') return bonus + BONUS_SLASH;
    if (prev == '_' || prev == '-' || prev == '.' || prev == ' ') return bonus + BONUS_WORD;
    if (g_ascii_islower(prev) && g_ascii_isupper(c)) return bonus + BONUS_CAMEL;
    return bonus;
}

/* The query's characters appear in order */
static gboolean is_subsequence(const Fuzzy* f) {
    gint i = 0;
    for (gint j = 0; j < f->m && i < f->n; j++) {
        if (same_char(f, f->q[i], f->p[j])) i++;
    }
    return i == f->n;
}

/* Fill the tables; FALSE when the path does not match */
static gboolean align(Fuzzy* f, const gchar* query, const gchar* path) {
    f->match = NULL;
    f->best = NULL;
    if (query == NULL || path == NULL || !prepare_query(f, query)) return FALSE;
    f->p = path;
    f->m = (gint)strlen(path);
    if (f->m > MAX_PATH) return FALSE;
    const gchar* slash = strrchr(path, '/');
    f->base = slash != NULL ? (gint)(slash - path) + 1 : 0;
    if (f->n == 0 || f->n > f->m || !is_subsequence(f)) return f->n == 0;

    gint n = f->n;
    gint m = f->m;
    f->match = g_new(gint, (gsize)n * (gsize)m);
    f->best = g_new(gint, (gsize)n * (gsize)m);
    for (gint i = 0; i < n; i++) {
        gint* mrow = f->match + (gsize)i * (gsize)m;
        gint* brow = f->best + (gsize)i * (gsize)m;
        const gint* mprev = i > 0 ? f->match + (gsize)(i - 1) * (gsize)m : NULL;
        const gint* bprev = i > 0 ? f->best + (gsize)(i - 1) * (gsize)m : NULL;
        for (gint j = 0; j < m; j++) {
            gint score = NEG;
            if (same_char(f, f->q[i], f->p[j])) {
                if (i == 0) {
                    score = SCORE_MATCH + bonus_at(f, j);
                } else if (j > 0) {
                    gint from = MAX(bprev[j - 1], mprev[j - 1] > NEG ? mprev[j - 1] + BONUS_CONSECUTIVE : NEG);
                    if (from > NEG) score = from + SCORE_MATCH + bonus_at(f, j);
                }
            }
            mrow[j] = score;
            /* No penalty before the first match: only gaps between matches count */
            gint carried = j > 0 && brow[j - 1] > NEG ? brow[j - 1] - (i == 0 ? 0 : GAP_PENALTY) : NEG;
            brow[j] = MAX(score, carried);
        }
    }
    return TRUE;
}

static void fuzzy_clear(Fuzzy* f) {
    g_free(f->match);
    g_free(f->best);
}

gint fuzzy_match_score(const gchar* query, const gchar* path) {
    Fuzzy f;
    if (!align(&f, query, path)) {
        fuzzy_clear(&f);
        return -1;
    }
    /* Shorter paths first among equal matches */
    gint length_penalty = f.m / 4;
    gint score;
    if (f.n == 0) {
        score = MAX_PATH - f.m;
    } else {
        gint last = f.best[(gsize)(f.n - 1) * (gsize)f.m + (gsize)(f.m - 1)];
        score = MAX(last - length_penalty + MAX_PATH, 0);
    }
    fuzzy_clear(&f);
    return score;
}

gsize* fuzzy_match_positions(const gchar* query, const gchar* path) {
    Fuzzy f;
    if (!align(&f, query, path) || f.n == 0) {
        fuzzy_clear(&f);
        return NULL;
    }
    gsize* pos = g_new(gsize, (gsize)f.n + 1);
    pos[f.n] = G_MAXSIZE;
    gint limit = f.m - 1;
    gboolean consecutive = FALSE;
    for (gint i = f.n - 1; i >= 0; i--) {
        const gint* mrow = f.match + (gsize)i * (gsize)f.m;
        gint chosen = -1;
        if (consecutive && limit >= 0 && mrow[limit] > NEG) {
            chosen = limit;
        } else {
            gint top = NEG;
            for (gint j = 0; j <= limit; j++) {
                if (mrow[j] <= NEG) continue;
                gint v = mrow[j] - GAP_PENALTY * (limit - j);
                if (v >= top) {
                    top = v;
                    chosen = j;
                }
            }
        }
        if (chosen < 0) {
            g_free(pos);
            fuzzy_clear(&f);
            return NULL;
        }
        pos[i] = (gsize)chosen;
        /* Did this match come from the previous character matched just before? */
        consecutive = FALSE;
        if (i > 0 && chosen > 0) {
            const gint* mprev = f.match + (gsize)(i - 1) * (gsize)f.m;
            const gint* bprev = f.best + (gsize)(i - 1) * (gsize)f.m;
            consecutive = mprev[chosen - 1] > NEG &&
                          mprev[chosen - 1] + BONUS_CONSECUTIVE >= bprev[chosen - 1];
        }
        limit = chosen - 1;
    }
    fuzzy_clear(&f);
    return pos;
}
