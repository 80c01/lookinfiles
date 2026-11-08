/* Vim engine: Insert and Replace modes (typed keys, completion, soft tabs,
 * block insert) (see vim_engine_private.h) */
#include "vim_engine_private.h"
#include "vim_keys.h"
#include "vim_motion.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Insert mode                                                               */
/* ------------------------------------------------------------------------- */

void vim_e_start_insert(VimEngine* e, gunichar kind, gint count, VimMode mode) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    if (e->ins_start == NULL) {
        e->ins_start = gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->ins_start, &it);
    }
    g_string_truncate(e->ins_keys, 0);
    g_string_truncate(e->replace_stack, 0);
    e->ins_kind = kind;
    e->ins_count = count > 0 ? count : 1;
    vim_e_begin_change(e);
    vim_e_set_mode(e, mode);
}

void vim_e_open_line(VimEngine* e, gboolean below) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    gint line = gtk_text_iter_get_line(&it);
    gchar* ind = e->opt.autoindent ? vim_e_line_indent_text(e, line) : g_strdup("");
    if (below) {
        vim_iter_line_end(&it);
        gchar* t = g_strconcat("\n", ind, NULL);
        vim_e_insert_text(e, &it, t);
        g_free(t);
    } else {
        gtk_text_iter_set_line_offset(&it, 0);
        GtkTextIter mark = it;
        gint start = vim_e_off(&mark);
        gchar* t = g_strconcat(ind, "\n", NULL);
        vim_e_insert_text(e, &it, t);
        g_free(t);
        vim_e_iter_at(e, &it, start + (gint)g_utf8_strlen(ind, -1));
    }
    gtk_text_buffer_place_cursor(e->buf, &it);
    g_free(ind);
}

static void compl_end(VimEngine* e) {
    if (e->compl_words != NULL) {
        g_ptr_array_free(e->compl_words, TRUE);
        e->compl_words = NULL;
    }
    g_free(e->compl_prefix);
    e->compl_prefix = NULL;
}

/* Ctrl-N / Ctrl-P: complete the word before the cursor from the buffer */
static void complete_word(VimEngine* e, gboolean next) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    if (e->compl_words == NULL) {
        GtkTextIter ws = cur;
        while (!gtk_text_iter_starts_line(&ws)) {
            GtkTextIter prev = ws;
            gtk_text_iter_backward_char(&prev);
            if (vim_char_class(gtk_text_iter_get_char(&prev), FALSE) != 2) break;
            ws = prev;
        }
        e->compl_prefix = gtk_text_iter_get_slice(&ws, &cur);
        e->compl_start = vim_e_off(&ws);
        e->compl_words = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(e->compl_words, g_strdup(e->compl_prefix));
        GHashTable* seen = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_add(seen, e->compl_prefix);
        GtkTextIter s, t;
        gtk_text_buffer_get_bounds(e->buf, &s, &t);
        gchar* text = gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
        GRegex* re = g_regex_new("[\\w]+", 0, 0, NULL);
        GMatchInfo* mi = NULL;
        /* Words after the cursor first, then from the start (like Vim) */
        GPtrArray* before = g_ptr_array_new();
        GPtrArray* after = g_ptr_array_new();
        gsize cur_byte = (gsize)(g_utf8_offset_to_pointer(text, e->compl_start) - text);
        g_regex_match(re, text, 0, &mi);
        while (g_match_info_matches(mi)) {
            gint bs, be;
            g_match_info_fetch_pos(mi, 0, &bs, &be);
            gchar* w = g_strndup(text + bs, be - bs);
            if ((gsize)bs != cur_byte && g_str_has_prefix(w, e->compl_prefix) &&
                strlen(w) > strlen(e->compl_prefix) && !g_hash_table_contains(seen, w)) {
                g_ptr_array_add((gsize)bs > cur_byte ? after : before, w);
                g_hash_table_add(seen, w);
            } else {
                g_free(w);
            }
            g_match_info_next(mi, NULL);
        }
        g_match_info_free(mi);
        g_regex_unref(re);
        if (next) {
            for (guint i = 0; i < after->len; i++) g_ptr_array_add(e->compl_words, after->pdata[i]);
            for (guint i = 0; i < before->len; i++) g_ptr_array_add(e->compl_words, before->pdata[i]);
        } else {
            for (gint i = (gint)before->len - 1; i >= 0; i--) g_ptr_array_add(e->compl_words, before->pdata[i]);
            for (gint i = (gint)after->len - 1; i >= 0; i--) g_ptr_array_add(e->compl_words, after->pdata[i]);
        }
        g_ptr_array_free(before, TRUE);
        g_ptr_array_free(after, TRUE);
        g_hash_table_destroy(seen);
        g_free(text);
        e->compl_index = 0;
        if (e->compl_words->len == 1) {
            vim_e_message(e, TRUE, "Pattern not found");
            compl_end(e);
            return;
        }
    }
    gint n = (gint)e->compl_words->len;
    gint dir = next ? 1 : -1;
    if (e->compl_index == 0) dir = 1;
    e->compl_index = (e->compl_index + (next || e->compl_index == 0 ? dir : -1) + n) % n;
    const gchar* word = g_ptr_array_index(e->compl_words, e->compl_index);
    GtkTextIter s;
    vim_e_iter_at(e, &s, e->compl_start);
    vim_e_delete(e, &s, &cur);
    vim_e_insert_text(e, &s, word);
    gtk_text_buffer_place_cursor(e->buf, &s);
    vim_e_message(e, FALSE, "match %d of %d", e->compl_index == 0 ? n : e->compl_index, n - 1);
}

/* Copy of the text inserted on the first line, for block insert */
static void replicate_block_insert(VimEngine* e) {
    e->blk_active = FALSE;
    GtkTextIter s, cur;
    gtk_text_buffer_get_iter_at_mark(e->buf, &s, e->ins_start);
    vim_e_cursor(e, &cur);
    if (gtk_text_iter_compare(&cur, &s) <= 0 || gtk_text_iter_get_line(&cur) != gtk_text_iter_get_line(&s)) {
        return;
    }
    gchar* text = gtk_text_iter_get_slice(&s, &cur);
    gint ts = e->opt.tabstop;
    for (gint l = e->blk_first + 1; l <= e->blk_last && l <= vim_e_last_line(e); l++) {
        GtkTextIter it;
        vim_e_iter_line(e, &it, l);
        if (e->blk_eol) {
            vim_iter_line_end(&it);
        } else {
            vim_iter_set_vcol(&it, e->blk_vcol, ts);
            gint v = vim_iter_vcol(&it, ts);
            if (v < e->blk_vcol && gtk_text_iter_ends_line(&it)) {
                if (!e->blk_append) continue;
                GString* pad = g_string_new(NULL);
                for (gint i = v; i < e->blk_vcol; i++) g_string_append_c(pad, ' ');
                vim_e_insert_text(e, &it, pad->str);
                g_string_free(pad, TRUE);
            }
        }
        vim_e_insert_text(e, &it, text);
    }
    g_free(text);
    GtkTextIter first;
    vim_e_iter_line(e, &first, e->blk_first);
    vim_iter_set_vcol(&first, e->blk_vcol, ts);
    gtk_text_buffer_place_cursor(e->buf, &first);
}

void vim_e_leave_insert(VimEngine* e, gboolean move_left) {
    compl_end(e);
    e->ins_ctrl_r = FALSE;
    e->ins_ctrl_v = FALSE;
    if (e->ins_count > 1 && !e->ins_repeating && !e->blk_active) {
        gchar* keys = g_strdup(e->ins_keys->str);
        e->ins_repeating = TRUE;
        for (gint i = 1; i < e->ins_count; i++) {
            if (e->ins_kind == 'o') vim_e_open_line(e, TRUE);
            else if (e->ins_kind == 'O') vim_e_open_line(e, FALSE);
            vim_e_feed_internal(e, keys);
        }
        e->ins_repeating = FALSE;
        g_free(keys);
    }
    GtkTextIter s, cur;
    gtk_text_buffer_get_iter_at_mark(e->buf, &s, e->ins_start);
    vim_e_cursor(e, &cur);
    if (gtk_text_iter_compare(&cur, &s) > 0) {
        gchar* text = gtk_text_iter_get_slice(&s, &cur);
        vim_e_reg_store(e, '.', text, REG_CHAR);
        g_free(text);
        vim_e_set_mark(e, '[', &s);
        vim_e_set_mark(e, ']', &cur);
    }
    if (e->blk_active) replicate_block_insert(e);
    vim_e_cursor(e, &cur);
    vim_e_set_mark(e, '^', &cur);
    if (e->buffer_dirty) vim_e_set_mark(e, '.', &cur);
    if (e->dot_pending != NULL) {
        g_string_truncate(e->dot, 0);
        g_string_append(e->dot, e->dot_pending->str);
        g_string_append(e->dot, e->ins_keys->str);
        g_string_append_unichar(e->dot, VIM_KEY_ESC);
        e->dot_visual = e->dot_pending_visual;
        g_string_free(e->dot_pending, TRUE);
        e->dot_pending = NULL;
    }
    if (move_left && !gtk_text_iter_starts_line(&cur)) {
        gtk_text_iter_backward_char(&cur);
        gtk_text_buffer_place_cursor(e->buf, &cur);
    }
    vim_e_set_mode(e, VIM_MODE_NORMAL);
    vim_e_end_change(e);
    if (!move_left) return;
    vim_e_clamp_cursor(e);
    vim_e_remember_column(e);
}

static void delete_before_cursor(VimEngine* e, gint chars) {
    GtkTextIter cur, s;
    vim_e_cursor(e, &cur);
    s = cur;
    for (gint i = 0; i < chars; i++) gtk_text_iter_backward_char(&s);
    vim_e_delete(e, &s, &cur);
}

/* Columns of a soft tab ('softtabstop', negative: 'shiftwidth'), 0 when off */
static gint soft_tab_width(VimEngine* e) {
    gint sts = e->opt.softtabstop;
    if (sts < 0) sts = e->opt.shiftwidth > 0 ? e->opt.shiftwidth : e->opt.tabstop;
    return sts;
}

/* Tab in Insert mode: blanks up to the next 'softtabstop' column (or
 * 'tabstop' when off); with 'noexpandtab' the blanks before the cursor are
 * rebuilt with as many tabs as possible */
static void insert_soft_tab(VimEngine* e) {
    gint ts = e->opt.tabstop > 0 ? e->opt.tabstop : 8;
    gint width = soft_tab_width(e);
    if (width <= 0 && !e->opt.expandtab) {
        vim_e_insert_at_cursor(e, "\t");
        return;
    }
    if (width <= 0) width = ts;
    GtkTextIter cur, s;
    vim_e_cursor(e, &cur);
    gint v = vim_iter_vcol(&cur, ts);
    gint target = (v / width + 1) * width;
    if (e->opt.expandtab) {
        gchar* sp = g_strnfill(target - v, ' ');
        vim_e_insert_at_cursor(e, sp);
        g_free(sp);
        return;
    }
    s = cur;
    while (!gtk_text_iter_starts_line(&s)) {
        GtkTextIter p = s;
        gtk_text_iter_backward_char(&p);
        gunichar c = gtk_text_iter_get_char(&p);
        if (c != ' ' && c != '\t') break;
        s = p;
    }
    gint col = vim_iter_vcol(&s, ts);
    GString* w = g_string_new(NULL);
    while ((col / ts + 1) * ts <= target) {
        g_string_append_c(w, '\t');
        col = (col / ts + 1) * ts;
    }
    for (; col < target; col++) g_string_append_c(w, ' ');
    vim_e_delete(e, &s, &cur);
    vim_e_insert_text(e, &s, w->str);
    gtk_text_buffer_place_cursor(e->buf, &s);
    g_string_free(w, TRUE);
}

/* Backspace over blanks with 'softtabstop': delete back to the previous
 * soft tab column (stopping at text), padding with spaces when a tab went
 * too far. FALSE when it does not apply (plain backspace). */
static gboolean delete_soft_tab(VimEngine* e) {
    gint width = soft_tab_width(e);
    gint ts = e->opt.tabstop > 0 ? e->opt.tabstop : 8;
    if (width <= 0) return FALSE;
    GtkTextIter cur, s;
    vim_e_cursor(e, &cur);
    if (gtk_text_iter_starts_line(&cur)) return FALSE;
    GtkTextIter prev = cur;
    gtk_text_iter_backward_char(&prev);
    if (!vim_e_is_blank_char(gtk_text_iter_get_char(&prev))) return FALSE;
    gint v = vim_iter_vcol(&cur, ts);
    gint target = (v - 1) / width * width;
    s = cur;
    while (!gtk_text_iter_starts_line(&s) && vim_iter_vcol(&s, ts) > target) {
        GtkTextIter p = s;
        gtk_text_iter_backward_char(&p);
        if (!vim_e_is_blank_char(gtk_text_iter_get_char(&p))) break;
        s = p;
    }
    gint col = vim_iter_vcol(&s, ts);
    vim_e_delete(e, &s, &cur);
    if (col < target) {
        gchar* sp = g_strnfill(target - col, ' ');
        vim_e_insert_text(e, &s, sp);
        g_free(sp);
    }
    gtk_text_buffer_place_cursor(e->buf, &s);
    return TRUE;
}

void vim_e_insert_newline(VimEngine* e, gboolean indent) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    gchar* ind = (indent && e->opt.autoindent) ? vim_e_line_indent_text(e, gtk_text_iter_get_line(&cur))
                                              : g_strdup("");
    /* Leading white space before the cursor does not stay on an emptied line */
    gchar* t = g_strconcat("\n", ind, NULL);
    vim_e_insert_text(e, &cur, t);
    gtk_text_buffer_place_cursor(e->buf, &cur);
    g_free(t);
    g_free(ind);
}

gboolean vim_e_insert_key(VimEngine* e, gunichar k) {
    gboolean replaying = e->replaying > 0;
    if (e->ins_ctrl_v) {
        e->ins_ctrl_v = FALSE;
        gchar buf[8] = {0};
        gunichar lit = k == VIM_KEY_TAB ? '\t' : k == VIM_KEY_CR ? '\r' : k == VIM_KEY_ESC ? 0x1b
                     : VIM_KEY_IS_CTRL(k) ? (gunichar)((k - 0xE100) & 0x1f) : k;
        g_unichar_to_utf8(lit, buf);
        vim_e_insert_at_cursor(e, buf);
        vim_e_record_insert_key(e, VIM_KEY_CTRL('v'));
        vim_e_record_insert_key(e, k);
        return TRUE;
    }
    if (e->ins_ctrl_r) {
        e->ins_ctrl_r = FALSE;
        VimRegister r;
        if (vim_e_reg_get(e, k, &r) && r.text != NULL) {
            vim_e_insert_at_cursor(e, r.text);
            vim_e_record_insert_keys(e, r.text);
        } else {
            vim_e_beep(e);
        }
        return TRUE;
    }
    if (e->compl_words != NULL && k != VIM_KEY_CTRL('n') && k != VIM_KEY_CTRL('p')) compl_end(e);

    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    switch (k) {
        case VIM_KEY_ESC: case VIM_KEY_CTRL('c'):
            vim_e_leave_insert(e, TRUE);
            return TRUE;
        case VIM_KEY_CR:
            vim_e_insert_newline(e, TRUE);
            vim_e_record_insert_key(e, k);
            return TRUE;
        case VIM_KEY_NL:
            vim_e_insert_newline(e, replaying ? FALSE : TRUE);
            vim_e_record_insert_key(e, replaying ? k : VIM_KEY_CR);
            return TRUE;
        case VIM_KEY_TAB:
            if (e->opt.expandtab || soft_tab_width(e) > 0 || replaying) {
                insert_soft_tab(e);
                vim_e_record_insert_key(e, k);
                return TRUE;
            }
            return FALSE;
        case VIM_KEY_BS:
            if (e->mode == VIM_MODE_REPLACE) {
                if (e->replace_stack->len == 0) {
                    GtkTextIter prev = cur;
                    if (gtk_text_iter_backward_char(&prev)) gtk_text_buffer_place_cursor(e->buf, &prev);
                } else {
                    const gchar* last = g_utf8_find_prev_char(e->replace_stack->str,
                                                              e->replace_stack->str + e->replace_stack->len);
                    gunichar orig = g_utf8_get_char(last);
                    g_string_truncate(e->replace_stack, last - e->replace_stack->str);
                    delete_before_cursor(e, 1);
                    if (orig != 0x1) {
                        gchar buf[8] = {0};
                        g_unichar_to_utf8(orig, buf);
                        vim_e_cursor(e, &cur);
                        vim_e_insert_text(e, &cur, buf);
                        gtk_text_iter_backward_char(&cur);
                        gtk_text_buffer_place_cursor(e->buf, &cur);
                    }
                }
                vim_e_record_insert_key(e, k);
                return TRUE;
            }
            if (delete_soft_tab(e)) {
                vim_e_record_insert_key(e, k);
                return TRUE;
            }
            if (replaying) {
                if (!gtk_text_iter_is_start(&cur)) delete_before_cursor(e, 1);
                vim_e_record_insert_key(e, k);
                return TRUE;
            }
            return FALSE;
        case VIM_KEY_DEL:
            if (replaying) {
                GtkTextIter t = cur;
                gtk_text_iter_forward_char(&t);
                vim_e_delete(e, &cur, &t);
                vim_e_record_insert_key(e, k);
                return TRUE;
            }
            return FALSE;
        case VIM_KEY_CTRL('w'): {
            GtkTextIter s = cur;
            if (gtk_text_iter_starts_line(&s)) {
                if (!gtk_text_iter_is_start(&s)) gtk_text_iter_backward_char(&s);
            } else {
                while (!gtk_text_iter_starts_line(&s)) {
                    GtkTextIter prev = s;
                    gtk_text_iter_backward_char(&prev);
                    if (!vim_e_is_blank_char(gtk_text_iter_get_char(&prev))) break;
                    s = prev;
                }
                if (!gtk_text_iter_starts_line(&s)) {
                    GtkTextIter prev = s;
                    gtk_text_iter_backward_char(&prev);
                    gint c0 = vim_char_class(gtk_text_iter_get_char(&prev), FALSE);
                    while (!gtk_text_iter_starts_line(&s)) {
                        prev = s;
                        gtk_text_iter_backward_char(&prev);
                        if (vim_char_class(gtk_text_iter_get_char(&prev), FALSE) != c0) break;
                        s = prev;
                    }
                }
            }
            vim_e_delete(e, &s, &cur);
            vim_e_record_insert_key(e, k);
            return TRUE;
        }
        case VIM_KEY_CTRL('u'): {
            GtkTextIter s = cur, nb = cur;
            vim_iter_first_nonblank(&nb);
            if (gtk_text_iter_compare(&cur, &nb) > 0) s = nb;
            else gtk_text_iter_set_line_offset(&s, 0);
            vim_e_delete(e, &s, &cur);
            vim_e_record_insert_key(e, k);
            return TRUE;
        }
        case VIM_KEY_CTRL('t'): case VIM_KEY_CTRL('d'): {
            gint line = gtk_text_iter_get_line(&cur);
            GtkTextIter end = cur;
            vim_iter_line_end(&end);
            gint from_end = vim_e_off(&end) - vim_e_off(&cur);
            vim_e_shift_lines(e, line, line, k == VIM_KEY_CTRL('t') ? 1 : -1);
            vim_e_iter_line(e, &end, line);
            vim_iter_line_end(&end);
            GtkTextIter nc;
            vim_e_iter_at(e, &nc, MAX(vim_e_off(&end) - from_end, 0));
            GtkTextIter nb = end;
            vim_iter_first_nonblank(&nb);
            if (gtk_text_iter_get_line(&nc) != line || gtk_text_iter_compare(&nc, &nb) < 0) {
                if (gtk_text_iter_compare(&nc, &nb) < 0 || gtk_text_iter_get_line(&nc) != line) nc = nb;
            }
            gtk_text_buffer_place_cursor(e->buf, &nc);
            vim_e_record_insert_key(e, k);
            return TRUE;
        }
        case VIM_KEY_CTRL('r'):
            e->ins_ctrl_r = TRUE;
            return TRUE;
        case VIM_KEY_CTRL('v'): case VIM_KEY_CTRL('q'):
            e->ins_ctrl_v = TRUE;
            return TRUE;
        case VIM_KEY_CTRL('a'): case VIM_KEY_CTRL('@'): {
            VimRegister r;
            if (vim_e_reg_get(e, '.', &r) && r.text != NULL) {
                vim_e_insert_at_cursor(e, r.text);
                vim_e_record_insert_keys(e, r.text);
            }
            if (k == VIM_KEY_CTRL('@')) vim_e_leave_insert(e, TRUE);
            return TRUE;
        }
        case VIM_KEY_CTRL('e'): case VIM_KEY_CTRL('y'): {
            gint line = gtk_text_iter_get_line(&cur) + (k == VIM_KEY_CTRL('e') ? 1 : -1);
            if (line < 0 || line > vim_e_last_line(e)) {
                vim_e_beep(e);
                return TRUE;
            }
            GtkTextIter src;
            vim_e_iter_line(e, &src, line);
            vim_iter_set_vcol(&src, vim_iter_vcol(&cur, e->opt.tabstop), e->opt.tabstop);
            if (gtk_text_iter_ends_line(&src)) {
                vim_e_beep(e);
                return TRUE;
            }
            gchar buf[8] = {0};
            g_unichar_to_utf8(gtk_text_iter_get_char(&src), buf);
            vim_e_insert_at_cursor(e, buf);
            vim_e_record_insert_keys(e, buf);
            return TRUE;
        }
        case VIM_KEY_CTRL('o'):
            e->ins_ctrl_o = TRUE;
            vim_e_leave_insert(e, FALSE);
            vim_e_state_changed(e);
            return TRUE;
        case VIM_KEY_CTRL('n'): case VIM_KEY_CTRL('p'):
            complete_word(e, k == VIM_KEY_CTRL('n'));
            return TRUE;
        case VIM_KEY_INSERT:
            vim_e_set_mode(e, e->mode == VIM_MODE_INSERT ? VIM_MODE_REPLACE : VIM_MODE_INSERT);
            return TRUE;
        case VIM_KEY_LEFT: case VIM_KEY_RIGHT: case VIM_KEY_UP: case VIM_KEY_DOWN:
        case VIM_KEY_HOME: case VIM_KEY_END: case VIM_KEY_PAGEUP: case VIM_KEY_PAGEDOWN: {
            GtkTextIter p = cur;
            gint ts = e->opt.tabstop;
            if (k == VIM_KEY_LEFT) {
                if (!gtk_text_iter_starts_line(&p)) gtk_text_iter_backward_char(&p);
            } else if (k == VIM_KEY_RIGHT) {
                if (!gtk_text_iter_ends_line(&p)) gtk_text_iter_forward_char(&p);
            } else if (k == VIM_KEY_HOME) {
                gtk_text_iter_set_line_offset(&p, 0);
            } else if (k == VIM_KEY_END) {
                vim_iter_line_end(&p);
            } else {
                gint delta = (k == VIM_KEY_UP) ? -1 : (k == VIM_KEY_DOWN) ? 1 : 0;
                if (delta == 0) {
                    gint first = 0, last = 0;
                    gint page = 20;
                    if (e->host.visible_lines != NULL && e->host.visible_lines(e->hd, &first, &last)) {
                        page = MAX(1, last - first - 1);
                    }
                    delta = (k == VIM_KEY_PAGEUP) ? -page : page;
                }
                gint line = CLAMP(gtk_text_iter_get_line(&p) + delta, 0, vim_e_last_line(e));
                gint v = vim_iter_vcol(&p, ts);
                vim_e_iter_line(e, &p, line);
                vim_iter_set_vcol(&p, v, ts);
            }
            gtk_text_buffer_place_cursor(e->buf, &p);
            /* Moving starts a new undo step and a new repeatable insert */
            if (e->dot_pending != NULL) {
                g_string_truncate(e->dot, 0);
                g_string_append(e->dot, e->dot_pending->str);
                g_string_append(e->dot, e->ins_keys->str);
                g_string_append_unichar(e->dot, VIM_KEY_ESC);
                g_string_assign(e->dot_pending, "i");
                e->dot_count = 0;
                e->dot_pending_visual = FALSE;
            }
            vim_e_end_change(e);
            vim_e_begin_change(e);
            g_string_truncate(e->ins_keys, 0);
            e->ins_count = 1;
            gtk_text_buffer_move_mark(e->buf, e->ins_start, &p);
            return TRUE;
        }
        default:
            break;
    }
    if (VIM_KEY_IS_SPECIAL(k)) return FALSE; /* other Ctrl keys: the application's */
    if (e->mode == VIM_MODE_REPLACE) {
        GtkTextIter t = cur;
        gchar orig[8] = {0};
        if (!gtk_text_iter_ends_line(&t)) {
            g_unichar_to_utf8(gtk_text_iter_get_char(&t), orig);
            gtk_text_iter_forward_char(&t);
            vim_e_delete(e, &cur, &t);
        } else {
            orig[0] = 0x1; /* appended: BS removes it */
        }
        g_string_append(e->replace_stack, orig);
        gchar buf[8] = {0};
        g_unichar_to_utf8(k, buf);
        vim_e_insert_at_cursor(e, buf);
        vim_e_record_insert_key(e, k);
        return TRUE;
    }
    if (replaying) {
        gchar buf[8] = {0};
        g_unichar_to_utf8(k, buf);
        vim_e_insert_at_cursor(e, buf);
        vim_e_record_insert_key(e, k);
        return TRUE;
    }
    return FALSE; /* the text view inserts it; on_insert_text records it */
}
