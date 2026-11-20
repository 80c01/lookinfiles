/* Side-by-side line diff with UTF-8 intra-line spans (see side_diff.h). */
#include "formats/side_diff.h"

#include <string.h>

#define SIDE_DIFF_MAX_EDITS 2000

typedef struct {
    const gchar* s;
    gsize len;
    guint hash;
} DiffLine;

static void row_free(gpointer data) {
    SideDiffRow* r = data;
    if (r == NULL) return;
    g_free(r->left_text);
    g_free(r->right_text);
    if (r->left_spans != NULL) g_array_unref(r->left_spans);
    if (r->right_spans != NULL) g_array_unref(r->right_spans);
    g_free(r);
}

void side_diff_free(SideDiffDoc* doc) {
    if (doc == NULL) return;
    if (doc->rows != NULL) g_ptr_array_unref(doc->rows);
    if (doc->hunks != NULL) g_array_unref(doc->hunks);
    g_free(doc);
}

static DiffLine* split_lines(const gchar* text, gsize len, guint* count, gboolean* truncated) {
    if (text == NULL || len == 0) {
        *count = 0;
        return NULL;
    }
    guint cap = 64;
    DiffLine* lines = g_new(DiffLine, cap);
    guint n = 0;
    gsize start = 0;
    for (gsize i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            /* A trailing newline ends the last line without adding an empty one */
            if (i == len && start == len) break;
            if (n >= SIDE_DIFF_MAX_LINES) {
                *truncated = TRUE;
                break;
            }
            if (n == cap) {
                cap *= 2;
                lines = g_renew(DiffLine, lines, cap);
            }
            gsize llen = i - start;
            if (llen > 0 && text[start + llen - 1] == '\r') {
                llen--;
            }
            guint h = 5381;
            for (gsize j = 0; j < llen; j++) {
                h = h * 33 + (guchar)text[start + j];
            }
            lines[n].s = text + start;
            lines[n].len = llen;
            lines[n].hash = h;
            n++;
            start = i + 1;
        }
    }
    *count = n;
    return lines;
}

static gboolean line_eq(const DiffLine* a, const DiffLine* b) {
    return a->hash == b->hash && a->len == b->len &&
           (a->len == 0 || memcmp(a->s, b->s, a->len) == 0);
}

static gchar* dup_valid_utf8(const gchar* s, gsize len) {
    if (s == NULL || len == 0) return g_strdup("");
    if (g_utf8_validate(s, (gssize)len, NULL)) {
        return g_strndup(s, len);
    }
    return g_utf8_make_valid(s, (gssize)len);
}

/* Pinpoint the differing middle span of two modified UTF-8 lines when they
 * share a common prefix or suffix (avoids noisy spans on totally unrelated lines). */
static void compute_inline_spans(SideDiffRow* r) {
    const gchar* a = r->left_text;
    const gchar* b = r->right_text;
    gsize la = strlen(a);
    gsize lb = strlen(b);
    if (la == 0 && lb == 0) return;

    /* Common UTF-8 prefix */
    const gchar* pa = a;
    const gchar* pb = b;
    while (*pa != '\0' && *pb != '\0') {
        const gchar* na = g_utf8_next_char(pa);
        const gchar* nb = g_utf8_next_char(pb);
        gsize ca = (gsize)(na - pa);
        gsize cb = (gsize)(nb - pb);
        if (ca != cb || memcmp(pa, pb, ca) != 0) break;
        pa = na;
        pb = nb;
    }
    gsize pre = (gsize)(pa - a);

    /* Common UTF-8 suffix (not overlapping the prefix) */
    const gchar* ea = a + la;
    const gchar* eb = b + lb;
    while (ea > a + pre && eb > b + pre) {
        const gchar* qa = g_utf8_prev_char(ea);
        const gchar* qb = g_utf8_prev_char(eb);
        gsize ca = (gsize)(ea - qa);
        gsize cb = (gsize)(eb - qb);
        if (ca != cb || memcmp(qa, qb, ca) != 0) break;
        ea = qa;
        eb = qb;
    }
    gsize suf = (gsize)((a + la) - ea);

    if (pre == 0 && suf == 0) {
        return; /* whole line changed: background highlight alone is cleaner */
    }
    if (pre + suf < la) {
        r->left_spans = g_array_new(FALSE, FALSE, sizeof(SideDiffSpan));
        SideDiffSpan sp = {(guint)pre, (guint)(la - suf)};
        g_array_append_val(r->left_spans, sp);
    }
    if (pre + suf < lb) {
        r->right_spans = g_array_new(FALSE, FALSE, sizeof(SideDiffSpan));
        SideDiffSpan sp = {(guint)pre, (guint)(lb - suf)};
        g_array_append_val(r->right_spans, sp);
    }
}

static void append_row(SideDiffDoc* doc, SideDiffKind kind,
                       gint left_line, const DiffLine* left,
                       gint right_line, const DiffLine* right) {
    SideDiffRow* r = g_new0(SideDiffRow, 1);
    r->kind = kind;
    r->left_line = left_line;
    r->right_line = right_line;
    r->left_text = left != NULL ? dup_valid_utf8(left->s, left->len) : g_strdup("");
    r->right_text = right != NULL ? dup_valid_utf8(right->s, right->len) : g_strdup("");
    if (kind == SIDE_DIFF_MODIFIED) {
        compute_inline_spans(r);
    }
    g_ptr_array_add(doc->rows, r);
}

/* Emit a contiguous change hunk: dels lines from a[ai..ai+dels) against
 * adds lines from b[bi..bi+adds). Numbers are 1-based using base_a / base_b. */
static void append_hunk(SideDiffDoc* doc,
                        const DiffLine* a, guint ai, guint dels, guint base_a,
                        const DiffLine* b, guint bi, guint adds, guint base_b) {
    if (dels == 0 && adds == 0) return;
    guint hunk_row = doc->rows->len;
    g_array_append_val(doc->hunks, hunk_row);

    guint pairs = MIN(dels, adds);
    for (guint i = 0; i < pairs; i++) {
        append_row(doc, SIDE_DIFF_MODIFIED,
                   (gint)(base_a + ai + i + 1), &a[ai + i],
                   (gint)(base_b + bi + i + 1), &b[bi + i]);
    }
    for (guint i = pairs; i < dels; i++) {
        append_row(doc, SIDE_DIFF_REMOVED,
                   (gint)(base_a + ai + i + 1), &a[ai + i],
                   0, NULL);
    }
    for (guint i = pairs; i < adds; i++) {
        append_row(doc, SIDE_DIFF_ADDED,
                   0, NULL,
                   (gint)(base_b + bi + i + 1), &b[bi + i]);
    }
    doc->modified += pairs;
    doc->removed += dels;
    doc->added += adds;
}

/* Myers O(ND) diff of a[0..n) vs b[0..m). Fills ins[j] (1 if b[j] inserted)
 * and del_at[j] (count of a lines deleted before b[j], j in 0..m).
 * Returns FALSE when the edit budget SIDE_DIFF_MAX_EDITS is exceeded. */
static gboolean myers(const DiffLine* a, guint n, const DiffLine* b, guint m,
                      guint8* ins, guint* del_at) {
    gint max = (gint)(n + m);
    gint dmax = MIN(max, SIDE_DIFF_MAX_EDITS);
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
            while (x < (gint)n && y < (gint)m && line_eq(&a[x], &b[y])) {
                x++;
                y++;
            }
            v[off + k] = x;
            if (x >= (gint)n && y >= (gint)m) {
                found = d;
                break;
            }
        }
        gint* snap = g_new(gint, 2 * (gsize)d + 1);
        memcpy(snap, v + off - d, sizeof(gint) * (2 * (gsize)d + 1));
        g_ptr_array_add(trace, snap);
    }

    if (found >= 0) {
        gint x = (gint)n;
        gint y = (gint)m;
        for (gint d = found; d > 0; d--) {
            const gint* pv = g_ptr_array_index(trace, d - 1);
            gint k = x - y;
            gboolean down = (k == -d || (k != d && pv[k - 1 + d - 1] < pv[k + 1 + d - 1]));
            gint pk = down ? k + 1 : k - 1;
            gint px = pv[pk + d - 1];
            gint py = px - pk;
            if (down) {
                ins[py] = 1;
            } else {
                del_at[py]++;
            }
            x = px;
            y = py;
        }
    }

    g_ptr_array_unref(trace);
    g_free(v);
    return found >= 0;
}

SideDiffDoc* side_diff_compute(const gchar* left, gsize left_len,
                               const gchar* right, gsize right_len) {
    SideDiffDoc* doc = g_new0(SideDiffDoc, 1);
    doc->rows = g_ptr_array_new_with_free_func(row_free);
    doc->hunks = g_array_new(FALSE, FALSE, sizeof(guint));

    guint n = 0, m = 0;
    DiffLine* a = split_lines(left, left_len, &n, &doc->truncated);
    DiffLine* b = split_lines(right, right_len, &m, &doc->truncated);

    /* Common prefix & suffix trimmed before running Myers */
    guint pre = 0;
    while (pre < n && pre < m && line_eq(&a[pre], &b[pre])) {
        pre++;
    }
    guint suf = 0;
    while (suf < n - pre && suf < m - pre && line_eq(&a[n - 1 - suf], &b[m - 1 - suf])) {
        suf++;
    }

    for (guint i = 0; i < pre; i++) {
        append_row(doc, SIDE_DIFF_EQUAL, (gint)(i + 1), &a[i], (gint)(i + 1), &b[i]);
    }

    guint mn = n - pre - suf;
    guint mm = m - pre - suf;
    if (mn > 0 || mm > 0) {
        const DiffLine* ma = a + pre;
        const DiffLine* mb = b + pre;
        guint8* ins = g_new0(guint8, mm + 1);
        guint* del_at = g_new0(guint, mm + 1);

        if (!myers(ma, mn, mb, mm, ins, del_at)) {
            append_hunk(doc, ma, 0, mn, pre, mb, 0, mm, pre);
        } else {
            guint ai = 0;
            guint j = 0;
            while (j <= mm) {
                if (del_at[j] > 0 || (j < mm && ins[j])) {
                    guint hunk_ai = ai;
                    guint hunk_bi = j;
                    guint dels = 0;
                    guint adds = 0;
                    for (;;) {
                        dels += del_at[j];
                        ai += del_at[j];
                        if (j < mm && ins[j]) {
                            adds++;
                            j++;
                        } else {
                            break;
                        }
                    }
                    append_hunk(doc, ma, hunk_ai, dels, pre, mb, hunk_bi, adds, pre);
                    /* Now at j, del_at[j] was already consumed if we ended on a non-inserted line */
                    if (j < mm) {
                        append_row(doc, SIDE_DIFF_EQUAL,
                                   (gint)(pre + ai + 1), &ma[ai],
                                   (gint)(pre + j + 1), &mb[j]);
                        ai++;
                    }
                    j++;
                } else {
                    if (j < mm) {
                        append_row(doc, SIDE_DIFF_EQUAL,
                                   (gint)(pre + ai + 1), &ma[ai],
                                   (gint)(pre + j + 1), &mb[j]);
                        ai++;
                    }
                    j++;
                }
            }
        }
        g_free(ins);
        g_free(del_at);
    }

    for (guint i = 0; i < suf; i++) {
        guint la = n - suf + i;
        guint lb = m - suf + i;
        append_row(doc, SIDE_DIFF_EQUAL, (gint)(la + 1), &a[la], (gint)(lb + 1), &b[lb]);
    }

    g_free(a);
    g_free(b);
    return doc;
}
