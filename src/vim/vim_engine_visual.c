/* Vim engine: Visual mode selection and its commands (see vim_engine_private.h) */
#include "vim/vim_engine_private.h"
#include "vim/vim_keys.h"
#include "vim/vim_motion.h"

/* ------------------------------------------------------------------------- */
/* Visual mode                                                               */
/* ------------------------------------------------------------------------- */

static void visual_anchor(VimEngine* e, GtkTextIter* it) {
    gtk_text_buffer_get_iter_at_mark(e->buf, it, e->vis_anchor);
}

/* Block columns of the selection, inclusive */
static void block_cols(VimEngine* e, gint* l1, gint* l2, gint* c1, gint* c2) {
    GtkTextIter a, c;
    visual_anchor(e, &a);
    vim_e_cursor(e, &c);
    gint ts = e->opt.tabstop;
    gint la = gtk_text_iter_get_line(&a), lc = gtk_text_iter_get_line(&c);
    *l1 = MIN(la, lc);
    *l2 = MAX(la, lc);
    gint va = vim_iter_vcol(&a, ts), vc = vim_iter_vcol(&c, ts);
    gint ea = vim_iter_vcol_end(&a, ts) - 1, ec = vim_iter_vcol_end(&c, ts) - 1;
    *c1 = MIN(va, vc);
    *c2 = MAX(ea, ec);
    if (e->vis_eol) *c2 = VIM_MAXCOL;
}

/* Characters of line covered by block columns [c1, c2] */
gboolean vim_e_block_line_span(VimEngine* e, gint line, gint c1, gint c2, GtkTextIter* s, GtkTextIter* t) {
    gint ts = e->opt.tabstop;
    vim_e_iter_line(e, s, line);
    vim_iter_set_vcol(s, c1, ts);
    if (vim_iter_vcol(s, ts) < c1 && gtk_text_iter_ends_line(s)) {
        *t = *s;
        return FALSE; /* line shorter than the block */
    }
    *t = *s;
    if (c2 == VIM_MAXCOL) {
        vim_iter_line_end(t);
    } else {
        while (!gtk_text_iter_ends_line(t) && vim_iter_vcol(t, ts) <= c2) {
            gtk_text_iter_forward_char(t);
        }
    }
    return TRUE;
}

void vim_e_visual_bounds(VimEngine* e, GtkTextIter* start, GtkTextIter* end) {
    GtkTextIter a, c;
    visual_anchor(e, &a);
    vim_e_cursor(e, &c);
    if (gtk_text_iter_compare(&a, &c) <= 0) {
        *start = a;
        *end = c;
    } else {
        *start = c;
        *end = a;
    }
    if (e->mode == VIM_MODE_VISUAL_LINE || e->mode == VIM_MODE_VISUAL_BLOCK) {
        gtk_text_iter_set_line_offset(start, 0);
        if (!gtk_text_iter_forward_line(end)) gtk_text_buffer_get_end_iter(e->buf, end);
    } else if (!gtk_text_iter_is_end(end)) {
        gtk_text_iter_forward_char(end);
    }
}

void vim_e_update_visual(VimEngine* e) {
    if (e->updating_visual) return;
    e->updating_visual = TRUE;
    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(e->buf, &s, &t);
    gtk_text_buffer_remove_tag(e->buf, e->tag_visual, &s, &t);
    if (vim_e_is_visual(e->mode) && e->vis_anchor != NULL) {
        /* Our highlight replaces GTK's selection */
        GtkTextIter cur;
        vim_e_cursor(e, &cur);
        GtkTextIter bound;
        gtk_text_buffer_get_iter_at_mark(e->buf, &bound, gtk_text_buffer_get_selection_bound(e->buf));
        if (!gtk_text_iter_equal(&cur, &bound)) gtk_text_buffer_place_cursor(e->buf, &cur);
        if (e->mode == VIM_MODE_VISUAL_BLOCK) {
            gint l1, l2, c1, c2;
            block_cols(e, &l1, &l2, &c1, &c2);
            for (gint l = l1; l <= l2; l++) {
                GtkTextIter a, b;
                if (vim_e_block_line_span(e, l, c1, c2, &a, &b)) {
                    gtk_text_buffer_apply_tag(e->buf, e->tag_visual, &a, &b);
                }
            }
        } else {
            vim_e_visual_bounds(e, &s, &t);
            gtk_text_buffer_apply_tag(e->buf, e->tag_visual, &s, &t);
        }
    }
    e->updating_visual = FALSE;
}

void vim_e_enter_visual(VimEngine* e, VimMode mode) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    if (!vim_e_is_visual(e->mode)) {
        if (e->vis_anchor == NULL) {
            e->vis_anchor = gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE);
        } else {
            gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &it);
        }
        e->vis_eol = FALSE;
    }
    vim_e_set_mode(e, mode);
    vim_e_update_visual(e);
}

/* Leave Visual mode, remembering the selection for gv and '< '> */
void vim_e_exit_visual(VimEngine* e) {
    if (!vim_e_is_visual(e->mode)) return;
    GtkTextIter a, c, s, t;
    visual_anchor(e, &a);
    vim_e_cursor(e, &c);
    vim_e_visual_bounds(e, &s, &t);
    if (e->mode == VIM_MODE_VISUAL) {
        if (gtk_text_iter_compare(&t, &s) > 0) gtk_text_iter_backward_char(&t);
        vim_e_set_mark(e, '<', &s);
        vim_e_set_mark(e, '>', &t);
    } else {
        GtkTextIter first = gtk_text_iter_compare(&a, &c) <= 0 ? a : c;
        GtkTextIter last = gtk_text_iter_compare(&a, &c) <= 0 ? c : a;
        if (e->mode == VIM_MODE_VISUAL_LINE) {
            gtk_text_iter_set_line_offset(&first, 0);
            vim_iter_line_end(&last);
            vim_iter_clamp_normal(&last);
        }
        vim_e_set_mark(e, '<', &first);
        vim_e_set_mark(e, '>', &last);
    }
    e->last_vis_mode = e->mode;
    e->last_vis_eol = e->vis_eol;
    e->last_vis_anchor_off = vim_e_off(&a);
    e->last_vis_cursor_off = vim_e_off(&c);
    vim_e_set_mode(e, VIM_MODE_NORMAL);
    vim_e_update_visual(e);
}

void vim_e_reselect_visual(VimEngine* e) {
    if (e->last_vis_mode == VIM_MODE_NORMAL) {
        vim_e_beep(e);
        return;
    }
    VimMode m = e->last_vis_mode;
    gint a = e->last_vis_anchor_off, c = e->last_vis_cursor_off;
    gboolean eol = e->last_vis_eol;
    if (vim_e_is_visual(e->mode)) {
        GtkTextIter ca, cc;
        visual_anchor(e, &ca);
        vim_e_cursor(e, &cc);
        e->last_vis_mode = e->mode;
        e->last_vis_anchor_off = vim_e_off(&ca);
        e->last_vis_cursor_off = vim_e_off(&cc);
        e->last_vis_eol = e->vis_eol;
    }
    GtkTextIter it;
    vim_e_iter_at(e, &it, a);
    if (e->vis_anchor == NULL) {
        e->vis_anchor = gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &it);
    }
    vim_e_iter_at(e, &it, c);
    gtk_text_buffer_place_cursor(e->buf, &it);
    e->vis_eol = eol;
    vim_e_set_mode(e, m);
    vim_e_update_visual(e);
}

/* The selection as an operator range, then back to Normal mode */
VimRange vim_e_visual_range(VimEngine* e) {
    VimRange r = {0};
    GtkTextIter s, t;
    if (e->mode == VIM_MODE_VISUAL_BLOCK) {
        block_cols(e, &r.line1, &r.line2, &r.vcol1, &r.vcol2);
        r.type = REG_BLOCK;
        vim_e_iter_line(e, &s, r.line1);
        vim_e_iter_line(e, &t, r.line2);
        vim_iter_line_end(&t);
        r.start = vim_e_off(&s);
        r.end = vim_e_off(&t);
    } else {
        vim_e_visual_bounds(e, &s, &t);
        r.start = vim_e_off(&s);
        r.end = vim_e_off(&t);
        r.type = e->mode == VIM_MODE_VISUAL_LINE ? REG_LINE : REG_CHAR;
        r.line1 = gtk_text_iter_get_line(&s);
        GtkTextIter last = t;
        if (gtk_text_iter_compare(&last, &s) > 0) gtk_text_iter_backward_char(&last);
        r.line2 = gtk_text_iter_get_line(&last);
        if (r.type == REG_LINE && gtk_text_iter_is_end(&t) && !gtk_text_iter_starts_line(&t)) {
            r.line2 = gtk_text_iter_get_line(&t);
        }
    }
    return r;
}

/* ------------------------------------------------------------------------- */
/* Visual mode commands                                                      */
/* ------------------------------------------------------------------------- */

static void record_visual_dot(VimEngine* e, const VimCmd* cmd, const VimRange* r, VimMode mode) {
    GString* s = g_string_new(NULL);
    if (cmd->key[0]) g_string_append_unichar(s, cmd->key[0]);
    if (cmd->key[1]) g_string_append_unichar(s, cmd->key[1]);
    if (cmd->arg) g_string_append_unichar(s, cmd->arg);
    e->dot_vis_mode = mode;
    e->dot_vis_lines = r->line2 - r->line1;
    if (mode == VIM_MODE_VISUAL_BLOCK) {
        e->dot_vis_cols = r->vcol2 == VIM_MAXCOL ? 1 : r->vcol2 - r->vcol1 + 1;
    } else if (mode == VIM_MODE_VISUAL && r->line1 == r->line2) {
        e->dot_vis_cols = r->end - r->start;
    } else {
        GtkTextIter t;
        vim_e_iter_at(e, &t, r->end);
        e->dot_vis_cols = gtk_text_iter_get_line_offset(&t);
    }
    e->dot_count = 0;
    e->dot_reg = cmd->reg;
    if (vim_e_is_insert(e->mode)) {
        if (e->dot_pending != NULL) g_string_free(e->dot_pending, TRUE);
        e->dot_pending = s;
        e->dot_pending_visual = TRUE;
    } else {
        g_string_assign(e->dot, s->str);
        e->dot_visual = TRUE;
        g_string_free(s, TRUE);
    }
}

static void visual_text_object(VimEngine* e, const VimCmd* cmd) {
    GtkTextIter a, c, s, t;
    visual_anchor(e, &a);
    vim_e_cursor(e, &c);
    if (gtk_text_iter_equal(&a, &c)) {
        s = t = c;
    } else {
        vim_e_visual_bounds(e, &s, &t);
    }
    gboolean lw = FALSE;
    if (!vim_textobj(e->buf, cmd->key[1], cmd->key[0] == 'a', vim_cmd_count1(cmd), &s, &t, &lw) ||
        gtk_text_iter_compare(&s, &t) >= 0) {
        vim_e_beep(e);
        return;
    }
    if (lw && e->mode == VIM_MODE_VISUAL) vim_e_set_mode(e, VIM_MODE_VISUAL_LINE);
    gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &s);
    gtk_text_iter_backward_char(&t);
    gtk_text_buffer_place_cursor(e->buf, &t);
    vim_e_update_visual(e);
    vim_e_remember_column(e);
}

static void visual_block_insert(VimEngine* e, gboolean append, gboolean to_eol) {
    VimRange r = vim_e_visual_range(e);
    VimMode mode = e->mode;
    vim_e_exit_visual(e);
    gint ts = e->opt.tabstop;
    e->blk_active = TRUE;
    e->blk_first = r.line1;
    e->blk_last = r.line2;
    e->blk_append = append;
    e->blk_eol = to_eol || (append && r.vcol2 == VIM_MAXCOL);
    GtkTextIter it;
    vim_e_iter_line(e, &it, r.line1);
    if (mode == VIM_MODE_VISUAL_LINE) {
        e->blk_vcol = 0;
        if (append) vim_iter_line_end(&it);
    } else if (e->blk_eol) {
        vim_iter_line_end(&it);
        e->blk_vcol = vim_iter_vcol(&it, ts);
    } else {
        e->blk_vcol = append ? r.vcol2 + 1 : r.vcol1;
        vim_iter_set_vcol(&it, e->blk_vcol, ts);
        gint v = vim_iter_vcol(&it, ts);
        if (append && v < e->blk_vcol) {
            GString* pad = g_string_new(NULL);
            for (gint i = v; i < e->blk_vcol; i++) g_string_append_c(pad, ' ');
            vim_e_insert_text(e, &it, pad->str);
            g_string_free(pad, TRUE);
        }
    }
    gtk_text_buffer_place_cursor(e->buf, &it);
    vim_e_start_insert(e, append ? 'A' : 'I', 1, VIM_MODE_INSERT);
}

void vim_e_exec_visual(VimEngine* e, const VimCmd* cmd) {
    if (cmd->kind == VIM_CMD_MOTION) {
        if (cmd->key[0] == '/' || cmd->key[0] == '?') {
            vim_e_open_cmdline_for(e, cmd->key[0], NULL);
            return;
        }
        GtkTextIter cur;
        vim_e_cursor(e, &cur);
        Motion m;
        if (!vim_e_do_motion(e, cmd, FALSE, &cur, &m)) {
            vim_e_beep(e);
            return;
        }
        if (e->mode == VIM_MODE_VISUAL_BLOCK && !m.want_eol && !m.keep_want) e->vis_eol = FALSE;
        vim_e_move_cursor(e, &m);
        return;
    }
    if (cmd->kind == VIM_CMD_TEXTOBJ) {
        visual_text_object(e, cmd);
        return;
    }
    gunichar k = cmd->key[0], k1 = cmd->key[1];
    gint n = vim_cmd_count1(cmd);
    VimMode mode = e->mode;

    if (k == VIM_KEY_ESC || k == VIM_KEY_CTRL('c')) {
        vim_e_exit_visual(e);
        return;
    }
    if (k == 'v' || k == 'V' || k == VIM_KEY_CTRL('v')) {
        VimMode want = k == 'v' ? VIM_MODE_VISUAL : k == 'V' ? VIM_MODE_VISUAL_LINE : VIM_MODE_VISUAL_BLOCK;
        if (want == mode) vim_e_exit_visual(e);
        else vim_e_enter_visual(e, want);
        return;
    }
    if (k == 'o' || k == 'O') {
        GtkTextIter a, c;
        visual_anchor(e, &a);
        vim_e_cursor(e, &c);
        if (k == 'O' && mode == VIM_MODE_VISUAL_BLOCK) {
            gint ts = e->opt.tabstop;
            gint va = vim_iter_vcol(&a, ts), vc = vim_iter_vcol(&c, ts);
            GtkTextIter na = a, nc = c;
            vim_iter_set_vcol(&na, vc, ts);
            vim_iter_set_vcol(&nc, va, ts);
            gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &na);
            gtk_text_buffer_place_cursor(e->buf, &nc);
        } else {
            gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &c);
            gtk_text_buffer_place_cursor(e->buf, &a);
        }
        vim_e_update_visual(e);
        vim_e_remember_column(e);
        return;
    }
    if (k == 'g' && k1 == 'v') {
        vim_e_reselect_visual(e);
        return;
    }
    if (k == ':') {
        vim_e_exit_visual(e);
        vim_e_cmdline_open(e, ':', "'<,'>");
        return;
    }
    if (k == 'm') {
        GtkTextIter c;
        vim_e_cursor(e, &c);
        vim_e_set_mark(e, cmd->arg, &c);
        return;
    }
    if (k == 'z') {
        if (e->host.scroll != NULL) {
            e->host.scroll(e->hd, k1 == 't' ? VIM_SCROLL_CURSOR_TOP
                                  : k1 == 'b' ? VIM_SCROLL_CURSOR_BOTTOM : VIM_SCROLL_CURSOR_CENTER, 0);
        }
        return;
    }
    if (k == VIM_KEY_CTRL('e') || k == VIM_KEY_CTRL('y') || k == VIM_KEY_CTRL('d') ||
        k == VIM_KEY_CTRL('u') || k == VIM_KEY_CTRL('f') || k == VIM_KEY_CTRL('b')) {
        vim_e_scroll_page(e, k, cmd->count);
        return;
    }
    if (k == 'I' || k == 'A') {
        if (mode == VIM_MODE_VISUAL) {
            /* Character selection: insert at its start / append after its end */
            GtkTextIter s, t;
            vim_e_visual_bounds(e, &s, &t);
            gint at = k == 'I' ? vim_e_off(&s) : vim_e_off(&t);
            vim_e_exit_visual(e);
            GtkTextIter it;
            vim_e_iter_at(e, &it, at);
            gtk_text_buffer_place_cursor(e->buf, &it);
            vim_e_start_insert(e, k, 1, VIM_MODE_INSERT);
            return;
        }
        VimRange r = vim_e_visual_range(e);
        visual_block_insert(e, k == 'A', FALSE);
        record_visual_dot(e, cmd, &r, mode);
        return;
    }

    /* Commands acting on the selection */
    VimRange r = vim_e_visual_range(e);
    VimCmd op = *cmd;
    op.kind = VIM_CMD_VISUAL_OP;
    op.op[0] = 0;
    op.op[1] = 0;
    gboolean change = TRUE;
    switch (k) {
        case 'd': case 'x': op.op[0] = 'd'; break;
        case 'X': case 'D':
            op.op[0] = 'd';
            if (mode == VIM_MODE_VISUAL_BLOCK) r.vcol2 = VIM_MAXCOL;
            else r.type = REG_LINE;
            break;
        case 'y': op.op[0] = 'y'; change = FALSE; break;
        case 'Y':
            op.op[0] = 'y';
            change = FALSE;
            if (mode != VIM_MODE_VISUAL_BLOCK) r.type = REG_LINE;
            break;
        case 'c': case 's': op.op[0] = 'c'; break;
        case 'C': case 'S': case 'R':
            op.op[0] = 'c';
            if (mode == VIM_MODE_VISUAL_BLOCK && k == 'C') r.vcol2 = VIM_MAXCOL;
            else if (mode != VIM_MODE_VISUAL_BLOCK || k != 'C') r.type = REG_LINE;
            break;
        case '<': case '>': case '=': op.op[0] = k; break;
        case '~': op.op[0] = 'g'; op.op[1] = '~'; break;
        case 'u': op.op[0] = 'g'; op.op[1] = 'u'; break;
        case 'U': op.op[0] = 'g'; op.op[1] = 'U'; break;
        case 'g':
            if (k1 == '~' || k1 == 'u' || k1 == 'U' || k1 == '?' || k1 == 'q' || k1 == 'w') {
                op.op[0] = 'g';
                op.op[1] = k1;
            }
            break;
        default: break;
    }
    if (r.type == REG_LINE && mode != VIM_MODE_VISUAL_LINE) {
        GtkTextIter s, t;
        vim_e_range_iters(e, &r, &s, &t);
        r.start = vim_e_off(&s);
        r.end = vim_e_off(&t);
    }
    if (op.op[0] != 0) {
        vim_e_exit_visual(e);
        vim_e_apply_operator(e, &op, &r, FALSE);
        if (change) record_visual_dot(e, cmd, &r, mode);
        return;
    }
    switch (k) {
        case 'r': {
            vim_e_exit_visual(e);
            gchar buf[8] = {0};
            g_unichar_to_utf8(cmd->arg, buf);
            if (r.type == REG_BLOCK) {
                for (gint l = r.line1; l <= r.line2; l++) {
                    GtkTextIter a, b;
                    if (!vim_e_block_line_span(e, l, r.vcol1, r.vcol2, &a, &b)) continue;
                    gint na = (gint)(vim_e_off(&b) - vim_e_off(&a));
                    GString* rep = g_string_new(NULL);
                    for (gint i = 0; i < na; i++) g_string_append(rep, buf);
                    vim_e_delete(e, &a, &b);
                    vim_e_insert_text(e, &a, rep->str);
                    g_string_free(rep, TRUE);
                }
            } else {
                GtkTextIter s, t;
                vim_e_range_iters(e, &r, &s, &t);
                gchar* text = gtk_text_iter_get_slice(&s, &t);
                GString* rep = g_string_new(NULL);
                for (const gchar* p = text; *p; p = g_utf8_next_char(p)) {
                    if (*p == '\n') g_string_append_c(rep, '\n');
                    else g_string_append(rep, buf);
                }
                gint start = vim_e_off(&s);
                vim_e_delete(e, &s, &t);
                vim_e_insert_text(e, &s, rep->str);
                vim_e_iter_at(e, &s, start);
                g_string_free(rep, TRUE);
                g_free(text);
            }
            GtkTextIter it;
            vim_e_iter_at(e, &it, r.start);
            if (r.type == REG_BLOCK) {
                vim_e_iter_line(e, &it, r.line1);
                vim_iter_set_vcol(&it, r.vcol1, e->opt.tabstop);
            }
            vim_e_set_cursor(e, &it);
            record_visual_dot(e, cmd, &r, mode);
            return;
        }
        case 'J':
            vim_e_exit_visual(e);
            vim_e_join_lines(e, r.line1, MAX(r.line2, r.line1 + 1), TRUE);
            record_visual_dot(e, cmd, &r, mode);
            return;
        case 'g':
            if (k1 == 'J') {
                vim_e_exit_visual(e);
                vim_e_join_lines(e, r.line1, MAX(r.line2, r.line1 + 1), FALSE);
                record_visual_dot(e, cmd, &r, mode);
                return;
            }
            if (k1 == VIM_KEY_CTRL('a') || k1 == VIM_KEY_CTRL('x')) {
                vim_e_exit_visual(e);
                gint64 step = (k1 == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n;
                gint64 delta = step;
                for (gint l = r.line1; l <= r.line2; l++) {
                    GtkTextIter p, le;
                    vim_e_iter_line(e, &p, l);
                    le = p;
                    vim_iter_line_end(&le);
                    if (vim_e_increment_at(e, &p, vim_e_off(&le), delta)) delta += step;
                }
                vim_e_set_cursor_line(e, r.line1, FALSE);
                return;
            }
            vim_e_exit_visual(e);
            return;
        case 'p': case 'P':
            vim_e_put_over_selection(e, cmd->reg, n, k == 'P');
            record_visual_dot(e, cmd, &r, mode);
            return;
        case VIM_KEY_CTRL('a'): case VIM_KEY_CTRL('x'): {
            vim_e_exit_visual(e);
            gint64 delta = (k == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n;
            for (gint l = r.line1; l <= r.line2; l++) {
                GtkTextIter p, le;
                vim_e_iter_line(e, &p, l);
                if (l == r.line1 && r.type == REG_CHAR) vim_e_iter_at(e, &p, r.start);
                le = p;
                vim_iter_line_end(&le);
                vim_e_increment_at(e, &p, vim_e_off(&le), delta);
            }
            vim_e_set_cursor_line(e, r.line1, FALSE);
            record_visual_dot(e, cmd, &r, mode);
            return;
        }
        default:
            vim_e_beep(e);
            return;
    }
}
