/* Vim engine: motions (h j k l, words, f / t, %, marks, searches...) for the
 * cursor, Visual mode and operators (see vim_engine_private.h) */
#include "vim_engine_private.h"
#include "vim_motion.h"
#include "vim_regex.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Motions                                                                   */
/* ------------------------------------------------------------------------- */


static gboolean motion_lines(VimEngine* e, const GtkTextIter* from, gint delta, Motion* m) {
    gint line = gtk_text_iter_get_line(from) + delta;
    gint last = vim_e_is_insert(e->mode) ? vim_e_last_line(e) : vim_e_last_text_line(e);
    if (line < 0) line = 0;
    if (line > last) line = last;
    if (line == gtk_text_iter_get_line(from)) return FALSE;
    vim_e_iter_line(e, &m->pos, line);
    if (e->want_eol) {
        vim_iter_line_end(&m->pos);
    } else {
        vim_iter_set_vcol(&m->pos, e->curswant, e->opt.tabstop);
    }
    m->linewise = TRUE;
    m->keep_want = TRUE;
    return TRUE;
}

static gboolean motion_line_nonblank(VimEngine* e, gint line, Motion* m) {
    if (line < 0 || line > vim_e_last_line(e)) return FALSE;
    vim_e_iter_line(e, &m->pos, line);
    vim_iter_first_nonblank(&m->pos);
    m->linewise = TRUE;
    return TRUE;
}

/* \<word\> pattern for * and # */
static gchar* word_pattern(const gchar* word, gboolean whole) {
    GString* s = g_string_new(NULL);
    gboolean kw = vim_char_class(g_utf8_get_char(word), FALSE) == 2;
    if (whole && kw) g_string_append(s, "\\<");
    for (const gchar* p = word; *p != '\0'; p++) {
        if (strchr("\\/.*$^~[]", *p) != NULL) g_string_append_c(s, '\\');
        g_string_append_c(s, *p);
    }
    if (whole && kw) g_string_append(s, "\\>");
    return g_string_free(s, FALSE);
}

static gboolean motion_section(VimEngine* e, const GtkTextIter* from, gint count, gboolean forward,
                               gunichar brace, Motion* m) {
    gint line = gtk_text_iter_get_line(from);
    gint last = vim_e_last_line(e);
    for (gint i = 0; i < count; i++) {
        for (;;) {
            line += forward ? 1 : -1;
            if (line <= 0 || line >= last) {
                line = CLAMP(line, 0, last);
                break;
            }
            GtkTextIter it;
            vim_e_iter_line(e, &it, line);
            if (gtk_text_iter_get_char(&it) == brace) break;
        }
    }
    vim_e_iter_line(e, &m->pos, line);
    if (forward && line == last && brace == '{') vim_iter_line_end(&m->pos);
    return !gtk_text_iter_equal(&m->pos, from);
}

gboolean vim_e_do_motion(VimEngine* e, const VimCmd* cmd, gboolean for_op, const GtkTextIter* from, Motion* m) {
    memset(m, 0, sizeof(*m));
    m->pos = *from;
    gint n = vim_cmd_count1(cmd);
    gunichar k = cmd->key[0], k1 = cmd->key[1];
    GtkTextIter p = *from;
    gint ts = e->opt.tabstop;

    if (k1 == 0) {
        switch (k) {
            case 'h':
                for (gint i = 0; i < n && !gtk_text_iter_starts_line(&p); i++) {
                    gtk_text_iter_backward_char(&p);
                }
                m->pos = p;
                return !gtk_text_iter_equal(&p, from);
            case 'l':
                for (gint i = 0; i < n && !gtk_text_iter_ends_line(&p); i++) {
                    if (!for_op) {
                        GtkTextIter next = p;
                        gtk_text_iter_forward_char(&next);
                        if (gtk_text_iter_ends_line(&next)) break;
                    }
                    gtk_text_iter_forward_char(&p);
                }
                m->pos = p;
                return !gtk_text_iter_equal(&p, from);
            case 'j': return motion_lines(e, from, n, m);
            case 'k': return motion_lines(e, from, -n, m);
            case 'w': case 'W':
                if (!vim_motion_word_forward(&p, n, k == 'W', for_op)) return FALSE;
                m->pos = p;
                return TRUE;
            case 'b': case 'B':
                if (!vim_motion_word_backward(&p, n, k == 'B')) return FALSE;
                m->pos = p;
                return TRUE;
            case 'e': case 'E':
                if (!vim_motion_word_end(&p, n, k == 'E')) return FALSE;
                m->pos = p;
                m->inclusive = TRUE;
                return TRUE;
            case '0':
                gtk_text_iter_set_line_offset(&p, 0);
                m->pos = p;
                return TRUE;
            case '^':
                vim_iter_first_nonblank(&p);
                m->pos = p;
                return TRUE;
            case '$': {
                gint line = gtk_text_iter_get_line(&p) + n - 1;
                if (line > vim_e_last_line(e)) return FALSE;
                vim_e_iter_line(e, &p, line);
                vim_iter_line_end(&p);
                m->want_eol = TRUE;
                if (!gtk_text_iter_starts_line(&p)) {
                    gtk_text_iter_backward_char(&p);
                    m->inclusive = TRUE;
                }
                m->pos = p;
                return TRUE;
            }
            case '|':
                vim_iter_set_vcol(&p, n - 1, ts);
                m->pos = p;
                return TRUE;
            case 'G':
                m->jump = TRUE;
                return motion_line_nonblank(e, cmd->count > 0 ? MIN(cmd->count - 1, vim_e_last_text_line(e))
                                                              : vim_e_last_text_line(e), m);
            case '+': return motion_line_nonblank(e, gtk_text_iter_get_line(&p) + n, m);
            case '-': return motion_line_nonblank(e, gtk_text_iter_get_line(&p) - n, m);
            case '_': return motion_line_nonblank(e, gtk_text_iter_get_line(&p) + n - 1, m);
            case 'f': case 'F': case 't': case 'T': {
                gboolean fwd = (k == 'f' || k == 't');
                gboolean till = (k == 't' || k == 'T');
                e->find_char = cmd->arg;
                e->find_kind = k;
                if (!vim_motion_find_char(&p, cmd->arg, n, fwd, till, FALSE)) return FALSE;
                m->pos = p;
                m->inclusive = fwd;
                return TRUE;
            }
            case ';': case ',': {
                if (e->find_kind == 0) return FALSE;
                gunichar kind = e->find_kind;
                if (k == ',') {
                    kind = kind == 'f' ? 'F' : kind == 'F' ? 'f' : kind == 't' ? 'T' : 't';
                }
                gboolean fwd = (kind == 'f' || kind == 't');
                gboolean till = (kind == 't' || kind == 'T');
                if (!vim_motion_find_char(&p, e->find_char, n, fwd, till, TRUE)) return FALSE;
                m->pos = p;
                m->inclusive = fwd;
                return TRUE;
            }
            case '%':
                m->jump = TRUE;
                if (cmd->count > 0) {
                    if (cmd->count > 100) return FALSE;
                    gint lines = vim_e_last_line(e) + 1;
                    return motion_line_nonblank(e, (cmd->count * lines + 99) / 100 - 1, m);
                }
                if (!vim_motion_match_pair(&p)) return FALSE;
                m->pos = p;
                m->inclusive = TRUE;
                m->numbered = TRUE;
                return TRUE;
            case '{': case '}':
                m->jump = TRUE;
                m->numbered = TRUE;
                if (!vim_motion_paragraph(&p, n, k == '}')) return FALSE;
                m->pos = p;
                return TRUE;
            case '(': case ')':
                m->jump = TRUE;
                m->numbered = TRUE;
                if (!vim_motion_sentence(&p, n, k == ')')) return FALSE;
                m->pos = p;
                return TRUE;
            case 'H': case 'M': case 'L': {
                gint first = 0, last = vim_e_last_line(e);
                if (e->host.visible_lines != NULL) e->host.visible_lines(e->hd, &first, &last);
                last = MIN(last, vim_e_last_line(e));
                first = MIN(first, last);
                gint line = k == 'H' ? MIN(first + n - 1, last)
                          : k == 'L' ? MAX(last - n + 1, first)
                                     : (first + last) / 2;
                m->jump = TRUE;
                return motion_line_nonblank(e, line, m);
            }
            case 'n': case 'N': {
                if (e->last_pat == NULL) {
                    vim_e_message(e, TRUE, "E35: No previous regular expression");
                    return FALSE;
                }
                gboolean fwd = (k == 'n') ? e->last_fwd : !e->last_fwd;
                if (!vim_e_search(e, e->last_pat, fwd, n, from, &p, FALSE)) return FALSE;
                m->pos = p;
                m->jump = TRUE;
                m->numbered = TRUE;
                return TRUE;
            }
            case '*': case '#': {
                GtkTextIter ws, we;
                if (!vim_e_word_under_cursor(e, &ws, &we)) {
                    vim_e_message(e, TRUE, "E348: No string under cursor");
                    return FALSE;
                }
                gchar* word = gtk_text_iter_get_slice(&ws, &we);
                gchar* pat = word_pattern(word, TRUE);
                g_free(word);
                g_free(e->last_pat);
                e->last_pat = pat;
                e->last_fwd = (k == '*');
                e->hl_active = TRUE;
                vim_e_history_add(e, '/', pat);
                GtkTextIter start = k == '#' ? ws : *from;
                gboolean ok = vim_e_search(e, pat, k == '*', n, &start, &p, FALSE);
                if (!ok) return FALSE;
                m->pos = p;
                m->jump = TRUE;
                return TRUE;
            }
            case '`': case '\'': {
                gunichar name = cmd->arg;
                GtkTextIter mk;
                if (!vim_e_get_mark(e, name, &mk)) {
                    vim_e_message(e, TRUE, "E20: Mark not set");
                    return FALSE;
                }
                m->jump = TRUE;
                m->numbered = TRUE;
                if (k == '\'') return motion_line_nonblank(e, gtk_text_iter_get_line(&mk), m);
                m->pos = mk;
                return TRUE;
            }
            default: return FALSE;
        }
    }
    if (k == 'g') {
        switch (k1) {
            case 'g':
                m->jump = TRUE;
                return motion_line_nonblank(e, cmd->count > 0 ? MIN(cmd->count - 1, vim_e_last_line(e)) : 0, m);
            case 'e': case 'E':
                if (!vim_motion_word_end_backward(&p, n, k1 == 'E')) return FALSE;
                m->pos = p;
                m->inclusive = TRUE;
                return TRUE;
            case '_': {
                gint line = gtk_text_iter_get_line(&p) + n - 1;
                if (line > vim_e_last_line(e)) return FALSE;
                vim_e_iter_line(e, &p, line);
                vim_iter_line_end(&p);
                while (!gtk_text_iter_starts_line(&p)) {
                    GtkTextIter prev = p;
                    gtk_text_iter_backward_char(&prev);
                    if (!vim_e_is_blank_char(gtk_text_iter_get_char(&prev))) break;
                    p = prev;
                }
                if (!gtk_text_iter_starts_line(&p)) gtk_text_iter_backward_char(&p);
                m->pos = p;
                m->inclusive = TRUE;
                return TRUE;
            }
            case 'j': return motion_lines(e, from, n, m);
            case 'k': return motion_lines(e, from, -n, m);
            case '0':
                gtk_text_iter_set_line_offset(&p, 0);
                m->pos = p;
                return TRUE;
            case '^':
                vim_iter_first_nonblank(&p);
                m->pos = p;
                return TRUE;
            case '$': {
                Motion mm;
                VimCmd c = *cmd;
                c.key[0] = '$';
                c.key[1] = 0;
                gboolean ok = vim_e_do_motion(e, &c, for_op, from, &mm);
                *m = mm;
                return ok;
            }
            case 'm': case 'M': {
                GtkTextIter end = p;
                vim_iter_line_end(&end);
                gint len = gtk_text_iter_get_line_offset(&end);
                gtk_text_iter_set_line_offset(&p, len / 2);
                m->pos = p;
                return TRUE;
            }
            case 'o': {
                gint target = MAX(0, n - 1);
                gtk_text_buffer_get_iter_at_offset(e->buf, &p, target);
                m->pos = p;
                m->jump = TRUE;
                return TRUE;
            }
            case '*': case '#': {
                GtkTextIter ws, we;
                if (!vim_e_word_under_cursor(e, &ws, &we)) return FALSE;
                gchar* word = gtk_text_iter_get_slice(&ws, &we);
                gchar* pat = word_pattern(word, FALSE);
                g_free(word);
                g_free(e->last_pat);
                e->last_pat = pat;
                e->last_fwd = (k1 == '*');
                e->hl_active = TRUE;
                vim_e_history_add(e, '/', pat);
                GtkTextIter start = k1 == '#' ? ws : *from;
                if (!vim_e_search(e, pat, k1 == '*', n, &start, &p, FALSE)) return FALSE;
                m->pos = p;
                m->jump = TRUE;
                return TRUE;
            }
            case 'd': case 'D': {
                GtkTextIter ws, we;
                if (!vim_e_word_under_cursor(e, &ws, &we)) return FALSE;
                gchar* word = gtk_text_iter_get_slice(&ws, &we);
                gchar* pat = word_pattern(word, TRUE);
                g_free(word);
                g_free(e->last_pat);
                e->last_pat = pat;
                e->last_fwd = TRUE;
                e->hl_active = TRUE;
                GRegex* re = vim_regex_compile(pat, FALSE, FALSE, NULL);
                GtkTextIter start, ms, me;
                gtk_text_buffer_get_start_iter(e->buf, &start);
                gboolean ok = vim_regex_search_at(e->buf, re, &start, &ms, &me);
                if (re != NULL) g_regex_unref(re);
                vim_e_highlight_search(e);
                if (!ok) return FALSE;
                m->pos = ms;
                m->jump = TRUE;
                return TRUE;
            }
            default: return FALSE;
        }
    }
    if (k == '[' || k == ']') {
        gboolean fwd = (k == ']');
        if (k1 == '(' || k1 == '{' || k1 == ')' || k1 == '}') {
            if (!vim_motion_unmatched(&p, k1, n)) return FALSE;
            m->pos = p;
            m->jump = TRUE;
            return TRUE;
        }
        /* [[ ]] : '{' in column 0; [] ][ : '}' in column 0 */
        gunichar brace = (k1 == k) ? '{' : '}';
        m->jump = TRUE;
        return motion_section(e, from, n, fwd, brace, m);
    }
    return FALSE;
}

/* Apply a motion as a cursor move (Normal and Visual modes) */
void vim_e_move_cursor(VimEngine* e, const Motion* m) {
    if (m->jump) vim_e_push_jump(e);
    GtkTextIter pos = m->pos;
    if (e->mode == VIM_MODE_NORMAL || (vim_e_is_visual(e->mode) && !m->want_eol)) {
        vim_iter_clamp_normal(&pos);
    }
    vim_e_set_cursor(e, &pos);
    if (!m->keep_want) {
        e->curswant = vim_iter_vcol(&pos, e->opt.tabstop);
        e->want_eol = m->want_eol;
    }
    if (e->mode == VIM_MODE_VISUAL_BLOCK) {
        e->vis_eol = m->want_eol || (m->keep_want && e->want_eol);
        vim_e_update_visual(e);
    }
}
