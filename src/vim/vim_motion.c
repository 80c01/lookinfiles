/* Vim motions and text objects on GtkTextIter */
#include "vim/vim_motion.h"
#include <string.h>

gint vim_char_class(gunichar c, gboolean big) {
    if (c == ' ' || c == '\t' || c == 0 || c == '\n' || c == '\r' || c == 0xA0) return 0;
    if (big) return 2;
    if (c == '_' || g_unichar_isalnum(c)) return 2;
    if (c >= 0x100 && !g_unichar_ispunct(c) && !g_unichar_isspace(c)) return 2;
    return 1;
}

static gboolean is_blank(gunichar c) {
    return c == ' ' || c == '\t' || c == 0xA0;
}

/* On a newline character (not the buffer end) */
static gboolean on_newline(const GtkTextIter* it) {
    return gtk_text_iter_ends_line(it) && !gtk_text_iter_is_end(it);
}

static gboolean on_empty_line(const GtkTextIter* it) {
    return gtk_text_iter_starts_line(it) && gtk_text_iter_ends_line(it);
}

static gint char_width(gunichar c, gint vcol, gint tabstop) {
    if (c == '\t') return tabstop - (vcol % tabstop);
    return g_unichar_iswide(c) ? 2 : 1;
}

gint vim_iter_vcol(const GtkTextIter* iter, gint tabstop) {
    if (tabstop <= 0) tabstop = 8;
    GtkTextIter p = *iter;
    gtk_text_iter_set_line_offset(&p, 0);
    gint v = 0;
    while (gtk_text_iter_compare(&p, iter) < 0) {
        v += char_width(gtk_text_iter_get_char(&p), v, tabstop);
        gtk_text_iter_forward_char(&p);
    }
    return v;
}

gint vim_iter_vcol_end(const GtkTextIter* iter, gint tabstop) {
    if (tabstop <= 0) tabstop = 8;
    gint v = vim_iter_vcol(iter, tabstop);
    if (gtk_text_iter_ends_line(iter)) return v + 1;
    return v + char_width(gtk_text_iter_get_char(iter), v, tabstop);
}

void vim_iter_set_vcol(GtkTextIter* iter, gint vcol, gint tabstop) {
    if (tabstop <= 0) tabstop = 8;
    gtk_text_iter_set_line_offset(iter, 0);
    gint v = 0;
    while (!gtk_text_iter_ends_line(iter)) {
        gint w = char_width(gtk_text_iter_get_char(iter), v, tabstop);
        if (v + w > vcol) break;
        v += w;
        gtk_text_iter_forward_char(iter);
    }
}

void vim_iter_line_start(GtkTextIter* iter) {
    gtk_text_iter_set_line_offset(iter, 0);
}

void vim_iter_line_end(GtkTextIter* iter) {
    if (!gtk_text_iter_ends_line(iter)) gtk_text_iter_forward_to_line_end(iter);
}

void vim_iter_first_nonblank(GtkTextIter* iter) {
    gtk_text_iter_set_line_offset(iter, 0);
    while (!gtk_text_iter_ends_line(iter) && is_blank(gtk_text_iter_get_char(iter))) {
        gtk_text_iter_forward_char(iter);
    }
}

void vim_iter_clamp_normal(GtkTextIter* iter) {
    if (gtk_text_iter_ends_line(iter) && !gtk_text_iter_starts_line(iter)) {
        gtk_text_iter_backward_char(iter);
    }
}

gboolean vim_iter_at_line_end(const GtkTextIter* iter) {
    return gtk_text_iter_ends_line(iter);
}

gboolean vim_line_is_empty(GtkTextBuffer* buf, gint line) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buf, &it, line);
    return gtk_text_iter_ends_line(&it);
}

gboolean vim_line_is_blank(GtkTextBuffer* buf, gint line) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buf, &it, line);
    vim_iter_first_nonblank(&it);
    return gtk_text_iter_ends_line(&it);
}

gint vim_line_indent(GtkTextBuffer* buf, gint line, gint tabstop) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buf, &it, line);
    vim_iter_first_nonblank(&it);
    return vim_iter_vcol(&it, tabstop);
}

/* ------------------------------------------------------------------------- */
/* Words                                                                     */
/* ------------------------------------------------------------------------- */

static gint cls_at(const GtkTextIter* it, gboolean big) {
    if (gtk_text_iter_ends_line(it)) return 0;
    return vim_char_class(gtk_text_iter_get_char(it), big);
}

gboolean vim_motion_word_forward(GtkTextIter* iter, gint count, gboolean big, gboolean for_operator) {
    gboolean moved = FALSE;
    if (count < 1) count = 1;
    for (gint i = 0; i < count; i++) {
        GtkTextIter before = *iter;
        if (gtk_text_iter_is_end(iter)) break;
        gint start_line = gtk_text_iter_get_line(iter);
        gint sc = cls_at(iter, big);
        if (sc != 0) {
            while (!gtk_text_iter_ends_line(iter) && cls_at(iter, big) == sc) {
                gtk_text_iter_forward_char(iter);
            }
        }
        gboolean last = (i == count - 1);
        while (!gtk_text_iter_is_end(iter)) {
            if (gtk_text_iter_ends_line(iter)) {
                if (for_operator && last && gtk_text_iter_get_line(iter) == start_line &&
                    !gtk_text_iter_equal(iter, &before)) {
                    break; /* dw on the last word of a line keeps the newline */
                }
                gtk_text_iter_forward_char(iter);
                if (on_empty_line(iter)) break;
                continue;
            }
            if (is_blank(gtk_text_iter_get_char(iter))) {
                gtk_text_iter_forward_char(iter);
                continue;
            }
            break;
        }
        if (gtk_text_iter_equal(iter, &before)) break;
        moved = TRUE;
    }
    return moved;
}

gboolean vim_motion_word_end(GtkTextIter* iter, gint count, gboolean big) {
    gboolean moved = FALSE;
    if (count < 1) count = 1;
    for (gint i = 0; i < count; i++) {
        GtkTextIter p = *iter;
        gtk_text_iter_forward_char(&p);
        while (!gtk_text_iter_is_end(&p) &&
               (gtk_text_iter_ends_line(&p) || is_blank(gtk_text_iter_get_char(&p)))) {
            gtk_text_iter_forward_char(&p);
        }
        if (gtk_text_iter_is_end(&p)) break;
        gint sc = cls_at(&p, big);
        for (;;) {
            GtkTextIter next = p;
            gtk_text_iter_forward_char(&next);
            if (gtk_text_iter_ends_line(&next) || cls_at(&next, big) != sc) break;
            p = next;
        }
        *iter = p;
        moved = TRUE;
    }
    return moved;
}

gboolean vim_motion_word_backward(GtkTextIter* iter, gint count, gboolean big) {
    gboolean moved = FALSE;
    if (count < 1) count = 1;
    for (gint i = 0; i < count; i++) {
        GtkTextIter p = *iter;
        if (!gtk_text_iter_backward_char(&p)) break;
        gboolean stop_empty = FALSE;
        for (;;) {
            if (on_newline(&p)) {
                if (gtk_text_iter_starts_line(&p)) {
                    stop_empty = TRUE;
                    break;
                }
                if (!gtk_text_iter_backward_char(&p)) break;
                continue;
            }
            if (is_blank(gtk_text_iter_get_char(&p))) {
                if (!gtk_text_iter_backward_char(&p)) break;
                continue;
            }
            break;
        }
        if (!stop_empty) {
            gint sc = cls_at(&p, big);
            for (;;) {
                GtkTextIter prev = p;
                if (!gtk_text_iter_backward_char(&prev)) break;
                if (gtk_text_iter_ends_line(&prev) || cls_at(&prev, big) != sc) break;
                p = prev;
            }
        }
        *iter = p;
        moved = TRUE;
    }
    return moved;
}

gboolean vim_motion_word_end_backward(GtkTextIter* iter, gint count, gboolean big) {
    gboolean moved = FALSE;
    if (count < 1) count = 1;
    for (gint i = 0; i < count; i++) {
        GtkTextIter p = *iter;
        gint sc = cls_at(&p, big);
        gboolean ok = TRUE;
        if (sc != 0) {
            for (;;) {
                if (!gtk_text_iter_backward_char(&p)) {
                    ok = FALSE;
                    break;
                }
                if (gtk_text_iter_ends_line(&p) || cls_at(&p, big) != sc) break;
            }
        } else if (!gtk_text_iter_backward_char(&p)) {
            ok = FALSE;
        }
        if (!ok) break;
        for (;;) {
            if (on_newline(&p)) {
                if (gtk_text_iter_starts_line(&p)) break;
                if (!gtk_text_iter_backward_char(&p)) break;
                continue;
            }
            if (is_blank(gtk_text_iter_get_char(&p))) {
                if (!gtk_text_iter_backward_char(&p)) break;
                continue;
            }
            break;
        }
        *iter = p;
        moved = TRUE;
    }
    return moved;
}

/* ------------------------------------------------------------------------- */
/* Characters, brackets, paragraphs, sentences                               */
/* ------------------------------------------------------------------------- */

gboolean vim_motion_find_char(GtkTextIter* iter, gunichar c, gint count, gboolean forward,
                              gboolean till, gboolean skip_adjacent) {
    if (count < 1) count = 1;
    GtkTextIter p = *iter;
    if (till && skip_adjacent) {
        GtkTextIter q = p;
        gboolean ok = forward ? (gtk_text_iter_forward_char(&q), !gtk_text_iter_ends_line(&q))
                              : (!gtk_text_iter_starts_line(&q) && gtk_text_iter_backward_char(&q));
        if (ok && gtk_text_iter_get_char(&q) == c) p = q;
    }
    for (gint i = 0; i < count; i++) {
        for (;;) {
            if (forward) {
                if (gtk_text_iter_ends_line(&p)) return FALSE;
                gtk_text_iter_forward_char(&p);
                if (gtk_text_iter_ends_line(&p)) return FALSE;
            } else {
                if (gtk_text_iter_starts_line(&p)) return FALSE;
                gtk_text_iter_backward_char(&p);
            }
            if (gtk_text_iter_get_char(&p) == c) break;
        }
    }
    if (till) {
        if (forward) gtk_text_iter_backward_char(&p);
        else gtk_text_iter_forward_char(&p);
    }
    *iter = p;
    return TRUE;
}

static gunichar bracket_partner(gunichar c, gboolean* is_open) {
    switch (c) {
        case '(': *is_open = TRUE; return ')';
        case '[': *is_open = TRUE; return ']';
        case '{': *is_open = TRUE; return '}';
        case ')': *is_open = FALSE; return '(';
        case ']': *is_open = FALSE; return '[';
        case '}': *is_open = FALSE; return '{';
        default: return 0;
    }
}

/* From p on c, find its partner (nested pairs skipped) */
static gboolean find_partner(GtkTextIter* p, gunichar c, gunichar partner, gboolean forward) {
    gint depth = 0;
    GtkTextIter q = *p;
    for (;;) {
        gboolean ok = forward ? gtk_text_iter_forward_char(&q) : gtk_text_iter_backward_char(&q);
        if (!ok || gtk_text_iter_is_end(&q)) return FALSE;
        gunichar ch = gtk_text_iter_get_char(&q);
        if (ch == c) {
            depth++;
        } else if (ch == partner) {
            if (depth == 0) {
                *p = q;
                return TRUE;
            }
            depth--;
        }
    }
}

gboolean vim_motion_match_pair(GtkTextIter* iter) {
    GtkTextIter p = *iter;
    gboolean is_open = FALSE;
    gunichar partner = 0;
    while (!gtk_text_iter_ends_line(&p)) {
        partner = bracket_partner(gtk_text_iter_get_char(&p), &is_open);
        if (partner != 0) break;
        gtk_text_iter_forward_char(&p);
    }
    if (partner == 0) return FALSE;
    if (!find_partner(&p, gtk_text_iter_get_char(&p), partner, is_open)) return FALSE;
    *iter = p;
    return TRUE;
}

gboolean vim_motion_unmatched(GtkTextIter* iter, gunichar bracket, gint count) {
    gboolean is_open = FALSE;
    gunichar partner = bracket_partner(bracket, &is_open);
    if (partner == 0) return FALSE;
    if (count < 1) count = 1;
    GtkTextIter p = *iter;
    for (gint i = 0; i < count; i++) {
        /* An open bracket is searched backward, a close one forward */
        if (!find_partner(&p, partner, bracket, !is_open)) return i > 0 ? (*iter = p, TRUE) : FALSE;
    }
    *iter = p;
    return TRUE;
}

gboolean vim_motion_paragraph(GtkTextIter* iter, gint count, gboolean forward) {
    GtkTextBuffer* buf = gtk_text_iter_get_buffer(iter);
    gint last = gtk_text_buffer_get_line_count(buf) - 1;
    gint line = gtk_text_iter_get_line(iter);
    if (count < 1) count = 1;
    gboolean to_edge = FALSE;
    for (gint i = 0; i < count; i++) {
        if (forward) {
            while (line < last && vim_line_is_empty(buf, line)) line++;
            while (line < last && !vim_line_is_empty(buf, line)) line++;
            if (line >= last && !vim_line_is_empty(buf, last)) {
                to_edge = TRUE;
                break;
            }
        } else {
            while (line > 0 && vim_line_is_empty(buf, line)) line--;
            while (line > 0 && !vim_line_is_empty(buf, line)) line--;
            if (line == 0) {
                to_edge = TRUE;
                break;
            }
        }
    }
    GtkTextIter p;
    if (to_edge) {
        if (forward) {
            gtk_text_buffer_get_end_iter(buf, &p);
        } else {
            gtk_text_buffer_get_start_iter(buf, &p);
        }
    } else {
        gtk_text_buffer_get_iter_at_line(buf, &p, line);
    }
    if (gtk_text_iter_equal(&p, iter)) return FALSE;
    *iter = p;
    return TRUE;
}

static gboolean is_sentence_end_char(gunichar c) {
    return c == '.' || c == '!' || c == '?';
}

static gboolean is_closing_char(gunichar c) {
    return c == ')' || c == ']' || c == '"' || c == '\'';
}

/* Does a sentence start at p? */
static gboolean sentence_starts_at(const GtkTextIter* p) {
    if (gtk_text_iter_is_start(p)) return TRUE;
    if (on_empty_line(p)) return TRUE;
    gunichar c = gtk_text_iter_get_char(p);
    if (gtk_text_iter_is_end(p) || is_blank(c) || on_newline(p)) return FALSE;
    GtkTextIter q = *p;
    gboolean saw_space = FALSE;
    gboolean saw_empty_line = FALSE;
    while (gtk_text_iter_backward_char(&q)) {
        if (on_newline(&q)) {
            saw_space = TRUE;
            if (gtk_text_iter_starts_line(&q)) saw_empty_line = TRUE;
            continue;
        }
        if (is_blank(gtk_text_iter_get_char(&q))) {
            saw_space = TRUE;
            continue;
        }
        break;
    }
    if (saw_empty_line) return TRUE;
    if (!saw_space) return FALSE;
    if (gtk_text_iter_is_start(&q) && is_blank(gtk_text_iter_get_char(&q))) return TRUE;
    while (is_closing_char(gtk_text_iter_get_char(&q))) {
        if (!gtk_text_iter_backward_char(&q)) return FALSE;
    }
    return is_sentence_end_char(gtk_text_iter_get_char(&q));
}

gboolean vim_motion_sentence(GtkTextIter* iter, gint count, gboolean forward) {
    if (count < 1) count = 1;
    GtkTextIter p = *iter;
    gboolean moved = FALSE;
    for (gint i = 0; i < count; i++) {
        GtkTextIter q = p;
        gboolean found = FALSE;
        if (forward) {
            while (gtk_text_iter_forward_char(&q)) {
                if (sentence_starts_at(&q)) {
                    found = TRUE;
                    break;
                }
            }
            if (!found) {
                gtk_text_iter_forward_to_end(&q);
                found = !gtk_text_iter_equal(&q, &p);
            }
        } else {
            while (gtk_text_iter_backward_char(&q)) {
                if (sentence_starts_at(&q)) {
                    found = TRUE;
                    break;
                }
            }
            if (!found && !gtk_text_iter_equal(&q, &p)) found = TRUE;
        }
        if (!found) break;
        p = q;
        moved = TRUE;
    }
    if (moved) *iter = p;
    return moved;
}

/* ------------------------------------------------------------------------- */
/* Text objects                                                              */
/* ------------------------------------------------------------------------- */

static gboolean has_selection(const GtkTextIter* start, const GtkTextIter* end) {
    return gtk_text_iter_compare(start, end) < 0;
}

/* Extend e over one run of characters of the same class on the line */
static void skip_run(GtkTextIter* e, gboolean big) {
    if (gtk_text_iter_ends_line(e)) return;
    gint c = cls_at(e, big);
    while (!gtk_text_iter_ends_line(e) && cls_at(e, big) == c) gtk_text_iter_forward_char(e);
}

static void skip_blanks(GtkTextIter* e) {
    while (!gtk_text_iter_ends_line(e) && is_blank(gtk_text_iter_get_char(e))) {
        gtk_text_iter_forward_char(e);
    }
}

static gboolean textobj_word(gboolean big, gboolean around, gint count,
                             GtkTextIter* start, GtkTextIter* end) {
    GtkTextIter s, e;
    gint units = 0;
    if (has_selection(start, end)) {
        s = *start;
        e = *end;
    } else {
        s = *start;
        if (gtk_text_iter_ends_line(&s)) {
            if (!gtk_text_iter_starts_line(&s)) return FALSE;
            *end = s; /* empty line: nothing to select */
            gtk_text_iter_forward_char(end);
            return !gtk_text_iter_equal(start, end);
        }
        gint c0 = cls_at(&s, big);
        for (;;) {
            GtkTextIter prev = s;
            if (gtk_text_iter_starts_line(&prev) || !gtk_text_iter_backward_char(&prev)) break;
            if (cls_at(&prev, big) != c0) break;
            s = prev;
        }
        e = s;
        skip_run(&e, big);
        units = 1;
        if (around) {
            if (c0 != 0) {
                GtkTextIter t = e;
                skip_blanks(&t);
                if (!gtk_text_iter_equal(&t, &e)) {
                    e = t;
                } else {
                    /* No trailing white space: take the leading one */
                    for (;;) {
                        GtkTextIter prev = s;
                        if (gtk_text_iter_starts_line(&prev) || !gtk_text_iter_backward_char(&prev)) break;
                        if (!is_blank(gtk_text_iter_get_char(&prev))) break;
                        s = prev;
                    }
                }
            } else {
                skip_run(&e, big);
            }
        }
    }
    for (; units < count; units++) {
        if (gtk_text_iter_ends_line(&e)) break;
        if (around) {
            skip_blanks(&e);
            skip_run(&e, big);
            skip_blanks(&e);
        } else {
            skip_run(&e, big);
        }
    }
    *start = s;
    *end = e;
    return TRUE;
}

static gboolean textobj_quote(gunichar q, gboolean around, GtkTextIter* start, GtkTextIter* end) {
    GtkTextIter ls = *start, le = *start;
    gtk_text_iter_set_line_offset(&ls, 0);
    vim_iter_line_end(&le);
    gint col = gtk_text_iter_get_line_offset(start);
    gchar* line = gtk_text_iter_get_slice(&ls, &le);
    glong n = 0;
    gunichar* chars = g_utf8_to_ucs4_fast(line, -1, &n);
    g_free(line);
    GArray* pos = g_array_new(FALSE, FALSE, sizeof(gint));
    for (gint i = 0; i < n; i++) {
        if (chars[i] == '\\' && q != '`') {
            i++;
            continue;
        }
        if (chars[i] == q) g_array_append_val(pos, i);
    }
    gint open = -1, close = -1;
    gint np = (gint)pos->len;
    gint k = 0;
    while (k < np && g_array_index(pos, gint, k) < col) k++;
    if (k < np && g_array_index(pos, gint, k) == col) {
        /* On a quote: opening when an even number of quotes precede it */
        if (k % 2 == 0 && k + 1 < np) {
            open = col;
            close = g_array_index(pos, gint, k + 1);
        } else if (k % 2 == 1) {
            open = g_array_index(pos, gint, k - 1);
            close = col;
        }
    } else if (k % 2 == 1 && k < np) {
        open = g_array_index(pos, gint, k - 1);
        close = g_array_index(pos, gint, k);
    } else if (k + 1 < np) {
        open = g_array_index(pos, gint, k);
        close = g_array_index(pos, gint, k + 1);
    }
    g_array_free(pos, TRUE);
    if (open < 0 || close < 0) {
        g_free(chars);
        return FALSE;
    }
    gint s = open + 1, e = close;
    if (around) {
        s = open;
        e = close + 1;
        gint t = e;
        while (t < n && is_blank(chars[t])) t++;
        if (t > e) {
            e = t;
        } else {
            while (s > 0 && is_blank(chars[s - 1])) s--;
        }
    }
    g_free(chars);
    *start = ls;
    gtk_text_iter_set_line_offset(start, s);
    *end = ls;
    gtk_text_iter_set_line_offset(end, e);
    return TRUE;
}

/* Bracket block around p: open / close positions of the count-th level */
static gboolean find_block(const GtkTextIter* p, gunichar open_c, gunichar close_c, gint count,
                           GtkTextIter* open, GtkTextIter* close) {
    GtkTextIter q = *p;
    gint found = 0;
    if (gtk_text_iter_get_char(&q) == open_c) {
        found = 1;
    }
    while (found < count) {
        gint depth = 0;
        gboolean hit = FALSE;
        while (gtk_text_iter_backward_char(&q)) {
            gunichar ch = gtk_text_iter_get_char(&q);
            if (ch == close_c) {
                depth++;
            } else if (ch == open_c) {
                if (depth == 0) {
                    hit = TRUE;
                    break;
                }
                depth--;
            }
        }
        if (!hit) return FALSE;
        found++;
    }
    GtkTextIter c = q;
    if (!find_partner(&c, open_c, close_c, TRUE)) return FALSE;
    *open = q;
    *close = c;
    return TRUE;
}

static gboolean line_blank_before(const GtkTextIter* it) {
    GtkTextIter p = *it;
    while (!gtk_text_iter_starts_line(&p)) {
        gtk_text_iter_backward_char(&p);
        if (!is_blank(gtk_text_iter_get_char(&p))) return FALSE;
    }
    return TRUE;
}

static gboolean textobj_block(gunichar open_c, gunichar close_c, gboolean around, gint count,
                              GtkTextIter* start, GtkTextIter* end, gboolean* linewise) {
    gboolean visual = has_selection(start, end);
    GtkTextIter cursor = *start;
    for (gint tries = 0; tries < 50; tries++) {
        GtkTextIter o, c;
        if (!find_block(&cursor, open_c, close_c, count, &o, &c)) return FALSE;
        GtkTextIter s = o, e = c;
        gboolean lw = FALSE;
        if (around) {
            gtk_text_iter_forward_char(&e);
        } else {
            gtk_text_iter_forward_char(&s);
            if (on_newline(&s)) {
                GtkTextIter after = s;
                gtk_text_iter_forward_char(&after);
                if (line_blank_before(&e) && gtk_text_iter_compare(&after, &e) <= 0) {
                    GtkTextIter close_line = e;
                    gtk_text_iter_set_line_offset(&close_line, 0);
                    s = after;
                    e = close_line;
                    lw = gtk_text_iter_compare(&s, &e) < 0;
                }
            }
        }
        if (visual && gtk_text_iter_compare(&s, start) >= 0 && gtk_text_iter_compare(&e, end) <= 0) {
            count++; /* already selected: take the enclosing block */
            continue;
        }
        *start = s;
        *end = e;
        if (linewise) *linewise = lw;
        return TRUE;
    }
    return FALSE;
}

typedef struct {
    gint os, oe, cs, ce; /* open tag start / end, close tag start / end (char offsets) */
} TagPair;

static gboolean textobj_tag(GtkTextBuffer* buf, gboolean around, gint count,
                            GtkTextIter* start, GtkTextIter* end) {
    GtkTextIter bs, be;
    gtk_text_buffer_get_bounds(buf, &bs, &be);
    gchar* text = gtk_text_buffer_get_slice(buf, &bs, &be, TRUE);
    glong n = 0;
    gunichar* t = g_utf8_to_ucs4_fast(text, -1, &n);
    g_free(text);
    GArray* stack = g_array_new(FALSE, FALSE, sizeof(gint)); /* index in names */
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    GArray* starts = g_array_new(FALSE, FALSE, sizeof(gint));
    GArray* ends = g_array_new(FALSE, FALSE, sizeof(gint));
    GArray* pairs = g_array_new(FALSE, FALSE, sizeof(TagPair));
    for (glong i = 0; i < n; i++) {
        if (t[i] != '<') continue;
        if (i + 3 < n && t[i + 1] == '!' && t[i + 2] == '-' && t[i + 3] == '-') {
            glong j = i + 4;
            while (j + 2 < n && !(t[j] == '-' && t[j + 1] == '-' && t[j + 2] == '>')) j++;
            i = j + 2;
            continue;
        }
        gboolean closing = (i + 1 < n && t[i + 1] == '/');
        glong j = i + (closing ? 2 : 1);
        if (j >= n || !(g_unichar_isalpha(t[j]) || t[j] == '_')) continue;
        GString* name = g_string_new(NULL);
        while (j < n && (g_unichar_isalnum(t[j]) || t[j] == '-' || t[j] == '_' || t[j] == ':' || t[j] == '.')) {
            g_string_append_unichar(name, t[j]);
            j++;
        }
        while (j < n && t[j] != '>') j++;
        if (j >= n) {
            g_string_free(name, TRUE);
            break;
        }
        gboolean self_closing = t[j - 1] == '/';
        gint ts = (gint)i, te = (gint)j + 1;
        if (closing) {
            for (gint k = (gint)stack->len - 1; k >= 0; k--) {
                gint idx = g_array_index(stack, gint, k);
                if (strcmp(g_ptr_array_index(names, idx), name->str) == 0) {
                    TagPair pr = {g_array_index(starts, gint, idx), g_array_index(ends, gint, idx), ts, te};
                    g_array_append_val(pairs, pr);
                    g_array_set_size(stack, k);
                    break;
                }
            }
            g_string_free(name, TRUE);
        } else if (!self_closing) {
            g_ptr_array_add(names, g_string_free(name, FALSE));
            g_array_append_val(starts, ts);
            g_array_append_val(ends, te);
            gint idx = (gint)names->len - 1;
            g_array_append_val(stack, idx);
        } else {
            g_string_free(name, TRUE);
        }
        i = j;
    }
    g_free(t);
    gint cur = gtk_text_iter_get_offset(start);
    gint sel_s = gtk_text_iter_get_offset(start), sel_e = gtk_text_iter_get_offset(end);
    gboolean visual = sel_e > sel_s;
    /* Enclosing pairs from the innermost outward */
    GArray* around_cur = g_array_new(FALSE, FALSE, sizeof(TagPair));
    for (guint i = 0; i < pairs->len; i++) {
        TagPair* p = &g_array_index(pairs, TagPair, i);
        if (p->os <= cur && cur < p->ce) g_array_append_val(around_cur, *p);
    }
    /* Pairs are recorded when they close: inner ones first */
    gboolean ok = FALSE;
    gint level = 0;
    for (guint i = 0; i < around_cur->len; i++) {
        TagPair* p = &g_array_index(around_cur, TagPair, i);
        gint s = around ? p->os : p->oe, e = around ? p->ce : p->cs;
        if (visual && s >= sel_s && e <= sel_e) continue;
        level++;
        if (level < count) continue;
        gtk_text_buffer_get_iter_at_offset(buf, start, s);
        gtk_text_buffer_get_iter_at_offset(buf, end, e);
        ok = TRUE;
        break;
    }
    g_array_free(around_cur, TRUE);
    g_array_free(pairs, TRUE);
    g_array_free(stack, TRUE);
    g_array_free(starts, TRUE);
    g_array_free(ends, TRUE);
    g_ptr_array_free(names, TRUE);
    return ok;
}

static gboolean textobj_paragraph(GtkTextBuffer* buf, gboolean around, gint count,
                                  GtkTextIter* start, GtkTextIter* end) {
    gint last = gtk_text_buffer_get_line_count(buf) - 1;
    gint s, e;
    gint units = 0;
    if (has_selection(start, end)) {
        s = gtk_text_iter_get_line(start);
        GtkTextIter last_sel = *end;
        gtk_text_iter_backward_char(&last_sel);
        e = gtk_text_iter_get_line(&last_sel);
    } else {
        gint line = gtk_text_iter_get_line(start);
        gboolean type = vim_line_is_blank(buf, line);
        s = line;
        while (s > 0 && vim_line_is_blank(buf, s - 1) == type) s--;
        e = line;
        while (e < last && vim_line_is_blank(buf, e + 1) == type) e++;
        units = 1;
        if (around) {
            if (!type) {
                if (e < last && vim_line_is_blank(buf, e + 1)) {
                    while (e < last && vim_line_is_blank(buf, e + 1)) e++;
                } else {
                    while (s > 0 && vim_line_is_blank(buf, s - 1)) s--;
                }
            } else {
                while (e < last && !vim_line_is_blank(buf, e + 1)) e++;
            }
        }
    }
    for (; units < count && e < last; units++) {
        gboolean type = vim_line_is_blank(buf, e + 1);
        e++;
        while (e < last && vim_line_is_blank(buf, e + 1) == type) e++;
        if (around && !type) {
            while (e < last && vim_line_is_blank(buf, e + 1)) e++;
        }
    }
    gtk_text_buffer_get_iter_at_line(buf, start, s);
    if (e >= last) {
        gtk_text_buffer_get_end_iter(buf, end);
    } else {
        gtk_text_buffer_get_iter_at_line(buf, end, e + 1);
    }
    return TRUE;
}

static gboolean textobj_sentence(gboolean around, gint count, GtkTextIter* start, GtkTextIter* end) {
    GtkTextIter s = *start;
    if (!sentence_starts_at(&s)) vim_motion_sentence(&s, 1, FALSE);
    GtkTextIter e = s;
    vim_motion_sentence(&e, count, TRUE);
    if (!around) {
        /* Drop the white space before the next sentence */
        GtkTextIter t = e;
        while (gtk_text_iter_compare(&t, &s) > 0) {
            GtkTextIter prev = t;
            gtk_text_iter_backward_char(&prev);
            gunichar c = gtk_text_iter_get_char(&prev);
            if (!is_blank(c) && c != '\n') break;
            t = prev;
        }
        e = t;
    }
    if (gtk_text_iter_compare(&s, &e) >= 0) return FALSE;
    *start = s;
    *end = e;
    return TRUE;
}

gboolean vim_textobj(GtkTextBuffer* buf, gunichar obj, gboolean around, gint count,
                     GtkTextIter* start, GtkTextIter* end, gboolean* linewise) {
    if (linewise != NULL) *linewise = FALSE;
    if (count < 1) count = 1;
    switch (obj) {
        case 'w': return textobj_word(FALSE, around, count, start, end);
        case 'W': return textobj_word(TRUE, around, count, start, end);
        case 's': return textobj_sentence(around, count, start, end);
        case 'p':
            if (linewise != NULL) *linewise = TRUE;
            return textobj_paragraph(buf, around, count, start, end);
        case '(': case ')': case 'b': return textobj_block('(', ')', around, count, start, end, linewise);
        case '[': case ']': return textobj_block('[', ']', around, count, start, end, linewise);
        case '{': case '}': case 'B': return textobj_block('{', '}', around, count, start, end, linewise);
        case '<': case '>': return textobj_block('<', '>', around, count, start, end, linewise);
        case '"': case '\'': case '`': return textobj_quote(obj, around, start, end);
        case 't': return textobj_tag(buf, around, count, start, end);
        default: return FALSE;
    }
}
