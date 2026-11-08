/* Vim engine: line operations shared with Ex commands, and the operators
 * (d c y > < = gq g~ ...) over a motion or a Visual range (see vim_engine_private.h) */
#include "vim_engine_private.h"
#include "vim_keys.h"
#include "vim_motion.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Line operations shared with Ex commands                                   */
/* ------------------------------------------------------------------------- */

/* Text of lines l1..l2 with a final newline (register format) */
static gchar* lines_text(VimEngine* e, gint l1, gint l2) {
    GtkTextIter s, t;
    vim_e_iter_line(e, &s, l1);
    vim_e_iter_line(e, &t, l2);
    vim_iter_line_end(&t);
    gchar* body = gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
    gchar* text = g_strconcat(body, "\n", NULL);
    g_free(body);
    return text;
}

void vim_e_delete_lines(VimEngine* e, gint l1, gint l2, gunichar reg, gboolean to_register) {
    gint last = vim_e_last_line(e);
    if (l2 > last) l2 = last;
    if (l1 > l2) return;
    if (to_register) {
        gchar* text = lines_text(e, l1, l2);
        vim_e_reg_set(e, reg, text, REG_LINE, REG_DELETE);
        g_free(text);
    }
    GtkTextIter s, t;
    vim_e_iter_line(e, &s, l1);
    if (l2 < last) {
        vim_e_iter_line(e, &t, l2 + 1);
    } else {
        gtk_text_buffer_get_end_iter(e->buf, &t);
        if (l1 > 0) {
            /* Deleting the last lines: the newline before them goes too */
            vim_e_iter_line(e, &s, l1 - 1);
            vim_iter_line_end(&s);
        }
    }
    vim_e_delete(e, &s, &t);
    vim_e_set_mark(e, '[', &s);
    vim_e_set_mark(e, ']', &s);
    vim_e_set_mark(e, '.', &s);
    vim_e_set_cursor_line(e, MIN(l1, vim_e_last_line(e)), TRUE);
}

void vim_e_put_lines(VimEngine* e, gint after_line, const gchar* text) {
    GtkTextIter it;
    gint last = vim_e_last_line(e);
    if (after_line >= last) {
        gtk_text_buffer_get_end_iter(e->buf, &it);
        gchar* body = g_strndup(text, strlen(text) - (g_str_has_suffix(text, "\n") ? 1 : 0));
        gchar* t = g_strconcat("\n", body, NULL);
        vim_e_insert_text(e, &it, t);
        g_free(t);
        g_free(body);
    } else {
        vim_e_iter_line(e, &it, after_line + 1);
        vim_e_insert_text(e, &it, text);
    }
}

void vim_e_shift_lines(VimEngine* e, gint l1, gint l2, gint amount) {
    gint sw = e->opt.shiftwidth > 0 ? e->opt.shiftwidth : e->opt.tabstop;
    for (gint l = l1; l <= l2 && l <= vim_e_last_line(e); l++) {
        if (vim_line_is_empty(e->buf, l)) continue;
        gint ind = vim_line_indent(e->buf, l, e->opt.tabstop);
        gint w = ind + amount * sw;
        if (amount < 0 && w < 0) w = 0;
        vim_e_set_line_indent(e, l, w);
    }
}

void vim_e_join_lines(VimEngine* e, gint l1, gint l2, gboolean spaces) {
    gint last = vim_e_last_line(e);
    if (l2 > last) l2 = last;
    GtkTextIter cursor;
    gboolean moved = FALSE;
    for (gint i = l1; i < l2; i++) {
        GtkTextIter end, next;
        vim_e_iter_line(e, &end, l1);
        vim_iter_line_end(&end);
        next = end;
        gtk_text_iter_forward_char(&next);
        gboolean ends_space = FALSE;
        if (!gtk_text_iter_starts_line(&end)) {
            GtkTextIter prev = end;
            gtk_text_iter_backward_char(&prev);
            ends_space = vim_e_is_blank_char(gtk_text_iter_get_char(&prev));
        }
        if (spaces) {
            while (!gtk_text_iter_ends_line(&next) && vim_e_is_blank_char(gtk_text_iter_get_char(&next))) {
                gtk_text_iter_forward_char(&next);
            }
        }
        gboolean next_empty = gtk_text_iter_ends_line(&next);
        gunichar nc = gtk_text_iter_get_char(&next);
        gint pos = vim_e_off(&end);
        vim_e_delete(e, &end, &next);
        /* One space between the lines, none after white space, before ')'
         * or when either side is empty; the cursor goes to the join */
        gboolean add_space = spaces && !ends_space && !next_empty && nc != ')' &&
                             !gtk_text_iter_starts_line(&end);
        if (add_space) vim_e_insert_text(e, &end, " ");
        vim_e_iter_at(e, &cursor, pos);
        if (!add_space && gtk_text_iter_ends_line(&cursor) && !gtk_text_iter_starts_line(&cursor)) {
            gtk_text_iter_backward_char(&cursor);
        }
        moved = TRUE;
    }
    if (moved) {
        vim_iter_clamp_normal(&cursor);
        vim_e_set_cursor(e, &cursor);
        vim_e_remember_column(e);
    }
}

void vim_e_align_lines(VimEngine* e, gint l1, gint l2, gchar how, gint width) {
    for (gint l = l1; l <= l2 && l <= vim_e_last_line(e); l++) {
        gchar* text = vim_e_line_text(e, l);
        gchar* trimmed = g_strstrip(g_strdup(text));
        glong len = g_utf8_strlen(trimmed, -1);
        gint ind = 0;
        if (how == 'c') ind = (gint)MAX(0, (width - len) / 2);
        else if (how == 'r') ind = (gint)MAX(0, width - len);
        else ind = width; /* :left [indent] */
        GtkTextIter s, t;
        vim_e_iter_line(e, &s, l);
        t = s;
        vim_iter_line_end(&t);
        vim_e_delete(e, &s, &t);
        if (trimmed[0] != '\0') {
            GString* line = g_string_new(NULL);
            for (gint i = 0; i < ind; i++) g_string_append_c(line, ' ');
            g_string_append(line, trimmed);
            vim_e_insert_text(e, &s, line->str);
            g_string_free(line, TRUE);
        }
        g_free(trimmed);
        g_free(text);
    }
}

/* = : indent from the previous non-blank line and the brackets */
static void reindent_lines(VimEngine* e, gint l1, gint l2) {
    gint sw = e->opt.shiftwidth > 0 ? e->opt.shiftwidth : e->opt.tabstop;
    for (gint l = l1; l <= l2 && l <= vim_e_last_line(e); l++) {
        if (vim_line_is_blank(e->buf, l)) {
            vim_e_set_line_indent(e, l, 0);
            continue;
        }
        gint p = l - 1;
        while (p >= 0 && vim_line_is_blank(e->buf, p)) p--;
        gint ind = 0;
        if (p >= 0) {
            ind = vim_line_indent(e->buf, p, e->opt.tabstop);
            gchar* prev = g_strchomp(vim_e_line_text(e, p));
            gsize n = strlen(prev);
            if (n > 0 && strchr("{([", prev[n - 1]) != NULL) ind += sw;
            g_free(prev);
        }
        gchar* cur = vim_e_line_text(e, l);
        gchar* c = cur;
        while (*c == ' ' || *c == '\t') c++;
        if (*c == '}' || *c == ')' || *c == ']') ind -= sw;
        g_free(cur);
        vim_e_set_line_indent(e, l, MAX(0, ind));
    }
}

/* gq : wrap the paragraphs of lines l1..l2 at textwidth; returns the last line */
static gint format_lines(VimEngine* e, gint l1, gint l2) {
    gint width = e->opt.textwidth > 0 ? e->opt.textwidth : 79;
    gint l = l1;
    while (l <= l2 && l <= vim_e_last_line(e)) {
        if (vim_line_is_blank(e->buf, l)) {
            l++;
            continue;
        }
        gint pe = l;
        while (pe + 1 <= l2 && !vim_line_is_blank(e->buf, pe + 1)) pe++;
        gchar* indent = vim_e_line_indent_text(e, l);
        gint ind_w = vim_line_indent(e->buf, l, e->opt.tabstop);
        GString* words = g_string_new(NULL);
        for (gint i = l; i <= pe; i++) {
            gchar* t = vim_e_line_text(e, i);
            if (words->len > 0) g_string_append_c(words, ' ');
            g_string_append(words, t);
            g_free(t);
        }
        gchar** parts = g_regex_split_simple("[ \\t]+", g_strstrip(words->str), 0, 0);
        GString* out = g_string_new(NULL);
        gint col = 0;
        gint nlines = 1;
        for (gint i = 0; parts[i] != NULL; i++) {
            if (parts[i][0] == '\0') continue;
            gint wl = (gint)g_utf8_strlen(parts[i], -1);
            if (col == 0) {
                g_string_append(out, indent);
                g_string_append(out, parts[i]);
                col = ind_w + wl;
            } else if (col + 1 + wl > width) {
                g_string_append_c(out, '\n');
                g_string_append(out, indent);
                g_string_append(out, parts[i]);
                col = ind_w + wl;
                nlines++;
            } else {
                g_string_append_c(out, ' ');
                g_string_append(out, parts[i]);
                col += 1 + wl;
            }
        }
        GtkTextIter s, t;
        vim_e_iter_line(e, &s, l);
        vim_e_iter_line(e, &t, pe);
        vim_iter_line_end(&t);
        vim_e_delete(e, &s, &t);
        vim_e_insert_text(e, &s, out->str);
        gint delta = nlines - (pe - l + 1);
        l2 += delta;
        l = pe + delta + 1;
        g_string_free(out, TRUE);
        g_strfreev(parts);
        g_string_free(words, TRUE);
        g_free(indent);
    }
    return MIN(l2, vim_e_last_line(e));
}

static gunichar rot13(gunichar c) {
    if (c >= 'a' && c <= 'z') return 'a' + (c - 'a' + 13) % 26;
    if (c >= 'A' && c <= 'Z') return 'A' + (c - 'A' + 13) % 26;
    return c;
}

void vim_e_transform_span(VimEngine* e, gint a, gint b, gunichar how) {
    if (b <= a) return;
    gchar* text = vim_e_slice(e, a, b);
    GString* out = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        switch (how) {
            case 'u': c = g_unichar_tolower(c); break;
            case 'U': c = g_unichar_toupper(c); break;
            case '?': c = rot13(c); break;
            default:
                if (g_unichar_isupper(c)) c = g_unichar_tolower(c);
                else if (g_unichar_islower(c)) c = g_unichar_toupper(c);
                break;
        }
        g_string_append_unichar(out, c);
    }
    if (strcmp(out->str, text) != 0) {
        GtkTextIter s, t;
        vim_e_iter_at(e, &s, a);
        vim_e_iter_at(e, &t, b);
        vim_e_delete(e, &s, &t);
        vim_e_insert_text(e, &s, out->str);
    }
    g_string_free(out, TRUE);
    g_free(text);
}

/* ------------------------------------------------------------------------- */
/* Operators                                                                 */
/* ------------------------------------------------------------------------- */

void vim_e_range_iters(VimEngine* e, const VimRange* r, GtkTextIter* s, GtkTextIter* t) {
    if (r->type == REG_LINE) {
        vim_e_iter_line(e, s, r->line1);
        if (r->line2 >= vim_e_last_line(e)) gtk_text_buffer_get_end_iter(e->buf, t);
        else vim_e_iter_line(e, t, r->line2 + 1);
    } else {
        vim_e_iter_at(e, s, r->start);
        vim_e_iter_at(e, t, r->end);
    }
}

static gchar* block_text(VimEngine* e, const VimRange* r) {
    GString* out = g_string_new(NULL);
    for (gint l = r->line1; l <= r->line2; l++) {
        GtkTextIter a, b;
        if (vim_e_block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) {
            gchar* t = gtk_text_iter_get_slice(&a, &b);
            g_string_append(out, t);
            g_free(t);
        }
        if (l < r->line2) g_string_append_c(out, '\n');
    }
    return g_string_free(out, FALSE);
}

void vim_e_delete_block(VimEngine* e, const VimRange* r) {
    for (gint l = r->line2; l >= r->line1; l--) {
        GtkTextIter a, b;
        if (vim_e_block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) vim_e_delete(e, &a, &b);
    }
}

void vim_e_yank_range(VimEngine* e, const VimRange* r, gunichar reg, RegKind kind) {
    gchar* text;
    if (r->type == REG_BLOCK) {
        text = block_text(e, r);
    } else if (r->type == REG_LINE) {
        text = lines_text(e, r->line1, MIN(r->line2, vim_e_last_line(e)));
    } else {
        text = vim_e_slice(e, r->start, r->end);
    }
    vim_e_reg_set(e, reg, text, r->type, kind);
    g_free(text);
}

static void report_lines(VimEngine* e, gint n, const gchar* what) {
    if (n > 2) vim_e_message(e, FALSE, "%d lines %s", n, what);
}

/* Run operator op on range r. Returns FALSE when it entered Insert mode. */
void vim_e_apply_operator(VimEngine* e, const VimCmd* cmd, VimRange* r, gboolean numbered) {
    gunichar o0 = cmd->op[0], o1 = cmd->op[1];
    gunichar reg = cmd->reg;
    GtkTextIter s, t;
    vim_e_range_iters(e, r, &s, &t);
    vim_e_set_mark(e, '[', &s);
    vim_e_set_mark(e, ']', &t);
    gint nlines = r->line2 - r->line1 + 1;

    if (o0 == 'y') {
        vim_e_yank_range(e, r, reg, REG_YANK);
        if (r->type == REG_BLOCK) {
            GtkTextIter it;
            vim_e_iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
        } else if (r->type == REG_LINE) {
            if (vim_e_cursor_line(e) > r->line1) {
                GtkTextIter it;
                vim_e_iter_line(e, &it, r->line1);
                vim_iter_set_vcol(&it, e->curswant, e->opt.tabstop);
                vim_e_set_cursor(e, &it);
            }
        } else {
            vim_e_set_cursor(e, &s);
        }
        if (r->type == REG_BLOCK) vim_e_message(e, FALSE, "block of %d lines yanked", nlines);
        else if (r->type == REG_LINE) report_lines(e, nlines, "yanked");
        return;
    }
    if (o0 == 'd' || o0 == 'c') {
        gboolean change = (o0 == 'c');
        if (r->type == REG_BLOCK) {
            vim_e_yank_range(e, r, reg, REG_DELETE);
            vim_e_delete_block(e, r);
            GtkTextIter it;
            vim_e_iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
            if (change) {
                e->blk_active = TRUE;
                e->blk_first = r->line1;
                e->blk_last = r->line2;
                e->blk_vcol = r->vcol1;
                e->blk_append = FALSE;
                e->blk_eol = FALSE;
                vim_e_start_insert(e, 'c', 1, VIM_MODE_INSERT);
            }
            return;
        }
        if (r->type == REG_LINE) {
            if (change) {
                vim_e_yank_range(e, r, reg, REG_DELETE);
                gchar* ind = e->opt.autoindent ? vim_e_line_indent_text(e, r->line1) : g_strdup("");
                GtkTextIter a, b;
                vim_e_iter_line(e, &a, r->line1);
                vim_e_iter_line(e, &b, MIN(r->line2, vim_e_last_line(e)));
                vim_iter_line_end(&b);
                vim_e_delete(e, &a, &b);
                vim_e_insert_text(e, &a, ind);
                gtk_text_buffer_place_cursor(e->buf, &a);
                g_free(ind);
                vim_e_start_insert(e, 'c', 1, VIM_MODE_INSERT);
            } else {
                vim_e_delete_lines(e, r->line1, r->line2, reg, TRUE);
                report_lines(e, nlines, "fewer");
            }
            return;
        }
        gchar* text = vim_e_slice(e, r->start, r->end);
        RegKind kind = REG_DELETE;
        gboolean multi = strchr(text, '\n') != NULL;
        if (numbered && !multi && (reg == 0 || reg == '"')) {
            /* %, n, `... deletes go to "1 even within a line */
            vim_e_reg_set(e, reg, text, REG_CHAR, kind);
            g_free(e->regs['-'].text);
            e->regs['-'].text = NULL;
            for (gunichar k = '9'; k > '1'; k--) {
                g_free(e->regs[k].text);
                e->regs[k] = e->regs[k - 1];
                e->regs[k - 1].text = NULL;
            }
            vim_e_reg_store(e, '1', text, REG_CHAR);
        } else {
            vim_e_reg_set(e, reg, text, REG_CHAR, kind);
        }
        g_free(text);
        vim_e_delete(e, &s, &t);
        vim_e_set_mark(e, '.', &s);
        gtk_text_buffer_place_cursor(e->buf, &s);
        if (change) {
            vim_e_start_insert(e, 'c', 1, VIM_MODE_INSERT);
        } else {
            vim_e_clamp_cursor(e);
        }
        vim_e_remember_column(e);
        return;
    }
    if (o0 == '<' || o0 == '>') {
        gint times = (cmd->kind == VIM_CMD_VISUAL_OP) ? vim_cmd_count1(cmd) : 1;
        vim_e_shift_lines(e, r->line1, r->line2, o0 == '>' ? times : -times);
        vim_e_set_cursor_line(e, r->line1, TRUE);
        if (nlines > 2) vim_e_message(e, FALSE, "%d lines %ced %d time%s", nlines, (gchar)o0, times,
                                      times > 1 ? "s" : "");
        return;
    }
    if (o0 == '=') {
        reindent_lines(e, r->line1, r->line2);
        vim_e_set_cursor_line(e, r->line1, TRUE);
        if (nlines > 2) vim_e_message(e, FALSE, "%d lines indented", nlines);
        return;
    }
    if (o0 == 'g' && (o1 == '~' || o1 == 'u' || o1 == 'U' || o1 == '?')) {
        if (r->type == REG_BLOCK) {
            for (gint l = r->line1; l <= r->line2; l++) {
                GtkTextIter a, b;
                if (vim_e_block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) vim_e_transform_span(e, vim_e_off(&a), vim_e_off(&b), o1);
            }
            GtkTextIter it;
            vim_e_iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
        } else {
            gint a = vim_e_off(&s), b = vim_e_off(&t);
            GtkTextIter keep;
            vim_e_cursor(e, &keep);
            gint keep_off = vim_e_off(&keep);
            vim_e_transform_span(e, a, b, o1);
            /* g~~ keeps the cursor; g~motion goes to the start */
            GtkTextIter it;
            vim_e_iter_at(e, &it, cmd->kind == VIM_CMD_OPERATOR_LINES ? keep_off : a);
            vim_iter_clamp_normal(&it);
            vim_e_set_cursor(e, &it);
        }
        return;
    }
    if (o0 == 'g' && (o1 == 'q' || o1 == 'w')) {
        GtkTextIter keep;
        vim_e_cursor(e, &keep);
        gint keep_off = vim_e_off(&keep);
        gint last = format_lines(e, r->line1, r->line2);
        if (o1 == 'q') {
            vim_e_set_cursor_line(e, last, TRUE);
        } else {
            GtkTextIter it;
            vim_e_iter_at(e, &it, MIN(keep_off, gtk_text_buffer_get_char_count(e->buf)));
            vim_iter_clamp_normal(&it);
            vim_e_set_cursor(e, &it);
        }
        return;
    }
}

/* Range of an operator from the cursor to a motion */
gboolean vim_e_motion_range(VimEngine* e, const VimCmd* cmd, const Motion* m, const GtkTextIter* cursor,
                             VimRange* r) {
    GtkTextIter a = *cursor, b = m->pos;
    if (gtk_text_iter_compare(&a, &b) > 0) {
        GtkTextIter tmp = a;
        a = b;
        b = tmp;
    }
    gboolean linewise = m->linewise;
    gboolean inclusive = m->inclusive;
    if (cmd->force == 'V') linewise = TRUE;
    else if (cmd->force == 'v') {
        if (linewise) {
            linewise = FALSE;
            inclusive = FALSE;
        } else {
            inclusive = !inclusive;
        }
    }
    memset(r, 0, sizeof(*r));
    if (cmd->force == VIM_KEY_CTRL('v')) {
        gint ts = e->opt.tabstop;
        r->type = REG_BLOCK;
        r->line1 = gtk_text_iter_get_line(&a);
        r->line2 = gtk_text_iter_get_line(&b);
        gint va = vim_iter_vcol(cursor, ts), vb = vim_iter_vcol(&m->pos, ts);
        r->vcol1 = MIN(va, vb);
        r->vcol2 = MAX(va, vb);
        if (!inclusive && r->vcol2 > r->vcol1) r->vcol2--;
        return TRUE;
    }
    if (!linewise) {
        if (inclusive && !gtk_text_iter_is_end(&b) && !gtk_text_iter_ends_line(&b)) {
            gtk_text_iter_forward_char(&b);
        } else if (inclusive && gtk_text_iter_ends_line(&b) && !gtk_text_iter_is_end(&b) &&
                   cmd->force == 'v') {
            gtk_text_iter_forward_char(&b);
        }
        /* An exclusive motion ending in column 0 stops at the previous line
         * end, and becomes linewise when it started before the text */
        if (!inclusive && cmd->force == 0 && gtk_text_iter_starts_line(&b) &&
            gtk_text_iter_get_line(&b) > gtk_text_iter_get_line(&a) && !gtk_text_iter_is_end(&b)) {
            GtkTextIter nb = a;
            vim_iter_first_nonblank(&nb);
            gtk_text_iter_backward_char(&b);
            if (gtk_text_iter_compare(&a, &nb) <= 0 && cmd->key[0] != 'w' && cmd->key[0] != 'W') {
                linewise = TRUE;
            }
        }
    }
    if (linewise) {
        r->type = REG_LINE;
        r->line1 = gtk_text_iter_get_line(&a);
        r->line2 = gtk_text_iter_get_line(&b);
        GtkTextIter s, t;
        vim_e_iter_line(e, &s, r->line1);
        t = s;
        r->start = vim_e_off(&s);
        vim_e_range_iters(e, r, &s, &t);
        r->end = vim_e_off(&t);
    } else {
        r->type = REG_CHAR;
        r->start = vim_e_off(&a);
        r->end = vim_e_off(&b);
        r->line1 = gtk_text_iter_get_line(&a);
        r->line2 = gtk_text_iter_get_line(&b);
    }
    return TRUE;
}

void vim_e_exec_operator(VimEngine* e, const VimCmd* cmd_in) {
    VimCmd cmd = *cmd_in;
    GtkTextIter cursor;
    vim_e_cursor(e, &cursor);
    VimRange r;
    gboolean numbered = FALSE;
    if (cmd.key[0] == 'i' || cmd.key[0] == 'a') {
        GtkTextIter s = cursor, t = cursor;
        gboolean lw = FALSE;
        if (!vim_textobj(e->buf, cmd.key[1], cmd.key[0] == 'a', vim_cmd_count1(&cmd), &s, &t, &lw) ||
            gtk_text_iter_compare(&s, &t) > 0) {
            vim_e_beep(e);
            return;
        }
        memset(&r, 0, sizeof(r));
        r.start = vim_e_off(&s);
        r.end = vim_e_off(&t);
        r.line1 = gtk_text_iter_get_line(&s);
        GtkTextIter last = t;
        if (gtk_text_iter_compare(&t, &s) > 0) gtk_text_iter_backward_char(&last);
        r.line2 = gtk_text_iter_get_line(&last);
        r.type = lw ? REG_LINE : REG_CHAR;
        if (lw && gtk_text_iter_is_end(&t) && !gtk_text_iter_starts_line(&t)) r.line2 = gtk_text_iter_get_line(&t);
        if (r.start == r.end && cmd.op[0] != 'c') return;
    } else {
        /* cw / cW on a word act like ce / cE */
        if (cmd.op[0] == 'c' && (cmd.key[0] == 'w' || cmd.key[0] == 'W') && cmd.key[1] == 0 &&
            !gtk_text_iter_ends_line(&cursor) && !vim_e_is_blank_char(gtk_text_iter_get_char(&cursor))) {
            GtkTextIter p = cursor;
            gint n = vim_cmd_count1(&cmd);
            gboolean big = cmd.key[0] == 'W';
            /* On the last character of a word, the first step stays there */
            GtkTextIter next = p;
            gtk_text_iter_forward_char(&next);
            gboolean at_end = gtk_text_iter_ends_line(&next) ||
                              vim_char_class(gtk_text_iter_get_char(&next), big) !=
                              vim_char_class(gtk_text_iter_get_char(&p), big);
            if (!at_end || n > 1) vim_motion_word_end(&p, at_end ? n - 1 : n, big);
            Motion m = {0};
            m.pos = p;
            m.inclusive = TRUE;
            vim_e_motion_range(e, &cmd, &m, &cursor, &r);
        } else {
            Motion m;
            if (!vim_e_do_motion(e, &cmd, TRUE, &cursor, &m)) {
                vim_e_beep(e);
                return;
            }
            if (m.jump) vim_e_push_jump(e);
            numbered = m.numbered;
            vim_e_motion_range(e, &cmd, &m, &cursor, &r);
            if (!m.linewise && !m.inclusive && r.type == REG_CHAR && r.start == r.end && cmd.op[0] != 'c') {
                return;
            }
        }
    }
    vim_e_apply_operator(e, &cmd, &r, numbered);
}

void vim_e_exec_operator_lines(VimEngine* e, const VimCmd* cmd) {
    gint line = vim_e_cursor_line(e);
    gint n = vim_cmd_count1(cmd);
    gint last = vim_e_last_line(e);
    if (line + n - 1 > last && cmd->op[0] != 'd' && cmd->op[0] != 'y' && cmd->op[0] != 'c') {
        n = last - line + 1;
    }
    VimRange r = {0};
    r.type = REG_LINE;
    r.line1 = line;
    r.line2 = MIN(line + n - 1, last);
    GtkTextIter s, t;
    vim_e_range_iters(e, &r, &s, &t);
    r.start = vim_e_off(&s);
    r.end = vim_e_off(&t);
    vim_e_apply_operator(e, cmd, &r, FALSE);
}
