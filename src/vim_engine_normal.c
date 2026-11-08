/* Vim engine: put, and the Normal mode commands (see vim_engine_private.h) */
#include "vim_engine_private.h"
#include "vim_keys.h"
#include "vim_motion.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Put                                                                       */
/* ------------------------------------------------------------------------- */

static void put_register(VimEngine* e, gunichar reg, gint count, gboolean before, gboolean gp) {
    VimRegister r;
    if (!vim_e_reg_get(e, reg, &r) || r.text == NULL) {
        vim_e_message(e, TRUE, "E353: Nothing in register %s",
                      reg == 0 ? "\"" : (gchar[]){(gchar)reg, 0});
        return;
    }
    if (count < 1) count = 1;
    gchar* text = g_strdup(r.text);
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    gint ts = e->opt.tabstop;
    if (r.type == REG_LINE) {
        GString* all = g_string_new(NULL);
        for (gint i = 0; i < count; i++) g_string_append(all, text);
        if (!g_str_has_suffix(all->str, "\n")) g_string_append_c(all, '\n');
        gint line = gtk_text_iter_get_line(&cur);
        gint first = before ? line : line + 1;
        vim_e_put_lines(e, before ? line - 1 : line, all->str);
        gint nl = 0;
        for (const gchar* p = all->str; *p; p++) if (*p == '\n') nl++;
        GtkTextIter a;
        vim_e_iter_line(e, &a, first);
        vim_e_set_mark(e, '[', &a);
        GtkTextIter b;
        vim_e_iter_line(e, &b, first + nl - 1);
        vim_e_set_mark(e, ']', &b);
        if (gp) vim_e_set_cursor_line(e, first + nl, FALSE);
        else vim_e_set_cursor_line(e, first, TRUE);
        if (nl > 2) vim_e_message(e, FALSE, "%d more lines", nl);
        g_string_free(all, TRUE);
    } else if (r.type == REG_BLOCK) {
        gchar** rows = g_strsplit(text, "\n", -1);
        gint vcol = vim_iter_vcol(&cur, ts);
        if (!before && !gtk_text_iter_ends_line(&cur)) vcol = vim_iter_vcol_end(&cur, ts);
        gint line = gtk_text_iter_get_line(&cur);
        gint width = 0;
        for (gint i = 0; rows[i] != NULL; i++) width = MAX(width, (gint)g_utf8_strlen(rows[i], -1));
        for (gint i = 0; rows[i] != NULL; i++) {
            gint l = line + i;
            if (l > vim_e_last_line(e)) {
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(e->buf, &end);
                vim_e_insert_text(e, &end, "\n");
            }
            GtkTextIter it;
            vim_e_iter_line(e, &it, l);
            vim_iter_set_vcol(&it, vcol, ts);
            gint v = vim_iter_vcol(&it, ts);
            GString* piece = g_string_new(NULL);
            for (gint k = v; k < vcol; k++) g_string_append_c(piece, ' ');
            for (gint c = 0; c < count; c++) {
                g_string_append(piece, rows[i]);
                if (!gtk_text_iter_ends_line(&it) || c < count - 1) {
                    for (glong k = g_utf8_strlen(rows[i], -1); k < width; k++) g_string_append_c(piece, ' ');
                }
            }
            if (gtk_text_iter_ends_line(&it)) g_strchomp(piece->str), piece->len = strlen(piece->str);
            vim_e_insert_text(e, &it, piece->str);
            g_string_free(piece, TRUE);
        }
        g_strfreev(rows);
        GtkTextIter it;
        vim_e_iter_line(e, &it, line);
        vim_iter_set_vcol(&it, vcol, ts);
        vim_iter_clamp_normal(&it);
        vim_e_set_cursor(e, &it);
    } else {
        GString* all = g_string_new(NULL);
        for (gint i = 0; i < count; i++) g_string_append(all, text);
        GtkTextIter at = cur;
        if (!before && !gtk_text_iter_ends_line(&at)) gtk_text_iter_forward_char(&at);
        gint start = vim_e_off(&at);
        vim_e_insert_text(e, &at, all->str);
        gint end = vim_e_off(&at);
        GtkTextIter a, b;
        vim_e_iter_at(e, &a, start);
        vim_e_iter_at(e, &b, end > start ? end - 1 : end);
        vim_e_set_mark(e, '[', &a);
        vim_e_set_mark(e, ']', &b);
        GtkTextIter c;
        if (gp) {
            vim_e_iter_at(e, &c, end);
        } else if (strchr(all->str, '\n') != NULL) {
            c = a;
        } else {
            c = b;
        }
        if (!gp) vim_iter_clamp_normal(&c);
        vim_e_set_cursor(e, &c);
        vim_e_remember_column(e);
        g_string_free(all, TRUE);
    }
    g_free(text);
}

/* Visual p / P: replace the selection with a register */
void vim_e_put_over_selection(VimEngine* e, gunichar reg, gint count, gboolean keep_register) {
    VimRegister r;
    if (!vim_e_reg_get(e, reg, &r) || r.text == NULL) {
        vim_e_exit_visual(e);
        vim_e_message(e, TRUE, "E353: Nothing in register");
        return;
    }
    gchar* text = g_strdup(r.text);
    RegType type = r.type;
    VimRange vr = vim_e_visual_range(e);
    vim_e_exit_visual(e);
    GtkTextIter s, t;
    vim_e_range_iters(e, &vr, &s, &t);
    /* The replaced text goes to the unnamed register (not with P) */
    if (vr.type == REG_LINE) {
        if (!keep_register) vim_e_yank_range(e, &vr, 0, REG_DELETE);
        gint line = vr.line1;
        gboolean at_end = vr.line2 >= vim_e_last_line(e);
        vim_e_delete_lines(e, vr.line1, vr.line2, '_', FALSE);
        gchar* lines = type == REG_LINE ? g_strdup(text) : g_strconcat(text, "\n", NULL);
        GString* all = g_string_new(NULL);
        for (gint i = 0; i < MAX(count, 1); i++) g_string_append(all, lines);
        gboolean empty_buffer = gtk_text_buffer_get_char_count(e->buf) == 0;
        if (empty_buffer) {
            GtkTextIter it;
            gtk_text_buffer_get_start_iter(e->buf, &it);
            g_string_truncate(all, all->len - 1);
            vim_e_insert_text(e, &it, all->str);
        } else {
            vim_e_put_lines(e, at_end && line > vim_e_last_line(e) ? vim_e_last_line(e) : line - 1, all->str);
        }
        vim_e_set_cursor_line(e, line, TRUE);
        g_string_free(all, TRUE);
        g_free(lines);
    } else if (vr.type == REG_BLOCK) {
        if (!keep_register) vim_e_yank_range(e, &vr, 0, REG_DELETE);
        vim_e_delete_block(e, &vr);
        GtkTextIter it;
        vim_e_iter_line(e, &it, vr.line1);
        vim_iter_set_vcol(&it, vr.vcol1, e->opt.tabstop);
        gtk_text_buffer_place_cursor(e->buf, &it);
        /* Put through a scratch slot so the unnamed register keeps the deleted block */
        vim_e_reg_store(e, 1, text, type);
        put_register(e, 1, count, TRUE, FALSE);
        vim_e_reg_store(e, 1, NULL, REG_CHAR);
    } else {
        if (!keep_register) vim_e_yank_range(e, &vr, 0, REG_DELETE);
        vim_e_delete(e, &s, &t);
        gtk_text_buffer_place_cursor(e->buf, &s);
        if (type == REG_LINE) {
            gchar* body = g_strconcat("\n", text, NULL);
            vim_e_insert_text(e, &s, body);
            g_free(body);
            vim_e_set_cursor_line(e, vr.line1 + 1, TRUE);
        } else {
            GString* all = g_string_new(NULL);
            for (gint i = 0; i < MAX(count, 1); i++) g_string_append(all, text);
            gint start = vim_e_off(&s);
            vim_e_insert_text(e, &s, all->str);
            GtkTextIter c;
            vim_e_iter_at(e, &c, vim_e_off(&s) > start ? vim_e_off(&s) - 1 : start);
            vim_iter_clamp_normal(&c);
            vim_e_set_cursor(e, &c);
            g_string_free(all, TRUE);
        }
    }
    g_free(text);
}

/* ------------------------------------------------------------------------- */
/* Normal mode commands                                                      */
/* ------------------------------------------------------------------------- */

static gboolean replace_chars(VimEngine* e, gint count, gunichar c) {
    GtkTextIter s, t;
    vim_e_cursor(e, &s);
    t = s;
    for (gint i = 0; i < count; i++) {
        if (gtk_text_iter_ends_line(&t)) return FALSE;
        gtk_text_iter_forward_char(&t);
    }
    if (c == VIM_KEY_CR || c == VIM_KEY_NL) {
        vim_e_delete(e, &s, &t);
        gtk_text_buffer_place_cursor(e->buf, &s);
        vim_e_insert_newline(e, TRUE);
        return TRUE;
    }
    gchar buf[8] = {0};
    g_unichar_to_utf8(c, buf);
    GString* rep = g_string_new(NULL);
    for (gint i = 0; i < count; i++) g_string_append(rep, buf);
    gint start = vim_e_off(&s);
    vim_e_delete(e, &s, &t);
    vim_e_insert_text(e, &s, rep->str);
    g_string_free(rep, TRUE);
    GtkTextIter c2;
    vim_e_iter_at(e, &c2, start + count - 1);
    vim_e_set_cursor(e, &c2);
    return TRUE;
}

/* Ctrl-A / Ctrl-X on the number at or after the cursor, in [from, limit) */
gboolean vim_e_increment_at(VimEngine* e, GtkTextIter* from, gint limit_off, gint64 delta) {
    GtkTextIter p = *from;
    /* Back to the start of a number the cursor is in */
    while (!gtk_text_iter_starts_line(&p)) {
        GtkTextIter prev = p;
        gtk_text_iter_backward_char(&prev);
        gunichar c = gtk_text_iter_get_char(&prev);
        if (!g_ascii_isxdigit(c) && c != 'x' && c != 'X') break;
        if (!g_ascii_isdigit(gtk_text_iter_get_char(&p)) && !g_ascii_isxdigit(gtk_text_iter_get_char(&p))) break;
        p = prev;
    }
    while (!gtk_text_iter_ends_line(&p) && vim_e_off(&p) < limit_off && !g_ascii_isdigit(gtk_text_iter_get_char(&p))) {
        gtk_text_iter_forward_char(&p);
    }
    if (gtk_text_iter_ends_line(&p) || vim_e_off(&p) >= limit_off) return FALSE;
    GtkTextIter s = p, t = p;
    gboolean hex = FALSE;
    if (gtk_text_iter_get_char(&p) == '0') {
        GtkTextIter x = p;
        gtk_text_iter_forward_char(&x);
        gunichar xc = gtk_text_iter_get_char(&x);
        GtkTextIter d = x;
        gtk_text_iter_forward_char(&d);
        if ((xc == 'x' || xc == 'X') && g_ascii_isxdigit(gtk_text_iter_get_char(&d))) {
            hex = TRUE;
            t = d;
        }
    }
    if (hex) {
        while (!gtk_text_iter_ends_line(&t) && g_ascii_isxdigit(gtk_text_iter_get_char(&t))) gtk_text_iter_forward_char(&t);
    } else {
        while (!gtk_text_iter_ends_line(&t) && g_ascii_isdigit(gtk_text_iter_get_char(&t))) gtk_text_iter_forward_char(&t);
        GtkTextIter minus = s;
        if (!gtk_text_iter_starts_line(&minus) && gtk_text_iter_backward_char(&minus) &&
            gtk_text_iter_get_char(&minus) == '-') {
            s = minus;
        }
    }
    gchar* text = gtk_text_iter_get_slice(&s, &t);
    gchar* out;
    if (hex) {
        const gchar* digits = text + 2;
        guint64 v = g_ascii_strtoull(digits, NULL, 16);
        v += (guint64)delta;
        gboolean upper = FALSE;
        for (const gchar* q = digits; *q; q++) if (*q >= 'A' && *q <= 'F') upper = TRUE;
        out = g_strdup_printf(upper ? "%.2s%0*" G_GINT64_MODIFIER "X" : "%.2s%0*" G_GINT64_MODIFIER "x",
                              text, (gint)strlen(digits), v);
    } else {
        gint64 v = g_ascii_strtoll(text, NULL, 10);
        gboolean neg = text[0] == '-';
        const gchar* digits = neg ? text + 1 : text;
        gint width = (digits[0] == '0' && strlen(digits) > 1) ? (gint)strlen(digits) : 0;
        v += delta;
        if (width > 0) {
            out = g_strdup_printf("%s%0*" G_GINT64_FORMAT, v < 0 ? "-" : "", width, v < 0 ? -v : v);
        } else {
            out = g_strdup_printf("%" G_GINT64_FORMAT, v);
        }
    }
    gint start = vim_e_off(&s);
    vim_e_delete(e, &s, &t);
    vim_e_insert_text(e, &s, out);
    vim_e_iter_at(e, from, start + (gint)g_utf8_strlen(out, -1) - 1);
    g_free(out);
    g_free(text);
    return TRUE;
}

void vim_e_scroll_page(VimEngine* e, gunichar k, gint count) {
    gint first = 0, last = vim_e_last_line(e);
    gboolean known = e->host.visible_lines != NULL && e->host.visible_lines(e->hd, &first, &last);
    gint page = MAX(1, last - first + 1);
    gint line = vim_e_cursor_line(e);
    gint target = line;
    gint scroll = 0;
    switch (k) {
        case VIM_KEY_CTRL('d'): scroll = count > 0 ? count : page / 2; target = line + scroll; break;
        case VIM_KEY_CTRL('u'): scroll = -(count > 0 ? count : page / 2); target = line + scroll; break;
        case VIM_KEY_CTRL('f'): scroll = MAX(1, page - 2) * MAX(count, 1); target = first + scroll; break;
        case VIM_KEY_CTRL('b'): scroll = -MAX(1, page - 2) * MAX(count, 1); target = last + scroll; break;
        case VIM_KEY_CTRL('e'): scroll = MAX(count, 1); target = MAX(line, first + scroll); break;
        case VIM_KEY_CTRL('y'): scroll = -MAX(count, 1); target = MIN(line, last + scroll); break;
        default: break;
    }
    if (e->host.scroll != NULL) e->host.scroll(e->hd, VIM_SCROLL_LINES, scroll);
    if (!known && (k == VIM_KEY_CTRL('e') || k == VIM_KEY_CTRL('y'))) return;
    target = CLAMP(target, 0, vim_e_last_line(e));
    if (target != line) {
        GtkTextIter it;
        vim_e_iter_line(e, &it, target);
        if (k == VIM_KEY_CTRL('e') || k == VIM_KEY_CTRL('y')) {
            vim_iter_set_vcol(&it, e->curswant, e->opt.tabstop);
        } else {
            vim_iter_first_nonblank(&it);
        }
        vim_iter_clamp_normal(&it);
        gtk_text_buffer_place_cursor(e->buf, &it);
        if (vim_e_is_visual(e->mode)) vim_e_update_visual(e);
    }
}

/* Apply one undo step backward (undo) or forward (redo); returns the
 * offset where the cursor goes (the start of the changed text) */
static gint apply_undo_step(VimEngine* e, VimUndoStep* st, gboolean redo) {
    gint first = G_MAXINT;
    e->undoing = TRUE;
    e->self_edit++;
    gint n = (gint)st->ops->len;
    for (gint k = 0; k < n; k++) {
        VimUndoOp* op = &g_array_index(st->ops, VimUndoOp, redo ? k : n - 1 - k);
        gboolean insert = redo ? op->insert : !op->insert;
        GtkTextIter a;
        vim_e_iter_at(e, &a, op->offset);
        if (insert) {
            gtk_text_buffer_insert(e->buf, &a, op->text, -1);
        } else {
            GtkTextIter b;
            vim_e_iter_at(e, &b, op->offset + op->chars);
            gtk_text_buffer_delete(e->buf, &a, &b);
        }
        first = MIN(first, op->offset);
    }
    e->self_edit--;
    e->undoing = FALSE;
    return first;
}

static void undo_redo(VimEngine* e, gint count, gboolean redo) {
    vim_e_end_change(e);
    vim_e_undo_close_step(e);
    gint done = 0;
    gint cursor = -1;
    gtk_text_buffer_begin_user_action(e->buf);
    for (gint i = 0; i < count; i++) {
        if (redo ? e->undo_pos >= (gint)e->undo_steps->len : e->undo_pos <= 0) break;
        VimUndoStep* st = g_ptr_array_index(e->undo_steps, redo ? e->undo_pos : e->undo_pos - 1);
        cursor = apply_undo_step(e, st, redo);
        e->undo_pos += redo ? 1 : -1;
        done++;
    }
    gtk_text_buffer_end_user_action(e->buf);
    if (done > 0) {
        e->undoing = TRUE;
        gtk_text_buffer_set_modified(e->buf, e->undo_pos != e->saved_pos);
        e->undoing = FALSE;
        GtkTextIter it;
        vim_e_iter_at(e, &it, cursor);
        vim_iter_clamp_normal(&it);
        gtk_text_buffer_place_cursor(e->buf, &it);
        vim_e_remember_column(e);
        vim_e_set_mark(e, '.', &it);
        vim_e_message(e, FALSE, "%d change%s; %s #%d", done, done > 1 ? "s" : "", redo ? "after" : "before",
                      e->undo_pos);
    } else {
        vim_e_message(e, TRUE, redo ? "Already at newest change" : "Already at oldest change");
    }
}

static void file_info(VimEngine* e) {
    const gchar* name = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
    gint lines = vim_e_last_line(e) + 1;
    gint line = vim_e_cursor_line(e) + 1;
    vim_e_message(e, FALSE, "\"%s\"%s %d line%s --%d%%--", name != NULL ? name : "[No Name]",
                  gtk_text_buffer_get_modified(e->buf) ? " [Modified]" : "", lines, lines == 1 ? "" : "s",
                  line * 100 / MAX(lines, 1));
}

static void word_count(VimEngine* e) {
    GtkTextIter s, t, cur;
    gtk_text_buffer_get_bounds(e->buf, &s, &t);
    vim_e_cursor(e, &cur);
    gchar* text = gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
    gint words = 0, word_at = 0;
    gboolean in = FALSE;
    gint idx = 0, cur_off = vim_e_off(&cur);
    for (const gchar* p = text; *p; p = g_utf8_next_char(p), idx++) {
        gboolean space = g_unichar_isspace(g_utf8_get_char(p));
        if (!space && !in) {
            words++;
            if (idx <= cur_off) word_at = words;
        }
        in = !space;
    }
    GtkTextIter le = cur;
    vim_iter_line_end(&le);
    vim_e_message(e, FALSE, "Col %d of %d; Line %d of %d; Word %d of %d; Char %d of %d",
                  gtk_text_iter_get_line_offset(&cur) + 1, gtk_text_iter_get_line_offset(&le),
                  gtk_text_iter_get_line(&cur) + 1, vim_e_last_line(e) + 1, word_at, words, cur_off + 1, idx);
    g_free(text);
}

static void show_char_info(VimEngine* e, gboolean utf8) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    if (gtk_text_iter_ends_line(&cur)) {
        vim_e_message(e, FALSE, "NUL");
        return;
    }
    gunichar c = gtk_text_iter_get_char(&cur);
    gchar buf[8] = {0};
    gint n = g_unichar_to_utf8(c, buf);
    if (utf8) {
        GString* s = g_string_new(NULL);
        for (gint i = 0; i < n; i++) g_string_append_printf(s, "%s%02x", i ? " " : "", (guchar)buf[i]);
        vim_e_message(e, FALSE, "%s", s->str);
        g_string_free(s, TRUE);
    } else {
        vim_e_message(e, FALSE, "<%s> %u, Hex %04x, Oct %o", buf, c, c, c);
    }
}

void vim_e_start_recording_dot(VimEngine* e, const VimCmd* cmd, const gchar* extra) {
    GString* s = g_string_new(NULL);
    if (cmd->kind == VIM_CMD_OPERATOR || cmd->kind == VIM_CMD_OPERATOR_LINES) {
        if (cmd->op[0]) g_string_append_unichar(s, cmd->op[0]);
        if (cmd->op[1]) g_string_append_unichar(s, cmd->op[1]);
        if (cmd->force) g_string_append_unichar(s, cmd->force);
        if (cmd->kind == VIM_CMD_OPERATOR_LINES) {
            g_string_append_unichar(s, cmd->op[1] ? cmd->op[1] : cmd->op[0]);
        }
    }
    if (cmd->kind != VIM_CMD_OPERATOR_LINES) {
        if (cmd->key[0]) g_string_append_unichar(s, cmd->key[0]);
        if (cmd->key[1]) g_string_append_unichar(s, cmd->key[1]);
        if (cmd->arg) g_string_append_unichar(s, cmd->arg);
    }
    if (extra != NULL) g_string_append(s, extra);
    if (vim_e_is_insert(e->mode)) {
        if (e->dot_pending != NULL) g_string_free(e->dot_pending, TRUE);
        e->dot_pending = s;
        e->dot_pending_visual = FALSE;
    } else {
        g_string_assign(e->dot, s->str);
        e->dot_visual = FALSE;
        g_string_free(s, TRUE);
    }
    e->dot_count = cmd->count;
    e->dot_reg = cmd->reg;
}

static void exec_dot(VimEngine* e, gint count) {
    if (e->dot->len == 0) {
        vim_e_beep(e);
        return;
    }
    gchar* keys = g_strdup(e->dot->str);
    if (e->dot_visual) {
        /* Same size of selection, from the cursor */
        GtkTextIter cur;
        vim_e_cursor(e, &cur);
        VimMode m = e->dot_vis_mode;
        vim_e_enter_visual(e, m);
        GtkTextIter end = cur;
        if (m == VIM_MODE_VISUAL && e->dot_vis_lines == 0) {
            for (gint i = 1; i < e->dot_vis_cols && !gtk_text_iter_ends_line(&end); i++) {
                gtk_text_iter_forward_char(&end);
            }
        } else {
            gint line = MIN(gtk_text_iter_get_line(&cur) + e->dot_vis_lines, vim_e_last_line(e));
            vim_e_iter_line(e, &end, line);
            if (m == VIM_MODE_VISUAL) {
                GtkTextIter le = end;
                vim_iter_line_end(&le);
                gtk_text_iter_set_line_offset(&end, MIN(e->dot_vis_cols, gtk_text_iter_get_line_offset(&le)));
            } else if (m == VIM_MODE_VISUAL_BLOCK) {
                vim_iter_set_vcol(&end, vim_iter_vcol(&cur, e->opt.tabstop) + e->dot_vis_cols - 1,
                                  e->opt.tabstop);
            }
        }
        gtk_text_buffer_place_cursor(e->buf, &end);
        vim_e_update_visual(e);
        gchar* saved = g_strdup(keys);
        gboolean dv = e->dot_visual;
        VimMode dm = e->dot_vis_mode;
        gint dl = e->dot_vis_lines, dc = e->dot_vis_cols;
        vim_e_feed_internal(e, keys);
        g_string_assign(e->dot, saved);
        e->dot_visual = dv;
        e->dot_vis_mode = dm;
        e->dot_vis_lines = dl;
        e->dot_vis_cols = dc;
        g_free(saved);
    } else {
        GString* s = g_string_new(NULL);
        if (e->dot_reg != 0) {
            g_string_append_c(s, '"');
            g_string_append_unichar(s, e->dot_reg);
        }
        gint c = count > 0 ? count : e->dot_count;
        if (c > 0) g_string_append_printf(s, "%d", c);
        g_string_append(s, keys);
        vim_e_feed_internal(e, s->str);
        g_string_free(s, TRUE);
    }
    g_free(keys);
}

static void exec_macro(VimEngine* e, gunichar reg, gint count) {
    if (reg == '@') reg = e->last_macro;
    if (reg == 0) {
        vim_e_message(e, TRUE, "E748: No previously used register");
        return;
    }
    if (reg == ':') {
        if (e->last_ex == NULL) {
            vim_e_message(e, TRUE, "E30: No previous command line");
            return;
        }
        e->last_macro = ':';
        gchar* cmd = g_strdup(e->last_ex);
        for (gint i = 0; i < MAX(count, 1); i++) vim_engine_ex(e, cmd);
        g_free(cmd);
        return;
    }
    VimRegister r;
    if (!vim_e_reg_get(e, reg, &r) || r.text == NULL) {
        vim_e_beep(e);
        return;
    }
    if (e->macro_depth > 50) {
        vim_e_message(e, TRUE, "E169: Command too recursive");
        return;
    }
    e->last_macro = reg;
    gchar* keys = g_strdup(r.text);
    e->macro_depth++;
    e->beeped = FALSE;
    for (gint i = 0; i < MAX(count, 1) && !e->beeped; i++) {
        const gchar* p = keys;
        e->replaying++;
        while (*p != '\0' && !e->beeped) {
            vim_engine_feed(e, g_utf8_get_char(p));
            p = g_utf8_next_char(p);
        }
        e->replaying--;
    }
    e->macro_depth--;
    g_free(keys);
}

void vim_e_open_cmdline_for(VimEngine* e, gunichar prompt, const VimCmd* op) {
    e->cl_has_op = op != NULL;
    if (op != NULL) e->cl_op = *op;
    vim_e_cmdline_open(e, prompt, "");
}

void vim_e_exec_simple(VimEngine* e, const VimCmd* cmd) {
    gunichar k = cmd->key[0], k1 = cmd->key[1];
    gint count = cmd->count;
    gint n = vim_cmd_count1(cmd);
    GtkTextIter cur;
    vim_e_cursor(e, &cur);

    if (k == 'g') {
        switch (k1) {
            case 'r':
                /* gr: references of the word under the cursor (host) */
                if (e->host.references == NULL || !e->host.references(e->hd)) vim_e_beep(e);
                return;
            case 'J': {
                gint line = gtk_text_iter_get_line(&cur);
                vim_e_join_lines(e, line, line + MAX(n, 2) - 1, FALSE);
                vim_e_start_recording_dot(e, cmd, NULL);
                return;
            }
            case 'p': case 'P':
                put_register(e, cmd->reg, n, k1 == 'P', TRUE);
                vim_e_start_recording_dot(e, cmd, NULL);
                return;
            case 'v': vim_e_reselect_visual(e); return;
            case 'i': {
                GtkTextIter m;
                if (vim_e_get_mark(e, '^', &m)) gtk_text_buffer_place_cursor(e->buf, &m);
                vim_e_start_insert(e, 'i', count, VIM_MODE_INSERT);
                vim_e_start_recording_dot(e, cmd, NULL);
                return;
            }
            case 'I':
                gtk_text_iter_set_line_offset(&cur, 0);
                gtk_text_buffer_place_cursor(e->buf, &cur);
                vim_e_start_insert(e, 'I', count, VIM_MODE_INSERT);
                vim_e_start_recording_dot(e, cmd, NULL);
                return;
            case '&': {
                vim_e_substitute_repeat(e, 0, vim_e_last_line(e), TRUE, TRUE);
                return;
            }
            case 't': case 'T':
                if (e->host.ex_command != NULL) {
                    e->host.ex_command(e->hd, k1 == 't' ? "tabnext" : "tabprevious", FALSE, "", NULL);
                }
                return;
            case 'a': show_char_info(e, FALSE); return;
            case '8': show_char_info(e, TRUE); return;
            case VIM_KEY_CTRL('g'): word_count(e); return;
            default: return;
        }
    }
    if (k == 'z') {
        VimScroll how = (k1 == 'z' || k1 == '.') ? VIM_SCROLL_CURSOR_CENTER
                      : (k1 == 't' || k1 == VIM_KEY_CR) ? VIM_SCROLL_CURSOR_TOP : VIM_SCROLL_CURSOR_BOTTOM;
        if (count > 0) vim_e_set_cursor_line(e, count - 1, FALSE);
        if (k1 == '.' || k1 == VIM_KEY_CR || k1 == '-') vim_e_set_cursor_line(e, vim_e_cursor_line(e), TRUE);
        if (e->host.scroll != NULL) e->host.scroll(e->hd, how, 0);
        return;
    }
    if (k == 'Z') {
        if (e->host.ex_command != NULL) {
            gchar* err = NULL;
            if (k1 == 'Z') e->host.ex_command(e->hd, "xit", FALSE, "", &err);
            else e->host.ex_command(e->hd, "quit", TRUE, "", &err);
            if (err != NULL) {
                vim_e_message(e, TRUE, "%s", err);
                g_free(err);
            }
        }
        return;
    }

    switch (k) {
        case 'K': {
            GtkTextIter ws, we;
            gboolean shown = FALSE;
            if (e->host.keyword_help != NULL && vim_e_word_under_cursor(e, &ws, &we)) {
                gchar* word = gtk_text_iter_get_slice(&ws, &we);
                shown = e->host.keyword_help(e->hd, word);
                g_free(word);
            }
            if (!shown) vim_e_beep(e);
            return;
        }
        case VIM_KEY_ESC: case VIM_KEY_CTRL('c'): case VIM_KEY_CTRL('l'):
            return;
        case 'x': case 'X': case 's': case 'S': case 'C': case 'D': case 'Y': {
            /* Shortcuts: x = dl, X = dh, s = cl, S = cc, C = c$, D = d$, Y = yy */
            VimCmd c = *cmd;
            c.kind = VIM_CMD_OPERATOR;
            c.key[1] = 0;
            c.arg = 0;
            switch (k) {
                case 'x': c.op[0] = 'd'; c.key[0] = 'l'; break;
                case 'X': c.op[0] = 'd'; c.key[0] = 'h'; break;
                case 's': c.op[0] = 'c'; c.key[0] = 'l'; break;
                case 'C': c.op[0] = 'c'; c.key[0] = '$'; break;
                case 'D': c.op[0] = 'd'; c.key[0] = '$'; break;
                case 'S': c.op[0] = 'c'; c.kind = VIM_CMD_OPERATOR_LINES; break;
                default: c.op[0] = 'y'; c.kind = VIM_CMD_OPERATOR_LINES; break;
            }
            c.op[1] = 0;
            if ((k == 'x' || k == 's') && gtk_text_iter_ends_line(&cur)) {
                /* Empty line: x has nothing to delete, s just inserts */
                if (k == 's') {
                    vim_e_start_insert(e, 's', 1, VIM_MODE_INSERT);
                    vim_e_start_recording_dot(e, cmd, NULL);
                } else {
                    vim_e_beep(e);
                }
                return;
            }
            if (c.kind == VIM_CMD_OPERATOR_LINES) vim_e_exec_operator_lines(e, &c);
            else vim_e_exec_operator(e, &c);
            if (k != 'Y') vim_e_start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'p': case 'P':
            put_register(e, cmd->reg, n, k == 'P', FALSE);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        case 'J': {
            gint line = gtk_text_iter_get_line(&cur);
            if (line >= vim_e_last_line(e)) {
                vim_e_beep(e);
                return;
            }
            vim_e_join_lines(e, line, line + MAX(n, 2) - 1, TRUE);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'r':
            if (!replace_chars(e, n, cmd->arg)) vim_e_beep(e);
            else vim_e_start_recording_dot(e, cmd, NULL);
            return;
        case 'R':
            vim_e_start_insert(e, 'R', count, VIM_MODE_REPLACE);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        case '~': {
            GtkTextIter t = cur;
            for (gint i = 0; i < n && !gtk_text_iter_ends_line(&t); i++) gtk_text_iter_forward_char(&t);
            gint end = vim_e_off(&t);
            vim_e_transform_span(e, vim_e_off(&cur), end, '~');
            GtkTextIter c;
            vim_e_iter_at(e, &c, end);
            vim_iter_clamp_normal(&c);
            vim_e_set_cursor(e, &c);
            vim_e_remember_column(e);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'u': case 'U': undo_redo(e, n, FALSE); return;
        case VIM_KEY_CTRL('r'): undo_redo(e, n, TRUE); return;
        case '.': exec_dot(e, count); return;
        case 'i': case 'a': case 'I': case 'A': {
            GtkTextIter p = cur;
            if (k == 'a' && !gtk_text_iter_ends_line(&p)) gtk_text_iter_forward_char(&p);
            if (k == 'A') vim_iter_line_end(&p);
            if (k == 'I') vim_iter_first_nonblank(&p);
            gtk_text_buffer_place_cursor(e->buf, &p);
            vim_e_start_insert(e, k, count, VIM_MODE_INSERT);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'o': case 'O':
            vim_e_begin_change(e);
            vim_e_open_line(e, k == 'o');
            vim_e_start_insert(e, k, count, VIM_MODE_INSERT);
            vim_e_start_recording_dot(e, cmd, NULL);
            return;
        case 'v': vim_e_enter_visual(e, VIM_MODE_VISUAL); return;
        case 'V': vim_e_enter_visual(e, VIM_MODE_VISUAL_LINE); return;
        case VIM_KEY_CTRL('v'): vim_e_enter_visual(e, VIM_MODE_VISUAL_BLOCK); return;
        case ':':
            if (count > 0) {
                gchar* range = count == 1 ? g_strdup(".") : g_strdup_printf(".,.+%d", count - 1);
                vim_e_cmdline_open(e, ':', range);
                g_free(range);
            } else {
                vim_e_cmdline_open(e, ':', "");
            }
            return;
        case '&':
            vim_e_substitute_repeat(e, gtk_text_iter_get_line(&cur), gtk_text_iter_get_line(&cur), FALSE, FALSE);
            return;
        case 'q':
            if (e->rec_reg != 0) {
                gunichar r = e->rec_reg;
                e->rec_reg = 0;
                gchar* keys = g_strdup(e->rec->str);
                if (r >= 'A' && r <= 'Z') vim_e_reg_set(e, r, keys, REG_CHAR, REG_YANK);
                else vim_e_reg_store(e, r, keys, REG_CHAR);
                g_free(keys);
                vim_e_state_changed(e);
                return;
            }
            if (!(g_ascii_isalnum(cmd->arg) || cmd->arg == '"')) {
                vim_e_beep(e);
                return;
            }
            e->rec_reg = cmd->arg;
            e->rec_started = TRUE;
            g_string_truncate(e->rec, 0);
            if (cmd->arg >= 'A' && cmd->arg <= 'Z') {
                VimRegister r;
                if (vim_e_reg_get(e, cmd->arg, &r) && r.text != NULL) g_string_append(e->rec, r.text);
                e->rec_reg = g_unichar_tolower(cmd->arg);
            }
            vim_e_state_changed(e);
            return;
        case '@': exec_macro(e, cmd->arg, n); return;
        case 'm':
            if (g_ascii_isalpha(cmd->arg) || cmd->arg == '\'' || cmd->arg == '`' || cmd->arg == '<' ||
                cmd->arg == '>' || cmd->arg == '[' || cmd->arg == ']') {
                vim_e_set_mark(e, cmd->arg, &cur);
            } else {
                vim_e_beep(e);
            }
            return;
        case VIM_KEY_CTRL('a'): case VIM_KEY_CTRL('x'): {
            GtkTextIter le = cur;
            vim_iter_line_end(&le);
            GtkTextIter p = cur;
            if (vim_e_increment_at(e, &p, vim_e_off(&le), (k == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n)) {
                vim_e_set_cursor(e, &p);
                vim_e_remember_column(e);
                vim_e_start_recording_dot(e, cmd, NULL);
            } else {
                vim_e_beep(e);
            }
            return;
        }
        case VIM_KEY_CTRL('e'): case VIM_KEY_CTRL('y'): case VIM_KEY_CTRL('d'):
        case VIM_KEY_CTRL('u'): case VIM_KEY_CTRL('f'): case VIM_KEY_CTRL('b'):
            vim_e_scroll_page(e, k, count);
            return;
        case VIM_KEY_CTRL('o'): vim_e_jump_older(e, n, TRUE); return;
        case VIM_KEY_CTRL(']'):
            if (!vim_e_host_goto_definition(e, FALSE)) vim_e_beep(e);
            return;
        case VIM_KEY_TAB: vim_e_jump_older(e, n, FALSE); return;
        case VIM_KEY_CTRL('g'): file_info(e); return;
        default:
            vim_e_beep(e);
            return;
    }
}
