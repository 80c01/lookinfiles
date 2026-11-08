/* Org-mode plain lists: items, bullets, checkboxes and statistics cookies
 * ([2/3], [66%]) on an array of lines (see org_doc.h) */
#include "org/org_doc.h"
#include "org/org_doc_private.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Lists and checkboxes                                                      */
/* ------------------------------------------------------------------------- */

gboolean org_item_parse(const gchar* line, OrgItem* it) {
    memset(it, 0, sizeof(*it));
    if (line == NULL) return FALSE;
    gint b = org_doc_indent_bytes(line);
    it->indent = org_doc_indent_of(line);
    const gchar* p = line + b;
    gint blen = 0;
    if ((p[0] == '-' || p[0] == '+' || (p[0] == '*' && b > 0)) && (p[1] == ' ' || p[1] == '\0')) {
        blen = 1;
    } else if (g_ascii_isdigit(p[0])) {
        gint d = 0;
        while (g_ascii_isdigit(p[d]) && d < 9) d++;
        if ((p[d] == '.' || p[d] == ')') && (p[d + 1] == ' ' || p[d + 1] == '\0')) {
            it->ordered = TRUE;
            it->number = atoi(p);
            blen = d + 1;
        }
    }
    if (blen == 0) return FALSE;
    memcpy(it->bullet, p, blen);
    it->bullet[blen] = '\0';
    const gchar* q = p + blen;
    while (*q == ' ') q++;
    if (q[0] == '[' && (q[1] == ' ' || q[1] == 'X' || q[1] == 'x' || q[1] == '-') && q[2] == ']' &&
        (q[3] == ' ' || q[3] == '\0')) {
        it->checkbox = q[1] == 'x' ? 'X' : q[1];
        q += 3;
        while (*q == ' ') q++;
    }
    it->content = (gint)(q - line);
    return TRUE;
}

/* Byte offset of the '[' of the item's checkbox, -1 if none */
static gint checkbox_byte(const gchar* line) {
    OrgItem it;
    if (!org_item_parse(line, &it) || it.checkbox == 0) return -1;
    const gchar* q = line + org_doc_indent_bytes(line) + strlen(it.bullet);
    while (*q == ' ') q++;
    return (gint)(q - line);
}

static void set_checkbox(GPtrArray* lines, gint i, gchar c) {
    gint b = checkbox_byte(org_line(lines, i));
    if (b < 0) return;
    gchar* l = g_strdup(org_line(lines, i));
    l[b + 1] = c;
    org_doc_set_line(lines, i, l);
}

gint org_item_end(GPtrArray* lines, gint i) {
    gint limit = org_doc_limit(lines);
    gint ind = org_doc_indent_of(org_line(lines, i));
    gint last = i;
    gint blanks = 0;
    for (gint j = i + 1; j < limit; j++) {
        const gchar* l = org_line(lines, j);
        if (org_doc_is_blank(l)) {
            if (++blanks >= 2) break;
            continue;
        }
        blanks = 0;
        if (org_heading_level(l) > 0 || org_doc_indent_of(l) <= ind) break;
        last = j;
    }
    return last + 1;
}

gint org_item_at(GPtrArray* lines, gint line) {
    OrgItem it;
    if (org_item_parse(org_line(lines, line), &it)) return line;
    for (gint j = line - 1; j >= 0 && j > line - 2000; j--) {
        const gchar* l = org_line(lines, j);
        if (org_heading_level(l) > 0) return -1;
        if (org_doc_is_blank(l)) continue;
        if (org_item_parse(l, &it)) {
            if (org_item_end(lines, j) > line) return j;
            if (it.indent == 0) return -1;
        } else if (org_doc_indent_of(l) == 0) {
            return -1;
        }
    }
    return -1;
}

gint org_item_parent(GPtrArray* lines, gint i) {
    gint ind = org_doc_indent_of(org_line(lines, i));
    for (gint j = i - 1; j >= 0; j--) {
        const gchar* l = org_line(lines, j);
        if (org_doc_is_blank(l)) continue;
        if (org_heading_level(l) > 0) return -1;
        gint li = org_doc_indent_of(l);
        if (li >= ind) continue;
        OrgItem it;
        if (org_item_parse(l, &it) && org_item_end(lines, j) > i) return j;
        return -1;
    }
    return -1;
}

static gint item_prev_sibling(GPtrArray* lines, gint i) {
    gint ind = org_doc_indent_of(org_line(lines, i));
    for (gint j = i - 1; j >= 0; j--) {
        const gchar* l = org_line(lines, j);
        if (org_doc_is_blank(l)) continue;
        if (org_heading_level(l) > 0) return -1;
        gint li = org_doc_indent_of(l);
        if (li > ind) continue;
        OrgItem it;
        if (li == ind && org_item_parse(l, &it) && org_item_end(lines, j) + 1 >= i) return j;
        return -1;
    }
    return -1;
}

static gint item_next_sibling(GPtrArray* lines, gint i) {
    gint ind = org_doc_indent_of(org_line(lines, i));
    gint k = org_item_end(lines, i);
    gint limit = org_doc_limit(lines);
    if (k < limit && org_doc_is_blank(org_line(lines, k))) k++;
    OrgItem it;
    if (k < limit && org_item_parse(org_line(lines, k), &it) && it.indent == ind) return k;
    return -1;
}

static gint item_first_sibling(GPtrArray* lines, gint i) {
    gint p;
    while ((p = item_prev_sibling(lines, i)) >= 0) i = p;
    return i;
}

/* Replace the bullet of item line i with bullet; keep the cursor in place */
static void set_bullet(GPtrArray* lines, OrgPos* pos, gint i, const gchar* bullet) {
    const gchar* l = org_line(lines, i);
    OrgItem it;
    if (!org_item_parse(l, &it)) return;
    gint b = org_doc_indent_bytes(l);
    gint old_len = (gint)strlen(it.bullet);
    gchar* text = g_strdup_printf("%.*s%s%s", b, l, bullet, l + b + old_len);
    org_doc_set_line(lines, i, text);
    if (pos != NULL && pos->line == i && pos->col > b) {
        pos->col += (gint)strlen(bullet) - old_len;
        org_doc_clamp_pos(lines, pos);
    }
}

void org_renumber_list(GPtrArray* lines, gint i) {
    OrgItem it;
    if (!org_item_parse(org_line(lines, i), &it)) return;
    gint j = item_first_sibling(lines, i);
    if (!org_item_parse(org_line(lines, j), &it) || !it.ordered) return;
    gint n = 1;
    for (; j >= 0; j = item_next_sibling(lines, j)) {
        if (!org_item_parse(org_line(lines, j), &it) || !it.ordered) break;
        gchar delim = it.bullet[strlen(it.bullet) - 1];
        gchar* b = g_strdup_printf("%d%c", n++, delim);
        if (strcmp(b, it.bullet) != 0) set_bullet(lines, NULL, j, b);
        g_free(b);
    }
}

gboolean org_insert_item(GPtrArray* lines, OrgPos* pos, gboolean checkbox) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    const gchar* l = org_line(lines, i);
    OrgItem it;
    org_item_parse(l, &it);
    gchar* bullet = it.ordered ? g_strdup_printf("%d%c", it.number + 1, it.bullet[strlen(it.bullet) - 1])
                               : g_strdup(it.bullet);
    gchar* prefix = g_strdup_printf("%.*s%s %s", org_doc_indent_bytes(l), l, bullet,
                                    (checkbox || it.checkbox != 0) ? "[ ] " : "");
    g_free(bullet);
    gint col_b = org_col_to_byte(l, pos->col);
    gint at;
    gchar* rest = g_strdup("");
    if (pos->line == i && col_b <= org_doc_indent_bytes(l)) {
        at = i; /* before the bullet: new item above */
    } else {
        at = org_item_end(lines, i);
        if (pos->line == i && col_b >= it.content && l[col_b] != '\0') {
            g_free(rest);
            rest = g_strdup(l + col_b);
            g_strchug(rest);
            gchar* head = g_strndup(l, col_b);
            g_strchomp(head);
            org_doc_set_line(lines, i, head);
        }
    }
    org_doc_insert_line(lines, at, g_strconcat(prefix, rest, NULL));
    pos->line = at;
    pos->col = org_utf8_width(prefix);
    g_free(prefix);
    g_free(rest);
    org_renumber_list(lines, at);
    return TRUE;
}

/* Shift lines [from, to) by delta columns (negative removes leading spaces) */
static void shift_lines(GPtrArray* lines, gint from, gint to, gint delta) {
    for (gint j = from; j < to; j++) {
        const gchar* l = org_line(lines, j);
        if (org_doc_is_blank(l)) continue;
        if (delta > 0) {
            gchar* pad = g_strnfill(delta, ' ');
            org_doc_set_line(lines, j, g_strconcat(pad, l, NULL));
            g_free(pad);
        } else {
            gint k = 0;
            while (k < -delta && (l[k] == ' ' || l[k] == '\t')) k++;
            org_doc_set_line(lines, j, g_strdup(l + k));
        }
    }
}

gboolean org_indent_item(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    gint ind = org_doc_indent_of(org_line(lines, i));
    gint e = org_item_end(lines, i);
    gint target;
    gint other;
    if (dir > 0) {
        other = item_prev_sibling(lines, i);
        if (other < 0) {
            org_doc_set_msg(msg, "Cannot indent the first item of a list");
            return FALSE;
        }
        OrgItem p;
        org_item_parse(org_line(lines, other), &p);
        target = p.indent + (gint)strlen(p.bullet) + 1;
    } else {
        other = org_item_parent(lines, i);
        if (other < 0) {
            org_doc_set_msg(msg, "Cannot outdent beyond margin");
            return FALSE;
        }
        target = org_doc_indent_of(org_line(lines, other));
    }
    gint delta = target - ind;
    shift_lines(lines, i, e, delta);
    if (pos->line >= i && pos->line < e) {
        pos->col += delta;
        org_doc_clamp_pos(lines, pos);
    }
    org_renumber_list(lines, i);
    org_renumber_list(lines, other);
    return TRUE;
}

gboolean org_move_item(GPtrArray* lines, OrgPos* pos, gint dir, OrgSwap* swap, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    OrgSwap sw;
    if (dir < 0) {
        gint p = item_prev_sibling(lines, i);
        if (p < 0) {
            org_doc_set_msg(msg, "Cannot move this item further up");
            return FALSE;
        }
        gint e = org_item_end(lines, i);
        sw.start = p;
        sw.mid = i;
        sw.end = e;
        pos->line -= i - p;
    } else {
        gint n = item_next_sibling(lines, i);
        if (n < 0) {
            org_doc_set_msg(msg, "Cannot move this item further down");
            return FALSE;
        }
        gint e2 = org_item_end(lines, n);
        sw.start = i;
        sw.mid = n;
        sw.end = e2;
        pos->line += e2 - n;
    }
    org_doc_swap_blocks(lines, sw.start, sw.mid, sw.end);
    if (swap != NULL) *swap = sw;
    org_renumber_list(lines, sw.start);
    return TRUE;
}

gboolean org_cycle_bullet(GPtrArray* lines, OrgPos* pos, gint dir) {
    static const gchar* BULLETS[] = {"-", "+", "*", "1.", "1)"};
    gint n = (gint)G_N_ELEMENTS(BULLETS);
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    OrgItem it;
    org_item_parse(org_line(lines, i), &it);
    gint cur = 0;
    if (it.ordered) cur = it.bullet[strlen(it.bullet) - 1] == '.' ? 3 : 4;
    else cur = it.bullet[0] == '-' ? 0 : (it.bullet[0] == '+' ? 1 : 2);
    gint next = cur;
    do {
        next = (next + (dir >= 0 ? 1 : -1) + n) % n;
    } while (next == 2 && it.indent == 0); /* "*" at column 0 would be a heading */
    for (gint j = item_first_sibling(lines, i); j >= 0; j = item_next_sibling(lines, j)) {
        set_bullet(lines, pos, j, BULLETS[next]);
    }
    org_renumber_list(lines, i);
    return TRUE;
}

gboolean org_ctrl_c_minus(GPtrArray* lines, OrgPos* pos) {
    const gchar* l = org_line(lines, pos->line);
    if (org_item_at(lines, pos->line) == pos->line) return org_cycle_bullet(lines, pos, 1);
    if (org_doc_is_blank(l) || org_heading_level(l) > 0) return FALSE;
    gint b = org_doc_indent_bytes(l);
    org_doc_set_line(lines, pos->line, g_strdup_printf("%.*s- %s", b, l, l + b));
    if (pos->col >= b) pos->col += 2;
    return TRUE;
}

/* Direct sub-items of item i (their indent is the first sub-item's) */
static void item_children_stats(GPtrArray* lines, gint i, gint* done, gint* total,
                                gint* partial) {
    gint e = org_item_end(lines, i);
    gint child_ind = -1;
    for (gint j = i + 1; j < e; j++) {
        OrgItem c;
        if (!org_item_parse(org_line(lines, j), &c)) continue;
        if (child_ind < 0) child_ind = c.indent;
        if (c.indent != child_ind || c.checkbox == 0) continue;
        (*total)++;
        if (c.checkbox == 'X') (*done)++;
        if (c.checkbox == '-') (*partial)++;
    }
}

gboolean org_toggle_checkbox(GPtrArray* lines, OrgPos* pos, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    OrgItem it;
    if (i < 0 || !org_item_parse(org_line(lines, i), &it) || it.checkbox == 0) {
        org_doc_set_msg(msg, "No checkbox here");
        return FALSE;
    }
    gchar c = it.checkbox == 'X' ? ' ' : 'X';
    set_checkbox(lines, i, c);
    gint e = org_item_end(lines, i);
    for (gint j = i + 1; j < e; j++) set_checkbox(lines, j, c);
    /* Parents follow their children: all [X], none [ ], else [-] */
    for (gint p = org_item_parent(lines, i); p >= 0; p = org_item_parent(lines, p)) {
        OrgItem pi;
        org_item_parse(org_line(lines, p), &pi);
        if (pi.checkbox == 0) break;
        gint done = 0, total = 0, partial = 0;
        item_children_stats(lines, p, &done, &total, &partial);
        if (total == 0) break;
        set_checkbox(lines, p, done == total ? 'X' : ((done == 0 && partial == 0) ? ' ' : '-'));
    }
    return TRUE;
}

/* Find the next statistics cookie in s from byte from: [n/m] or [p%] */
static gboolean find_cookie(const gchar* s, gint from, gint* start, gint* end, gboolean* percent) {
    for (const gchar* p = strchr(s + from, '['); p != NULL; p = strchr(p + 1, '[')) {
        const gchar* q = p + 1;
        while (g_ascii_isdigit(*q)) q++;
        if (*q == '%' && q[1] == ']') {
            *start = (gint)(p - s);
            *end = (gint)(q - s) + 2;
            *percent = TRUE;
            return TRUE;
        }
        if (*q == '/') {
            q++;
            while (g_ascii_isdigit(*q)) q++;
            if (*q == ']') {
                *start = (gint)(p - s);
                *end = (gint)(q - s) + 1;
                *percent = FALSE;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static void heading_stats(GPtrArray* lines, gint h, const OrgTodoConfig* cfg, gint* done, gint* total) {
    gint ee = org_entry_end(lines, h);
    gint min_ind = G_MAXINT;
    for (gint j = h + 1; j < ee; j++) {
        OrgItem it;
        if (org_item_parse(org_line(lines, j), &it) && it.checkbox != 0) min_ind = MIN(min_ind, it.indent);
    }
    if (min_ind != G_MAXINT) {
        for (gint j = h + 1; j < ee; j++) {
            OrgItem it;
            if (!org_item_parse(org_line(lines, j), &it) || it.checkbox == 0 || it.indent != min_ind) continue;
            (*total)++;
            if (it.checkbox == 'X') (*done)++;
        }
        return;
    }
    gint se = org_subtree_end(lines, h);
    for (gint j = h + 1; j < se; j++) {
        if (org_heading_level(org_line(lines, j)) == 0 || org_heading_parent(lines, j) != h) continue;
        OrgHeading c;
        org_heading_parse(org_line(lines, j), cfg, &c);
        if (c.keyword != NULL) {
            (*total)++;
            if (org_todo_is_done(cfg, c.keyword)) (*done)++;
        }
        org_heading_clear(&c);
    }
}

gboolean org_update_statistics(GPtrArray* lines, const OrgTodoConfig* cfg) {
    gboolean changed = FALSE;
    for (gint i = 0; i < org_doc_n_lines(lines); i++) {
        const gchar* l = org_line(lines, i);
        gint s, e;
        gboolean pct;
        if (strchr(l, '[') == NULL || !find_cookie(l, 0, &s, &e, &pct)) continue;
        gint done = 0, total = 0, partial = 0;
        gboolean heading = org_heading_level(l) > 0;
        OrgItem it;
        if (heading) heading_stats(lines, i, cfg, &done, &total);
        else if (org_item_parse(l, &it)) item_children_stats(lines, i, &done, &total, &partial);
        else continue;
        GString* out = g_string_new(NULL);
        gint from = 0;
        while (find_cookie(l, from, &s, &e, &pct)) {
            g_string_append_len(out, l + from, s - from);
            if (pct) g_string_append_printf(out, "[%d%%]", total > 0 ? done * 100 / total : 0);
            else g_string_append_printf(out, "[%d/%d]", done, total);
            from = e;
        }
        g_string_append(out, l + from);
        if (strcmp(out->str, l) != 0) {
            changed = TRUE;
            if (heading) {
                OrgHeading hd;
                org_heading_parse(out->str, cfg, &hd);
                gboolean tags = hd.tags != NULL;
                if (tags) {
                    org_doc_set_line(lines, i, org_heading_build(&hd));
                    g_string_free(out, TRUE);
                    out = NULL;
                }
                org_heading_clear(&hd);
            }
            if (out != NULL) org_doc_set_line(lines, i, g_string_free(out, FALSE));
        } else {
            g_string_free(out, TRUE);
        }
    }
    return changed;
}
