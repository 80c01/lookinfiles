/* Line diff of two texts (Myers' O(ND) algorithm) reduced to per-line marks,
 * as shown in an editor gutter: added, modified, deleted above / below. */
#include "line_diff.h"

#include <string.h>

/* Edit distance budget: beyond it the changed region is marked as a whole
 * (keeps the trace memory, O(D^2), small). */
#define LINE_DIFF_MAX_EDITS 1500

typedef struct {
    const gchar* s;
    gsize len;
    guint hash;
} Line;

guint line_diff_count_lines(const gchar* text, gsize len) {
    if (text == NULL || len == 0) {
        return 0;
    }
    guint n = 0;
    for (gsize i = 0; i < len; i++) {
        if (text[i] == '\n') {
            n++;
        }
    }
    return (text[len - 1] == '\n') ? n : n + 1;
}

static Line* split_lines(const gchar* text, gsize len, guint* count) {
    *count = line_diff_count_lines(text, len);
    Line* lines = g_new(Line, MAX(*count, 1));
    gsize start = 0;
    guint n = 0;
    for (gsize i = 0; i <= len && n < *count; i++) {
        if (i == len || text[i] == '\n') {
            Line* l = &lines[n++];
            l->s = text + start;
            l->len = i - start;
            guint h = 5381;
            for (gsize j = 0; j < l->len; j++) {
                h = h * 33 + (guchar)l->s[j];
            }
            l->hash = h;
            start = i + 1;
        }
    }
    return lines;
}

static gboolean line_equal(const Line* a, const Line* b) {
    return a->hash == b->hash && a->len == b->len && memcmp(a->s, b->s, a->len) == 0;
}

/* Mark one change: adds new lines at new index pos (in marks), replacing dels old lines */
static void mark_hunk(guint8* marks, guint n_new, guint pos, guint adds, guint dels) {
    if (adds > 0) {
        guint8 m = (dels > 0) ? LINE_DIFF_MODIFIED : LINE_DIFF_ADDED;
        for (guint i = pos; i < pos + adds && i < n_new; i++) {
            marks[i] |= m;
        }
    } else if (dels > 0) {
        if (pos < n_new) {
            marks[pos] |= LINE_DIFF_DELETED_ABOVE;
        } else if (n_new > 0) {
            marks[n_new - 1] |= LINE_DIFF_DELETED_BELOW;
        }
    }
}

/* Myers diff of a[0..n) against b[0..m). Fills ins[j] (new line j inserted)
 * and del_at[j] (old lines deleted just before new line j, j <= m).
 * Returns FALSE when the edit budget is exceeded. */
static gboolean myers(const Line* a, guint n, const Line* b, guint m,
                      guint8* ins, guint* del_at) {
    gint max = (gint)(n + m);
    gint dmax = MIN(max, LINE_DIFF_MAX_EDITS);
    gint* v = g_new0(gint, 2 * (gsize)max + 3);
    gint off = max + 1;
    GPtrArray* trace = g_ptr_array_new_with_free_func(g_free);
    gint found = -1;

    for (gint d = 0; d <= dmax && found < 0; d++) {
        for (gint k = -d; k <= d; k += 2) {
            gint x = (k == -d || (k != d && v[off + k - 1] < v[off + k + 1]))
                         ? v[off + k + 1]
                         : v[off + k - 1] + 1;
            gint y = x - k;
            while (x < (gint)n && y < (gint)m && line_equal(&a[x], &b[y])) {
                x++;
                y++;
            }
            v[off + k] = x;
            if (x >= (gint)n && y >= (gint)m) {
                found = d;
                break;
            }
        }
        /* Snapshot of v[-d..d] after step d */
        gint* snap = g_new(gint, 2 * (gsize)d + 1);
        memcpy(snap, v + off - d, sizeof(gint) * (2 * (gsize)d + 1));
        g_ptr_array_add(trace, snap);
    }

    if (found >= 0) {
        gint x = (gint)n;
        gint y = (gint)m;
        for (gint d = found; d > 0; d--) {
            const gint* pv = g_ptr_array_index(trace, d - 1); /* index k -> pv[k + d - 1] */
            gint k = x - y;
            gboolean down = (k == -d || (k != d && pv[k - 1 + d - 1] < pv[k + 1 + d - 1]));
            gint pk = down ? k + 1 : k - 1;
            gint px = pv[pk + d - 1];
            gint py = px - pk;
            if (down) {
                ins[py] = 1; /* new line py inserted */
            } else {
                del_at[py]++; /* old line px deleted before new line py */
            }
            x = px;
            y = py;
        }
    }
    g_ptr_array_free(trace, TRUE);
    g_free(v);
    return found >= 0;
}

guint8* line_diff_marks(const gchar* old_text, gsize old_len,
                        const gchar* new_text, gsize new_len, guint* n_lines) {
    guint n_old = 0;
    guint n_new = 0;
    Line* a = split_lines(old_text != NULL ? old_text : "", old_text != NULL ? old_len : 0, &n_old);
    Line* b = split_lines(new_text != NULL ? new_text : "", new_text != NULL ? new_len : 0, &n_new);
    *n_lines = n_new;
    if (n_new == 0) {
        g_free(a);
        g_free(b);
        return NULL;
    }
    guint8* marks = g_new0(guint8, n_new);

    /* Common prefix and suffix are unchanged */
    guint pre = 0;
    while (pre < n_old && pre < n_new && line_equal(&a[pre], &b[pre])) {
        pre++;
    }
    guint suf = 0;
    while (suf < n_old - pre && suf < n_new - pre &&
           line_equal(&a[n_old - 1 - suf], &b[n_new - 1 - suf])) {
        suf++;
    }
    guint n = n_old - pre - suf;
    guint m = n_new - pre - suf;

    if (n > 0 || m > 0) {
        guint8* ins = g_new0(guint8, MAX(m, 1));
        guint* del_at = g_new0(guint, m + 1);
        if (!myers(a + pre, n, b + pre, m, ins, del_at)) {
            mark_hunk(marks, n_new, pre, m, n); /* too different: one block */
        } else {
            /* Group adjacent insertions and deletions into hunks */
            guint j = 0;
            while (j <= m) {
                guint start = j;
                guint dels = del_at[j];
                while (j < m && ins[j]) {
                    j++;
                    dels += del_at[j];
                }
                mark_hunk(marks, n_new, pre + start, j - start, dels);
                j++;
            }
        }
        g_free(ins);
        g_free(del_at);
    }
    g_free(a);
    g_free(b);
    return marks;
}
