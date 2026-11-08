/* Vim engine: command line, search and Ex commands */
#include "vim/vim_engine_private.h"
#include "vim/vim_ex.h"
#include "vim/vim_keys.h"
#include "vim/vim_motion.h"
#include "vim/vim_regex.h"
#include <stdlib.h>
#include <string.h>

static void iter_line(VimEngine* e, GtkTextIter* it, gint line) {
    gtk_text_buffer_get_iter_at_line(e->buf, it, line);
}

static void feed_one(VimEngine* e, gunichar key) {
    gchar buf[8] = {0};
    g_unichar_to_utf8(key, buf);
    vim_e_feed_internal(e, buf);
}

static void clamp_cursor(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    GtkTextIter c = it;
    vim_iter_clamp_normal(&c);
    if (!gtk_text_iter_equal(&c, &it)) gtk_text_buffer_place_cursor(e->buf, &c);
}

/* ------------------------------------------------------------------------- */
/* Search                                                                    */
/* ------------------------------------------------------------------------- */

static GRegex* compile(VimEngine* e, const gchar* pattern, gboolean quiet) {
    GError* err = NULL;
    GRegex* re = vim_regex_compile(pattern, e->opt.ignorecase, e->opt.smartcase, &err);
    if (re == NULL) {
        if (!quiet) vim_e_message(e, TRUE, "E486: Invalid pattern: %s", pattern);
        g_clear_error(&err);
    }
    return re;
}

gboolean vim_e_search(VimEngine* e, const gchar* pattern, gboolean forward, gint count,
                      const GtkTextIter* from, GtkTextIter* match, gboolean quiet) {
    GRegex* re = compile(e, pattern, quiet);
    if (re == NULL) return FALSE;
    GtkTextIter pos = *from, ms, me;
    gboolean any_wrap = FALSE;
    for (gint i = 0; i < MAX(count, 1); i++) {
        gboolean wrapped = FALSE;
        if (!vim_regex_search(e->buf, re, &pos, forward, e->opt.wrapscan, &wrapped, &ms, &me)) {
            if (!quiet) {
                if (e->opt.wrapscan) {
                    vim_e_message(e, TRUE, "E486: Pattern not found: %s", pattern);
                } else {
                    vim_e_message(e, TRUE, "E38%d: Search hit %s without match for: %s", forward ? 5 : 4,
                                  forward ? "BOTTOM" : "TOP", pattern);
                }
            }
            g_regex_unref(re);
            return FALSE;
        }
        any_wrap = any_wrap || wrapped;
        pos = ms;
    }
    *match = pos;
    if (!quiet) {
        gint index = 0;
        gint total = vim_regex_count(e->buf, re, &pos, &index);
        if (any_wrap) {
            vim_e_message(e, FALSE, "search hit %s, continuing at %s  [%d/%d]",
                          forward ? "BOTTOM" : "TOP", forward ? "TOP" : "BOTTOM", index, total);
        } else {
            vim_e_message(e, FALSE, "%c%s  [%d/%d]", forward ? '/' : '?', pattern, index, total);
        }
    }
    g_regex_unref(re);
    e->hl_active = TRUE;
    vim_e_highlight_search(e);
    return TRUE;
}

void vim_e_highlight_search(VimEngine* e) {
    if (!e->hl_active || !e->opt.hlsearch || e->last_pat == NULL) {
        GtkTextIter s, t;
        gtk_text_buffer_get_bounds(e->buf, &s, &t);
        gtk_text_buffer_remove_tag(e->buf, e->tag_search, &s, &t);
        return;
    }
    GRegex* re = compile(e, e->last_pat, TRUE);
    vim_regex_tag_matches(e->buf, re, e->tag_search, 20000);
    if (re != NULL) g_regex_unref(re);
    e->buffer_dirty = FALSE;
}

gboolean vim_e_word_under_cursor(VimEngine* e, GtkTextIter* ws, GtkTextIter* we) {
    GtkTextIter p;
    vim_e_cursor(e, &p);
    gint want = 2;
    GtkTextIter q = p;
    while (!gtk_text_iter_ends_line(&q) && vim_char_class(gtk_text_iter_get_char(&q), FALSE) != 2) {
        gtk_text_iter_forward_char(&q);
    }
    if (gtk_text_iter_ends_line(&q)) {
        /* No keyword: the non-blank text under or after the cursor */
        q = p;
        while (!gtk_text_iter_ends_line(&q) && vim_char_class(gtk_text_iter_get_char(&q), FALSE) == 0) {
            gtk_text_iter_forward_char(&q);
        }
        if (gtk_text_iter_ends_line(&q)) return FALSE;
        want = 1;
    }
    *ws = q;
    while (!gtk_text_iter_starts_line(ws)) {
        GtkTextIter prev = *ws;
        gtk_text_iter_backward_char(&prev);
        if (vim_char_class(gtk_text_iter_get_char(&prev), FALSE) != want) break;
        *ws = prev;
    }
    *we = q;
    while (!gtk_text_iter_ends_line(we) && vim_char_class(gtk_text_iter_get_char(we), FALSE) == want) {
        gtk_text_iter_forward_char(we);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Command line                                                              */
/* ------------------------------------------------------------------------- */

void vim_e_history_add(VimEngine* e, gunichar prompt, const gchar* text) {
    if (text == NULL || text[0] == '\0') return;
    GPtrArray* h = prompt == ':' ? e->hist_cmd : e->hist_search;
    for (guint i = 0; i < h->len; i++) {
        if (strcmp(g_ptr_array_index(h, i), text) == 0) {
            g_ptr_array_remove_index(h, i);
            break;
        }
    }
    g_ptr_array_add(h, g_strdup(text));
    if (h->len > 200) g_ptr_array_remove_index(h, 0);
}

void vim_e_cmdline_open(VimEngine* e, gunichar prompt, const gchar* text) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    e->cl_prompt = prompt;
    e->cl_return_mode = e->mode == VIM_MODE_CMDLINE ? VIM_MODE_NORMAL : e->mode;
    e->cl_origin = gtk_text_iter_get_offset(&cur);
    GPtrArray* h = prompt == ':' ? e->hist_cmd : e->hist_search;
    e->hist_pos = (gint)h->len;
    g_free(e->cl_prefill);
    e->cl_prefill = g_strdup(text != NULL ? text : "");
    e->mode = VIM_MODE_CMDLINE;
    if (e->replaying > 0 || e->host.cmdline_open == NULL) {
        e->cl_internal = TRUE;
        g_string_assign(e->cl_text, text != NULL ? text : "");
    } else {
        e->cl_internal = FALSE;
        e->host.cmdline_open(e->hd, prompt, text != NULL ? text : "");
    }
    vim_e_state_changed(e);
}

static void clear_incsearch(VimEngine* e) {
    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(e->buf, &s, &t);
    gtk_text_buffer_remove_tag(e->buf, e->tag_incsearch, &s, &t);
}

static void close_cmdline(VimEngine* e) {
    e->cl_prompt = 0;
    e->mode = e->cl_return_mode;
    clear_incsearch(e);
}

static void restore_origin(VimEngine* e) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset(e->buf, &it, e->cl_origin);
    gtk_text_buffer_place_cursor(e->buf, &it);
}

static void record_cmdline(VimEngine* e, const gchar* text, gunichar end_key) {
    if (e->rec_reg == 0 || e->replaying > 0 || e->cl_internal) return;
    const gchar* typed = text;
    if (e->cl_prefill != NULL && g_str_has_prefix(text, e->cl_prefill)) typed = text + strlen(e->cl_prefill);
    g_string_append(e->rec, typed);
    g_string_append_unichar(e->rec, end_key);
}

void vim_engine_cmdline_submit(VimEngine* e, const gchar* text) {
    if (e->mode != VIM_MODE_CMDLINE) return;
    gunichar prompt = e->cl_prompt;
    record_cmdline(e, text, VIM_KEY_CR);
    restore_origin(e);
    close_cmdline(e);
    if (prompt == ':') {
        vim_e_history_add(e, ':', text);
        vim_engine_ex(e, text);
        vim_e_state_changed(e);
        return;
    }
    const gchar* pattern = text;
    if (pattern == NULL || pattern[0] == '\0') {
        if (e->last_pat == NULL) {
            vim_e_message(e, TRUE, "E35: No previous regular expression");
            e->cl_has_op = FALSE;
            vim_e_state_changed(e);
            return;
        }
        pattern = e->last_pat;
    }
    gchar* pat = g_strdup(pattern);
    vim_e_history_add(e, '/', pat);
    g_free(e->last_pat);
    e->last_pat = pat;
    e->last_fwd = (prompt == '/');
    GtkTextIter from, match;
    vim_e_cursor(e, &from);
    if (vim_e_search(e, pat, e->last_fwd, 1, &from, &match, FALSE)) {
        vim_e_run_motion_after_search(e, &match);
    } else {
        e->cl_has_op = FALSE;
    }
    vim_e_state_changed(e);
}

void vim_engine_cmdline_cancel(VimEngine* e) {
    if (e->mode != VIM_MODE_CMDLINE) return;
    if (e->rec_reg != 0 && e->replaying == 0 && !e->cl_internal) g_string_append_unichar(e->rec, VIM_KEY_ESC);
    restore_origin(e);
    close_cmdline(e);
    e->cl_has_op = FALSE;
    vim_e_highlight_search(e);
    if (e->mode != VIM_MODE_NORMAL) vim_e_update_visual(e);
    vim_e_state_changed(e);
}

void vim_engine_cmdline_changed(VimEngine* e, const gchar* text) {
    if (e->mode != VIM_MODE_CMDLINE || (e->cl_prompt != '/' && e->cl_prompt != '?')) return;
    if (!e->opt.incsearch) return;
    clear_incsearch(e);
    restore_origin(e);
    if (text == NULL || text[0] == '\0') {
        vim_e_highlight_search(e);
        vim_e_state_changed(e);
        return;
    }
    GRegex* re = compile(e, text, TRUE);
    if (re == NULL) return;
    GtkTextIter from, ms, me;
    vim_e_cursor(e, &from);
    gboolean wrapped = FALSE;
    if (vim_regex_search(e->buf, re, &from, e->cl_prompt == '/', e->opt.wrapscan, &wrapped, &ms, &me)) {
        gtk_text_buffer_apply_tag(e->buf, e->tag_incsearch, &ms, &me);
        gtk_text_buffer_place_cursor(e->buf, &ms);
    }
    if (e->opt.hlsearch) vim_regex_tag_matches(e->buf, re, e->tag_search, 20000);
    g_regex_unref(re);
    vim_e_state_changed(e);
}

gchar* vim_engine_cmdline_history(VimEngine* e, gint dir, const gchar* prefix) {
    GPtrArray* h = e->cl_prompt == ':' ? e->hist_cmd : e->hist_search;
    gint pos = e->hist_pos;
    const gchar* pre = prefix != NULL ? prefix : "";
    for (;;) {
        pos += dir < 0 ? -1 : 1;
        if (pos < 0) return NULL;
        if (pos >= (gint)h->len) {
            if (e->hist_pos >= (gint)h->len) return NULL;
            e->hist_pos = (gint)h->len;
            return g_strdup(pre);
        }
        const gchar* entry = g_ptr_array_index(h, pos);
        if (g_str_has_prefix(entry, pre)) {
            e->hist_pos = pos;
            return g_strdup(entry);
        }
    }
}

static const gchar* const OPTION_NAMES[] = {
    "autoindent", "clipboard", "expandtab", "hlsearch", "ignorecase", "incsearch", "nonumber",
    "number", "shiftwidth", "smartcase", "softtabstop", "tabstop", "textwidth", "wrap", "wrapscan", NULL,
};

static gint compare_names(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

/* Names starting with typed, each with prefix in front (sorted later) */
static void add_matches(GPtrArray* out, const gchar* prefix, const gchar* typed, const gchar* const* names) {
    for (gint i = 0; names[i] != NULL; i++) {
        if (prefix[0] != '\0' && g_str_has_prefix(names[i], prefix)) continue; /* no "nono..." */
        if (g_str_has_prefix(names[i], typed)) g_ptr_array_add(out, g_strconcat(prefix, names[i], NULL));
    }
}

gchar** vim_engine_cmdline_matches(VimEngine* e, const gchar* text, gsize* word_start) {
    (void)e;
    if (word_start != NULL) *word_start = 0;
    if (text == NULL) return NULL;
    const gchar* space = strrchr(text, ' ');
    const gchar* word;
    GPtrArray* m = g_ptr_array_new();
    if (g_str_has_prefix(text, "set ") || g_str_has_prefix(text, "se ")) {
        word = space + 1;
        add_matches(m, "", word, OPTION_NAMES);
        if (g_str_has_prefix(word, "no")) add_matches(m, "no", word + 2, OPTION_NAMES);
    } else if (space == NULL) {
        /* Command name after an optional range */
        word = text;
        while (*word != '\0' && !g_ascii_isalpha(*word)) word++;
        add_matches(m, "", word, vim_ex_command_names());
    } else {
        g_ptr_array_free(m, TRUE);
        return NULL;
    }
    if (m->len == 0) {
        g_ptr_array_free(m, TRUE);
        return NULL;
    }
    g_ptr_array_sort(m, compare_names);
    g_ptr_array_add(m, NULL);
    if (word_start != NULL) *word_start = (gsize)(word - text);
    return (gchar**)g_ptr_array_free(m, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Ex ranges                                                                 */
/* ------------------------------------------------------------------------- */

static gboolean resolve_address(VimEngine* e, const VimAddr* a, gint base, gint* line, gchar** err) {
    gint last = vim_e_last_line(e);
    gint l = base;
    switch (a->kind) {
        case VIM_ADDR_NUMBER: l = a->number - 1; break;
        case VIM_ADDR_CURRENT: case VIM_ADDR_NONE: l = base; break;
        case VIM_ADDR_LAST: l = last; break;
        case VIM_ADDR_MARK: {
            GtkTextIter it;
            if (!vim_e_get_mark(e, a->mark, &it)) {
                *err = g_strdup("E20: Mark not set");
                return FALSE;
            }
            l = gtk_text_iter_get_line(&it);
            break;
        }
        case VIM_ADDR_SEARCH_FWD: case VIM_ADDR_SEARCH_BWD:
        case VIM_ADDR_SEARCH_NEXT: case VIM_ADDR_SEARCH_PREV: {
            const gchar* pat = a->pattern != NULL ? a->pattern : e->last_pat;
            if (pat == NULL) {
                *err = g_strdup("E35: No previous regular expression");
                return FALSE;
            }
            if (a->pattern != NULL) {
                g_free(e->last_pat);
                e->last_pat = g_strdup(a->pattern);
                pat = e->last_pat;
            }
            gboolean fwd = a->kind == VIM_ADDR_SEARCH_FWD || a->kind == VIM_ADDR_SEARCH_NEXT;
            GRegex* re = compile(e, pat, TRUE);
            if (re == NULL) {
                *err = g_strdup_printf("E486: Pattern not found: %s", pat);
                return FALSE;
            }
            GtkTextIter from, ms, me;
            iter_line(e, &from, base);
            if (fwd) vim_iter_line_end(&from);
            gboolean wrapped;
            gboolean ok = vim_regex_search(e->buf, re, &from, fwd, e->opt.wrapscan, &wrapped, &ms, &me);
            g_regex_unref(re);
            if (!ok) {
                *err = g_strdup_printf("E486: Pattern not found: %s", pat);
                return FALSE;
            }
            l = gtk_text_iter_get_line(&ms);
            break;
        }
    }
    l += a->offset;
    *line = l;
    return TRUE;
}

/* Lines of the range (0-based); default: the current line */
static gboolean resolve_range(VimEngine* e, const VimExCmd* c, gint* l1, gint* l2, gchar** err) {
    gint cur = vim_e_cursor_line(e);
    if (c->naddr == 0) {
        *l1 = *l2 = cur;
        return TRUE;
    }
    gint a = cur, b = cur;
    if (!resolve_address(e, &c->addr[0], cur, &a, err)) return FALSE;
    if (c->naddr == 1) {
        b = a;
    } else if (!resolve_address(e, &c->addr[1], c->semicolon ? a : cur, &b, err)) {
        return FALSE;
    }
    if (a > b) {
        gint t = a;
        a = b;
        b = t;
    }
    *l1 = a;
    *l2 = b;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* :substitute                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint start, end;   /* byte offsets in the range text */
    gchar* text;
} SubEdit;

static void do_substitute(VimEngine* e, gint l1, gint l2, const gchar* pattern, const gchar* rep,
                          const gchar* flags, gint count) {
    gboolean global = FALSE, count_only = FALSE, no_error = FALSE;
    gint icase = -1;
    for (const gchar* f = flags; f != NULL && *f; f++) {
        switch (*f) {
            case 'g': global = !global; break;
            case 'n': count_only = TRUE; break;
            case 'e': no_error = TRUE; break;
            case 'i': icase = 1; break;
            case 'I': icase = 0; break;
            default: break;
        }
    }
    gint last = vim_e_last_line(e);
    if (count > 0) {
        l1 = l2;
        l2 = MIN(l1 + count - 1, last);
    }
    if (l1 < 0 || l2 > last) {
        vim_e_message(e, TRUE, "E16: Invalid range");
        return;
    }
    GError* err = NULL;
    GRegex* re = vim_regex_compile(pattern, icase >= 0 ? icase == 1 : e->opt.ignorecase,
                                   icase >= 0 ? FALSE : e->opt.smartcase, &err);
    if (re == NULL) {
        vim_e_message(e, TRUE, "E486: Invalid pattern: %s", pattern);
        g_clear_error(&err);
        return;
    }
    GtkTextIter rs, rt;
    iter_line(e, &rs, l1);
    iter_line(e, &rt, l2);
    vim_iter_line_end(&rt);
    gint base = gtk_text_iter_get_offset(&rs);
    gchar* text = gtk_text_buffer_get_slice(e->buf, &rs, &rt, TRUE);
    GArray* edits = g_array_new(FALSE, FALSE, sizeof(SubEdit));
    GMatchInfo* mi = NULL;
    gint lines_changed = 0, last_line_done = -1, line_idx = 0;
    const gchar* scan = text;
    g_regex_match_full(re, text, -1, 0, 0, &mi, NULL);
    while (g_match_info_matches(mi)) {
        gint ms, me;
        g_match_info_fetch_pos(mi, 0, &ms, &me);
        while (scan < text + ms) {
            if (*scan == '\n') line_idx++;
            scan++;
        }
        if (global || line_idx != last_line_done) {
            if (line_idx != last_line_done) lines_changed++;
            last_line_done = line_idx;
            SubEdit ed = {ms, me, count_only ? NULL : vim_regex_expand(rep, mi)};
            g_array_append_val(edits, ed);
        }
        g_match_info_next(mi, NULL);
    }
    g_match_info_free(mi);
    g_regex_unref(re);
    gint n = (gint)edits->len;
    if (n == 0) {
        if (!no_error) vim_e_message(e, TRUE, "E486: Pattern not found: %s", pattern);
    } else if (count_only) {
        vim_e_message(e, FALSE, "%d match%s on %d line%s", n, n == 1 ? "" : "es", lines_changed,
                      lines_changed == 1 ? "" : "s");
    } else {
        GtkTextMark* last_mark = NULL;
        for (gint i = n - 1; i >= 0; i--) {
            SubEdit* ed = &g_array_index(edits, SubEdit, i);
            glong cs = g_utf8_pointer_to_offset(text, text + ed->start);
            glong ce = g_utf8_pointer_to_offset(text, text + ed->end);
            GtkTextIter a, b;
            gtk_text_buffer_get_iter_at_offset(e->buf, &a, base + (gint)cs);
            gtk_text_buffer_get_iter_at_offset(e->buf, &b, base + (gint)ce);
            vim_e_delete(e, &a, &b);
            if (last_mark == NULL) last_mark = gtk_text_buffer_create_mark(e->buf, NULL, &a, TRUE);
            vim_e_insert_text(e, &a, ed->text);
        }
        GtkTextIter lm;
        gtk_text_buffer_get_iter_at_mark(e->buf, &lm, last_mark);
        vim_e_set_mark(e, '.', &lm);
        vim_e_set_cursor_line(e, gtk_text_iter_get_line(&lm), TRUE);
        gtk_text_buffer_delete_mark(e->buf, last_mark);
        if (lines_changed > 2 || n > 2) {
            vim_e_message(e, FALSE, "%d substitution%s on %d line%s", n, n == 1 ? "" : "s", lines_changed,
                          lines_changed == 1 ? "" : "s");
        }
    }
    for (gint i = 0; i < n; i++) g_free(g_array_index(edits, SubEdit, i).text);
    g_array_free(edits, TRUE);
    g_free(text);
}

/* ~ in a replacement is the previous replacement */
static gchar* expand_tilde(const gchar* rep, const gchar* prev) {
    GString* s = g_string_new(NULL);
    for (const gchar* p = rep; *p; p++) {
        if (*p == '\\' && p[1] != '\0') {
            g_string_append_c(s, *p);
            g_string_append_c(s, *++p);
        } else if (*p == '~') {
            g_string_append(s, prev != NULL ? prev : "");
        } else {
            g_string_append_c(s, *p);
        }
    }
    return g_string_free(s, FALSE);
}

static gint parse_count(const gchar* s) {
    while (*s == ' ') s++;
    if (!g_ascii_isdigit(*s)) return 0;
    return atoi(s);
}

static void ex_substitute(VimEngine* e, const VimExCmd* c, gint l1, gint l2) {
    const gchar* name = c->name;
    gchar* pattern = NULL;
    gchar* rep = NULL;
    gchar* rest = NULL;
    if (strcmp(name, "substitute") == 0 &&
        vim_ex_split_pattern(c->args, &pattern, &rep, &rest, TRUE)) {
        if (pattern[0] == '\0') {
            g_free(pattern);
            pattern = g_strdup(e->last_pat != NULL ? e->last_pat : "");
        }
        if (rep == NULL) rep = g_strdup("");
        gchar* expanded = expand_tilde(rep, e->sub_rep);
        g_free(rep);
        rep = expanded;
    } else {
        /* :s [flags], :&, :&&, :~ repeat the last substitution */
        rest = g_strdup(c->args);
        if (strcmp(name, "&&") == 0) {
            gchar* f = g_strconcat("&", rest, NULL);
            g_free(rest);
            rest = f;
        }
        const gchar* p = (strcmp(name, "~") == 0) ? e->last_pat : e->sub_pat;
        if (p == NULL || e->sub_rep == NULL) {
            vim_e_message(e, TRUE, "E35: No previous regular expression");
            g_free(rest);
            return;
        }
        pattern = g_strdup(p);
        rep = g_strdup(e->sub_rep);
    }
    if (pattern[0] == '\0') {
        vim_e_message(e, TRUE, "E35: No previous regular expression");
        g_free(pattern);
        g_free(rep);
        g_free(rest);
        return;
    }
    GString* flags = g_string_new(NULL);
    const gchar* r = rest != NULL ? rest : "";
    if (*r == '&') {
        if (e->sub_flags != NULL) g_string_append(flags, e->sub_flags);
        r++;
    }
    while (*r && strchr("gcenriI#lp", *r) != NULL) g_string_append_c(flags, *r++);
    gint count = parse_count(r);
    g_free(e->sub_pat);
    e->sub_pat = g_strdup(pattern);
    g_free(e->sub_rep);
    e->sub_rep = g_strdup(rep);
    g_free(e->sub_flags);
    e->sub_flags = g_strdup(flags->str);
    g_free(e->last_pat);
    e->last_pat = g_strdup(pattern);
    e->hl_active = TRUE;
    do_substitute(e, l1, l2, pattern, rep, flags->str, count);
    vim_e_highlight_search(e);
    g_string_free(flags, TRUE);
    g_free(pattern);
    g_free(rep);
    g_free(rest);
}

void vim_e_substitute_repeat(VimEngine* e, gint l1, gint l2, gboolean keep_flags, gboolean whole) {
    (void)whole;
    if (e->sub_pat == NULL || e->sub_rep == NULL) {
        vim_e_message(e, TRUE, "E35: No previous regular expression");
        return;
    }
    gchar* pat = g_strdup(e->sub_pat);
    gchar* rep = g_strdup(e->sub_rep);
    gtk_text_buffer_begin_user_action(e->buf);
    do_substitute(e, l1, l2, pat, rep, keep_flags ? e->sub_flags : "", 0);
    gtk_text_buffer_end_user_action(e->buf);
    g_free(pat);
    g_free(rep);
}

/* ------------------------------------------------------------------------- */
/* :global                                                                   */
/* ------------------------------------------------------------------------- */

static void ex_global(VimEngine* e, const VimExCmd* c, gint l1, gint l2, gboolean invert) {
    if (e->global_depth > 0) {
        vim_e_message(e, TRUE, "E147: Cannot do :global recursive");
        return;
    }
    gchar* pattern = NULL;
    gchar* rest = NULL;
    if (!vim_ex_split_pattern(c->args, &pattern, NULL, &rest, FALSE)) {
        vim_e_message(e, TRUE, "E476: Invalid command");
        g_free(rest);
        return;
    }
    if (pattern[0] == '\0') {
        g_free(pattern);
        pattern = g_strdup(e->last_pat != NULL ? e->last_pat : "");
    }
    GRegex* re = compile(e, pattern, FALSE);
    if (re == NULL) {
        g_free(pattern);
        g_free(rest);
        return;
    }
    g_free(e->last_pat);
    e->last_pat = g_strdup(pattern);
    GPtrArray* marks = g_ptr_array_new();
    for (gint l = l1; l <= l2; l++) {
        gchar* text = vim_e_line_text(e, l);
        gboolean hit = g_regex_match(re, text, 0, NULL);
        g_free(text);
        if (hit != invert) {
            GtkTextIter it;
            iter_line(e, &it, l);
            g_ptr_array_add(marks, gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE));
        }
    }
    g_regex_unref(re);
    const gchar* cmd = (rest == NULL || rest[0] == '\0') ? "p" : rest;
    e->global_depth++;
    for (guint i = 0; i < marks->len; i++) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(e->buf, &it, g_ptr_array_index(marks, i));
        gtk_text_iter_set_line_offset(&it, 0);
        gtk_text_buffer_place_cursor(e->buf, &it);
        vim_engine_ex(e, cmd);
    }
    e->global_depth--;
    for (guint i = 0; i < marks->len; i++) gtk_text_buffer_delete_mark(e->buf, g_ptr_array_index(marks, i));
    if (marks->len == 0) vim_e_message(e, TRUE, "E486: Pattern not found: %s", pattern);
    g_ptr_array_free(marks, TRUE);
    g_free(pattern);
    g_free(rest);
}

/* ------------------------------------------------------------------------- */
/* Other Ex commands                                                         */
/* ------------------------------------------------------------------------- */

/* "[x] [count]" arguments of :d and :y */
static void reg_and_count(const gchar* args, gunichar* reg, gint* count) {
    *reg = 0;
    *count = 0;
    const gchar* p = args;
    while (*p == ' ') p++;
    if (*p != '\0' && !g_ascii_isdigit(*p)) {
        *reg = (gunichar)(guchar)*p;
        p++;
    }
    *count = parse_count(p);
}

static gboolean target_line(VimEngine* e, const gchar* args, gint* line, gchar** err) {
    VimExCmd t;
    if (!vim_ex_parse(args, &t, err) || t.naddr == 0) {
        if (t.name != NULL) vim_ex_cmd_clear(&t);
        if (*err == NULL) *err = g_strdup("E14: Invalid address");
        return FALSE;
    }
    gint l1 = 0, l2 = 0;
    gboolean ok = resolve_range(e, &t, &l1, &l2, err);
    vim_ex_cmd_clear(&t);
    *line = l2;
    return ok;
}

static gchar* lines_with_newline(VimEngine* e, gint l1, gint l2) {
    GString* s = g_string_new(NULL);
    for (gint l = l1; l <= l2; l++) {
        gchar* t = vim_e_line_text(e, l);
        g_string_append(s, t);
        g_string_append_c(s, '\n');
        g_free(t);
    }
    return g_string_free(s, FALSE);
}

static gint compare_lines(gconstpointer a, gconstpointer b, gpointer data) {
    const gchar* x = *(const gchar* const*)a;
    const gchar* y = *(const gchar* const*)b;
    const gchar* flags = data;
    if (strchr(flags, 'n') != NULL) {
        const gchar* px = x;
        const gchar* py = y;
        while (*px && !g_ascii_isdigit(*px) && *px != '-') px++;
        while (*py && !g_ascii_isdigit(*py) && *py != '-') py++;
        gboolean hx = *px != '\0', hy = *py != '\0';
        if (hx != hy) return hx ? 1 : -1; /* lines without number first */
        gint64 vx = g_ascii_strtoll(px, NULL, 10), vy = g_ascii_strtoll(py, NULL, 10);
        return vx < vy ? -1 : vx > vy ? 1 : 0;
    }
    if (strchr(flags, 'i') != NULL) return g_ascii_strcasecmp(x, y);
    return strcmp(x, y);
}

static void ex_sort(VimEngine* e, gint l1, gint l2, gboolean reverse, const gchar* flags) {
    GPtrArray* lines = g_ptr_array_new_with_free_func(g_free);
    for (gint l = l1; l <= l2; l++) g_ptr_array_add(lines, vim_e_line_text(e, l));
    g_ptr_array_sort_with_data(lines, compare_lines, (gpointer)flags);
    GString* out = g_string_new(NULL);
    gboolean unique = strchr(flags, 'u') != NULL;
    const gchar* prev = NULL;
    gint kept = 0;
    for (guint i = 0; i < lines->len; i++) {
        guint idx = reverse ? lines->len - 1 - i : i;
        const gchar* t = g_ptr_array_index(lines, idx);
        if (unique && prev != NULL && compare_lines(&prev, &t, (gpointer)flags) == 0) continue;
        if (kept > 0) g_string_append_c(out, '\n');
        g_string_append(out, t);
        prev = t;
        kept++;
    }
    GtkTextIter s, t;
    iter_line(e, &s, l1);
    iter_line(e, &t, l2);
    vim_iter_line_end(&t);
    vim_e_delete(e, &s, &t);
    vim_e_insert_text(e, &s, out->str);
    vim_e_set_cursor_line(e, l1, TRUE);
    g_string_free(out, TRUE);
    g_ptr_array_free(lines, TRUE);
}

static void ex_retab(VimEngine* e, gint l1, gint l2, const gchar* args) {
    gint old_ts = e->opt.tabstop;
    gint new_ts = parse_count(args);
    if (new_ts <= 0) new_ts = old_ts;
    for (gint l = l1; l <= l2; l++) {
        gchar* text = vim_e_line_text(e, l);
        if (strchr(text, '\t') == NULL && e->opt.expandtab) {
            g_free(text);
            continue;
        }
        /* Expand tabs with the old tabstop, then rebuild the indent */
        GString* expanded = g_string_new(NULL);
        gint v = 0;
        gboolean leading = TRUE;
        gint indent = 0;
        for (const gchar* p = text; *p; p = g_utf8_next_char(p)) {
            gunichar ch = g_utf8_get_char(p);
            if (ch == '\t' && (e->opt.expandtab || leading)) {
                gint w = old_ts - (v % old_ts);
                for (gint i = 0; i < w; i++) g_string_append_c(expanded, ' ');
                v += w;
            } else {
                if (ch != ' ' && ch != '\t') leading = FALSE;
                g_string_append_unichar(expanded, ch);
                v++;
            }
            if (leading) indent = v;
        }
        GString* out = g_string_new(NULL);
        if (!e->opt.expandtab) {
            for (gint i = 0; i < indent / new_ts; i++) g_string_append_c(out, '\t');
            for (gint i = 0; i < indent % new_ts; i++) g_string_append_c(out, ' ');
        } else {
            for (gint i = 0; i < indent; i++) g_string_append_c(out, ' ');
        }
        const gchar* body = expanded->str;
        gint skipped = 0;
        while (*body == ' ' && skipped < indent) {
            body++;
            skipped++;
        }
        g_string_append(out, body);
        if (strcmp(out->str, text) != 0) {
            GtkTextIter s, t;
            iter_line(e, &s, l);
            t = s;
            vim_iter_line_end(&t);
            vim_e_delete(e, &s, &t);
            vim_e_insert_text(e, &s, out->str);
        }
        g_string_free(out, TRUE);
        g_string_free(expanded, TRUE);
        g_free(text);
    }
    e->opt.tabstop = new_ts;
}

static gchar* display_text(const gchar* text, gsize max) {
    gchar* shown = vim_keys_to_display(text);
    GString* s = g_string_new(NULL);
    for (const gchar* p = shown; *p; p++) {
        if (*p == '\n') g_string_append(s, "^J");
        else g_string_append_c(s, *p);
    }
    g_free(shown);
    if (s->len > max) {
        const gchar* cut = g_utf8_find_prev_char(s->str, s->str + max);
        g_string_truncate(s, cut != NULL ? (gsize)(cut - s->str) : max);
        g_string_append(s, "…");
    }
    return g_string_free(s, FALSE);
}

static void ex_registers(VimEngine* e, const gchar* filter) {
    GString* out = g_string_new("Type Name Content");
    const gchar* order = "\"0123456789abcdefghijklmnopqrstuvwxyz-.:/+*";
    for (const gchar* p = order; *p; p++) {
        if (filter != NULL && filter[0] != '\0' && strchr(filter, *p) == NULL) continue;
        VimRegister r;
        if (!vim_e_reg_get(e, (gunichar)*p, &r) || r.text == NULL || r.text[0] == '\0') continue;
        gchar* shown = display_text(r.text, 70);
        g_string_append_printf(out, "\n  %c  \"%c   %s", r.type == REG_LINE ? 'l' : r.type == REG_BLOCK ? 'b' : 'c',
                               *p, shown);
        g_free(shown);
    }
    vim_e_message(e, FALSE, "%s", out->str);
    g_string_free(out, TRUE);
}

static void ex_marks(VimEngine* e) {
    GString* out = g_string_new("mark line  col file/text");
    const gchar* order = "'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\"[]^.<>";
    for (const gchar* p = order; *p; p++) {
        GtkTextIter it;
        if (!vim_e_get_mark(e, (gunichar)*p, &it)) continue;
        gchar* line = vim_e_line_text(e, gtk_text_iter_get_line(&it));
        gchar* shown = display_text(g_strstrip(line), 50);
        g_string_append_printf(out, "\n %c %6d %4d %s", *p, gtk_text_iter_get_line(&it) + 1,
                               gtk_text_iter_get_line_offset(&it), shown);
        g_free(shown);
        g_free(line);
    }
    vim_e_message(e, FALSE, "%s", out->str);
    g_string_free(out, TRUE);
}

static void ex_jumps(VimEngine* e) {
    GString* out = g_string_new(" jump line  col text");
    gint n = (gint)e->jumps->len;
    for (gint i = MAX(0, n - 20); i < n; i++) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(e->buf, &it, g_ptr_array_index(e->jumps, i));
        gchar* line = vim_e_line_text(e, gtk_text_iter_get_line(&it));
        gchar* shown = display_text(g_strstrip(line), 50);
        g_string_append_printf(out, "\n%4d %5d %4d %s", ABS(e->jump_pos - i), gtk_text_iter_get_line(&it) + 1,
                               gtk_text_iter_get_line_offset(&it), shown);
        g_free(shown);
        g_free(line);
    }
    vim_e_message(e, FALSE, "%s", out->str);
    g_string_free(out, TRUE);
}

/* :set */

typedef enum { OPT_BOOL, OPT_INT, OPT_CLIPBOARD } OptKind;

typedef struct {
    const gchar* name;
    const gchar* abbr;
    OptKind kind;
    gsize offset;
} OptDef;

static const OptDef OPTIONS[] = {
    {"ignorecase", "ic", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, ignorecase)},
    {"smartcase", "scs", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, smartcase)},
    {"hlsearch", "hls", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, hlsearch)},
    {"incsearch", "is", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, incsearch)},
    {"wrapscan", "ws", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, wrapscan)},
    {"expandtab", "et", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, expandtab)},
    {"autoindent", "ai", OPT_BOOL, G_STRUCT_OFFSET(VimOptions, autoindent)},
    {"shiftwidth", "sw", OPT_INT, G_STRUCT_OFFSET(VimOptions, shiftwidth)},
    {"tabstop", "ts", OPT_INT, G_STRUCT_OFFSET(VimOptions, tabstop)},
    {"softtabstop", "sts", OPT_INT, G_STRUCT_OFFSET(VimOptions, softtabstop)},
    {"textwidth", "tw", OPT_INT, G_STRUCT_OFFSET(VimOptions, textwidth)},
    {"clipboard", "cb", OPT_CLIPBOARD, G_STRUCT_OFFSET(VimOptions, unnamedplus)},
};

static const OptDef* find_option(const gchar* name) {
    for (gsize i = 0; i < G_N_ELEMENTS(OPTIONS); i++) {
        if (strcmp(OPTIONS[i].name, name) == 0 || strcmp(OPTIONS[i].abbr, name) == 0) return &OPTIONS[i];
    }
    return NULL;
}

static gchar* option_value(VimEngine* e, const OptDef* o) {
    gpointer field = G_STRUCT_MEMBER_P(&e->opt, o->offset);
    switch (o->kind) {
        case OPT_BOOL: return g_strdup_printf("%s%s", *(gboolean*)field ? "" : "no", o->name);
        case OPT_INT: return g_strdup_printf("%s=%d", o->name, *(gint*)field);
        default: return g_strdup_printf("clipboard=%s", *(gboolean*)field ? "unnamedplus" : "");
    }
}

static void ex_set(VimEngine* e, const gchar* args) {
    if (args == NULL || args[0] == '\0' || strcmp(args, "all") == 0) {
        GString* out = g_string_new(NULL);
        for (gsize i = 0; i < G_N_ELEMENTS(OPTIONS); i++) {
            gchar* v = option_value(e, &OPTIONS[i]);
            g_string_append_printf(out, "%s%s", out->len > 0 ? "  " : "", v);
            g_free(v);
        }
        vim_e_message(e, FALSE, "%s", out->str);
        g_string_free(out, TRUE);
        return;
    }
    gchar** words = g_strsplit(args, " ", -1);
    GString* shown = g_string_new(NULL);
    for (gint i = 0; words[i] != NULL; i++) {
        gchar* w = words[i];
        if (w[0] == '\0') continue;
        gchar* name = g_strdup(w);
        gchar* value = NULL;
        gchar op = 0;
        gchar* eq = strpbrk(name, "=:");
        if (eq != NULL) {
            value = eq + 1;
            if (eq > name && (eq[-1] == '+' || eq[-1] == '-' || eq[-1] == '^')) {
                op = eq[-1];
                eq[-1] = '\0';
            } else {
                op = '=';
            }
            *eq = '\0';
        }
        gsize len = strlen(name);
        gboolean query = len > 0 && name[len - 1] == '?';
        gboolean toggle = len > 0 && name[len - 1] == '!';
        if (query || toggle) name[len - 1] = '\0';
        gboolean neg = FALSE, inv = FALSE;
        const gchar* base = name;
        const OptDef* o = find_option(base);
        if (o == NULL && g_str_has_prefix(name, "no")) {
            o = find_option(name + 2);
            if (o != NULL && o->kind == OPT_BOOL) neg = TRUE;
            else o = NULL;
        }
        if (o == NULL && g_str_has_prefix(name, "inv")) {
            o = find_option(name + 3);
            if (o != NULL && o->kind == OPT_BOOL) inv = TRUE;
            else o = NULL;
        }
        if (o == NULL) {
            /* Options of the view (number, wrap, mode...) belong to the host */
            gchar* err = NULL;
            gboolean handled = e->host.ex_command != NULL && e->host.ex_command(e->hd, "set", FALSE, w, &err);
            if (!handled) {
                if (err != NULL) vim_e_message(e, TRUE, "%s", err);
                else vim_e_message(e, TRUE, "E518: Unknown option: %s", w);
                g_free(err);
                g_free(name);
                break;
            }
            g_free(err);
            g_free(name);
            continue;
        }
        gpointer field = G_STRUCT_MEMBER_P(&e->opt, o->offset);
        if (query || (o->kind != OPT_BOOL && value == NULL)) {
            gchar* v = option_value(e, o);
            g_string_append_printf(shown, "%s%s", shown->len > 0 ? "  " : "", v);
            g_free(v);
        } else if (o->kind == OPT_BOOL) {
            gboolean* b = field;
            *b = toggle || inv ? !*b : !neg;
        } else if (o->kind == OPT_INT) {
            gint* n = field;
            gint v = atoi(value);
            if (op == '+') *n += v;
            else if (op == '-') *n -= v;
            else if (op == '^') *n *= v;
            else *n = v;
            if (*n < 0 && o->offset != G_STRUCT_OFFSET(VimOptions, softtabstop)) *n = 0;
            if (*n < -1) *n = -1;
            if ((o->offset == G_STRUCT_OFFSET(VimOptions, tabstop)) && *n <= 0) *n = 8;
        } else {
            gboolean* b = field;
            *b = value != NULL && strstr(value, "unnamed") != NULL;
        }
        if (!query && e->host.ex_command != NULL) e->host.ex_command(e->hd, "set", FALSE, w, NULL);
        g_free(name);
    }
    if (shown->len > 0) vim_e_message(e, FALSE, "  %s", shown->str);
    g_string_free(shown, TRUE);
    g_strfreev(words);
    e->hl_active = e->hl_active && e->opt.hlsearch;
    vim_e_highlight_search(e);
}

static void ex_normal(VimEngine* e, const VimExCmd* c, gint l1, gint l2) {
    const gchar* keys = c->args;
    if (keys == NULL || keys[0] == '\0') return;
    if (c->naddr == 0) {
        vim_e_feed_internal(e, keys);
        vim_engine_reset(e);
        return;
    }
    GPtrArray* marks = g_ptr_array_new();
    for (gint l = l1; l <= l2; l++) {
        GtkTextIter it;
        iter_line(e, &it, l);
        g_ptr_array_add(marks, gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE));
    }
    for (guint i = 0; i < marks->len; i++) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(e->buf, &it, g_ptr_array_index(marks, i));
        gtk_text_iter_set_line_offset(&it, 0);
        gtk_text_buffer_place_cursor(e->buf, &it);
        vim_e_feed_internal(e, keys);
        vim_engine_reset(e);
    }
    for (guint i = 0; i < marks->len; i++) gtk_text_buffer_delete_mark(e->buf, g_ptr_array_index(marks, i));
    g_ptr_array_free(marks, TRUE);
}

static gboolean host_command(VimEngine* e, const VimExCmd* c, gchar** err) {
    if (e->host.ex_command == NULL) return FALSE;
    return e->host.ex_command(e->hd, c->name, c->bang, c->args, err);
}

static gchar* expand_file_wildcards(VimEngine* e, const gchar* cmd, gchar** err) {
    const gchar* fn = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
    GString* s = g_string_new(NULL);
    for (const gchar* p = cmd; *p; p++) {
        if (*p == '\\' && p[1] == '%') {
            g_string_append_c(s, '%');
            p++;
        } else if (*p == '%') {
            if (fn == NULL || fn[0] == '\0') {
                if (err != NULL) *err = g_strdup("E499: Empty file name for '%'");
                g_string_free(s, TRUE);
                return NULL;
            }
            g_string_append(s, fn);
        } else {
            g_string_append_c(s, *p);
        }
    }
    return g_string_free(s, FALSE);
}

static void ex_read(VimEngine* e, const VimExCmd* c, gint l2) {
    gboolean is_cmd = c->bang;
    const gchar* arg = c->args != NULL ? c->args : "";
    while (*arg == ' ' || *arg == '\t') arg++;
    if (!is_cmd && *arg == '!') {
        is_cmd = TRUE;
        arg++;
        while (*arg == ' ' || *arg == '\t') arg++;
    }

    gchar* content = NULL;
    gsize len = 0;

    if (is_cmd) {
        if (*arg == '\0') {
            vim_e_message(e, TRUE, "E471: Argument required");
            return;
        }
        gchar* err = NULL;
        gchar* cmd = expand_file_wildcards(e, arg, &err);
        if (cmd == NULL) {
            vim_e_message(e, TRUE, "%s", err);
            g_free(err);
            return;
        }

        const gchar* fn = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
        gchar* dir = NULL;
        if (fn != NULL && g_path_is_absolute(fn)) {
            dir = g_path_get_dirname(fn);
        }

        const gchar* argv[] = {"/bin/sh", "-c", cmd, NULL};
        gchar* out = NULL;
        gchar* err_out = NULL;
        gint status = 0;
        GError* gerr = NULL;
        gboolean ok = g_spawn_sync(dir, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH,
                                   NULL, NULL, &out, &err_out, &status, &gerr);
        g_free(dir);
        g_free(cmd);

        if (!ok) {
            vim_e_message(e, TRUE, "Cannot execute shell: %s", gerr != NULL ? gerr->message : "unknown error");
            g_clear_error(&gerr);
            g_free(out);
            g_free(err_out);
            return;
        }

        GError* wait_err = NULL;
        if (!g_spawn_check_wait_status(status, &wait_err)) {
            if (err_out != NULL && *err_out != '\0') {
                gchar* trimmed = g_strstrip(g_strdup(err_out));
                if (*trimmed != '\0') {
                    gchar* nl = strchr(trimmed, '\n');
                    if (nl != NULL) *nl = '\0';
                    vim_e_message(e, TRUE, "%s", trimmed);
                }
                g_free(trimmed);
            } else if (wait_err != NULL) {
                if (wait_err->domain == G_SPAWN_EXIT_ERROR) {
                    vim_e_message(e, TRUE, "shell returned %d", wait_err->code);
                } else {
                    vim_e_message(e, TRUE, "%s", wait_err->message);
                }
            }
            g_clear_error(&wait_err);
        }

        g_free(err_out);
        content = out;
        if (content != NULL) len = strlen(content);
    } else {
        const gchar* path = arg;
        gchar* full_path = NULL;
        if (*path == '\0') {
            path = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
            if (path == NULL || *path == '\0') {
                vim_e_message(e, TRUE, "E32: No file name");
                return;
            }
        }
        if (!g_path_is_absolute(path)) {
            const gchar* fn = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
            if (fn != NULL && g_path_is_absolute(fn)) {
                gchar* dir = g_path_get_dirname(fn);
                full_path = g_build_filename(dir, path, NULL);
                g_free(dir);
                path = full_path;
            }
        }
        GError* gerr = NULL;
        if (!g_file_get_contents(path, &content, &len, &gerr)) {
            vim_e_message(e, TRUE, "E484: Can't open file %s", path);
            g_clear_error(&gerr);
            g_free(full_path);
            return;
        }
        g_free(full_path);
    }

    if (content == NULL || *content == '\0') {
        g_free(content);
        return;
    }

    if (!g_utf8_validate(content, (gssize)len, NULL)) {
        gchar* valid = g_utf8_make_valid(content, (gssize)len);
        g_free(content);
        content = valid;
    }

    gchar* text = g_str_has_suffix(content, "\n") ? content : g_strconcat(content, "\n", NULL);
    if (text != content) g_free(content);

    gint after = l2;
    vim_e_put_lines(e, after, text);
    vim_e_set_cursor_line(e, MAX(0, after + 1), TRUE);
    g_free(text);
}

static void ex_print(VimEngine* e, gint l1, gint l2) {
    GString* out = g_string_new(NULL);
    for (gint l = l1; l <= l2 && l <= l1 + 20; l++) {
        gchar* t = vim_e_line_text(e, l);
        if (out->len > 0) g_string_append_c(out, '\n');
        g_string_append(out, t);
        g_free(t);
    }
    vim_e_message(e, FALSE, "%s", out->str);
    g_string_free(out, TRUE);
    vim_e_set_cursor_line(e, l2, TRUE);
}

void vim_engine_ex(VimEngine* e, const gchar* line) {
    VimExCmd c;
    gchar* err = NULL;
    if (line == NULL) return;
    const gchar* trimmed = line;
    while (*trimmed == ' ' || *trimmed == ':') trimmed++;
    if (*trimmed == '\0') return;
    if (e->global_depth == 0) {
        g_free(e->last_ex);
        e->last_ex = g_strdup(trimmed);
    }
    if (!vim_ex_parse(line, &c, &err)) {
        vim_e_message(e, TRUE, "%s", err);
        g_free(err);
        return;
    }
    const gchar* name = c.name;
    gint l1 = 0, l2 = 0;
    gboolean whole_default = strcmp(name, "global") == 0 || strcmp(name, "vglobal") == 0 ||
                             strcmp(name, "sort") == 0 || strcmp(name, "retab") == 0;
    if (c.naddr == 0 && whole_default) {
        l1 = 0;
        l2 = vim_e_last_line(e);
    } else if (!resolve_range(e, &c, &l1, &l2, &err)) {
        vim_e_message(e, TRUE, "%s", err);
        g_free(err);
        vim_ex_cmd_clear(&c);
        return;
    }
    gint last = vim_e_last_line(e);
    gboolean allow_zero = strcmp(name, "put") == 0 || strcmp(name, "read") == 0 || name[0] == '\0';
    if (l2 > last || (l1 < 0 && !allow_zero) || l1 < -1) {
        if (name[0] == '\0') {
            /* :N past the end goes to the last line */
            l2 = CLAMP(l2, 0, last);
        } else {
            vim_e_message(e, TRUE, "E16: Invalid range");
            vim_ex_cmd_clear(&c);
            return;
        }
    }

    gtk_text_buffer_begin_user_action(e->buf);
    if (name[0] == '\0') {
        vim_e_push_jump(e);
        vim_e_set_cursor_line(e, MAX(l2, 0), TRUE);
    } else if (strcmp(name, "substitute") == 0 || strcmp(name, "&") == 0 || strcmp(name, "&&") == 0 ||
               strcmp(name, "~") == 0) {
        ex_substitute(e, &c, l1, l2);
    } else if (strcmp(name, "global") == 0 || strcmp(name, "vglobal") == 0) {
        ex_global(e, &c, l1, l2, c.bang || name[0] == 'v');
    } else if (strcmp(name, "delete") == 0 || strcmp(name, "yank") == 0) {
        gunichar reg;
        gint count;
        reg_and_count(c.args, &reg, &count);
        if (count > 0) {
            l1 = l2;
            l2 = MIN(l1 + count - 1, last);
        }
        if (name[0] == 'd') {
            vim_e_delete_lines(e, l1, l2, reg, TRUE);
            if (l2 - l1 + 1 > 2) vim_e_message(e, FALSE, "%d fewer lines", l2 - l1 + 1);
        } else {
            gchar* text = lines_with_newline(e, l1, l2);
            vim_e_reg_set(e, reg, text, REG_LINE, REG_YANK);
            g_free(text);
            if (l2 - l1 + 1 > 2) vim_e_message(e, FALSE, "%d lines yanked", l2 - l1 + 1);
        }
    } else if (strcmp(name, "put") == 0) {
        gunichar reg;
        gint count;
        reg_and_count(c.args, &reg, &count);
        VimRegister r;
        if (!vim_e_reg_get(e, reg, &r) || r.text == NULL) {
            vim_e_message(e, TRUE, "E353: Nothing in register");
        } else {
            gchar* text = g_str_has_suffix(r.text, "\n") ? g_strdup(r.text) : g_strconcat(r.text, "\n", NULL);
            gint after = c.bang ? l2 - 1 : l2;
            vim_e_put_lines(e, after, text);
            gint nl = 0;
            for (const gchar* p = text; *p; p++) if (*p == '\n') nl++;
            vim_e_set_cursor_line(e, after + nl, TRUE);
            g_free(text);
        }
    } else if (strcmp(name, "read") == 0) {
        ex_read(e, &c, l2);
    } else if (strcmp(name, "move") == 0 || strcmp(name, "copy") == 0) {
        gint t;
        if (!target_line(e, c.args, &t, &err)) {
            vim_e_message(e, TRUE, "%s", err);
            g_free(err);
        } else if (t > last) {
            vim_e_message(e, TRUE, "E16: Invalid range");
        } else if (name[0] == 'm' && t >= l1 && t < l2) {
            vim_e_message(e, TRUE, "E134: Cannot move a range of lines into itself");
        } else {
            gchar* text = lines_with_newline(e, l1, l2);
            gint n = l2 - l1 + 1;
            if (name[0] == 'm') {
                if (t != l2 && t != l1 - 1) {
                    vim_e_delete_lines(e, l1, l2, '_', FALSE);
                    if (t > l2) t -= n;
                    vim_e_put_lines(e, t, text);
                }
            } else {
                vim_e_put_lines(e, t, text);
            }
            vim_e_set_cursor_line(e, t + n, TRUE);
            g_free(text);
        }
    } else if (strcmp(name, "join") == 0) {
        gint count = parse_count(c.args);
        if (count > 0) {
            l1 = l2;
            l2 = l1 + count - 1;
        } else if (l1 == l2) {
            l2 = l1 + 1;
        }
        if (l2 > last) l2 = last;
        if (l2 > l1) vim_e_join_lines(e, l1, l2, !c.bang);
    } else if (strcmp(name, ">") == 0 || strcmp(name, "<") == 0) {
        gint amount = 1;
        const gchar* p = c.args;
        while (*p == name[0] || *p == ' ') {
            if (*p == name[0]) amount++;
            p++;
        }
        gint count = parse_count(p);
        if (count > 0) {
            l1 = l2;
            l2 = MIN(l1 + count - 1, last);
        }
        vim_e_shift_lines(e, l1, l2, name[0] == '>' ? amount : -amount);
        vim_e_set_cursor_line(e, l2, TRUE);
    } else if (strcmp(name, "normal") == 0) {
        ex_normal(e, &c, l1, l2);
    } else if (strcmp(name, "sort") == 0) {
        ex_sort(e, l1, l2, c.bang, c.args);
    } else if (strcmp(name, "nohlsearch") == 0) {
        e->hl_active = FALSE;
        vim_e_highlight_search(e);
    } else if (strcmp(name, "set") == 0) {
        ex_set(e, c.args);
    } else if (strcmp(name, "registers") == 0) {
        ex_registers(e, c.args);
    } else if (strcmp(name, "marks") == 0) {
        ex_marks(e);
    } else if (strcmp(name, "jumps") == 0) {
        ex_jumps(e);
    } else if (strcmp(name, "mark") == 0) {
        if (c.args[0] == '\0') {
            vim_e_message(e, TRUE, "E471: Argument required");
        } else {
            GtkTextIter it;
            iter_line(e, &it, l2);
            vim_e_set_mark(e, (gunichar)(guchar)c.args[0], &it);
        }
    } else if (strcmp(name, "undo") == 0 || strcmp(name, "redo") == 0) {
        gtk_text_buffer_end_user_action(e->buf);
        feed_one(e, name[0] == 'u' ? 'u' : VIM_KEY_CTRL('r'));
        gtk_text_buffer_begin_user_action(e->buf);
    } else if (strcmp(name, "center") == 0 || strcmp(name, "right") == 0 || strcmp(name, "left") == 0) {
        gint w = parse_count(c.args);
        if (name[0] != 'l' && w <= 0) w = e->opt.textwidth > 0 ? e->opt.textwidth : 80;
        vim_e_align_lines(e, l1, l2, name[0], w);
        vim_e_set_cursor_line(e, l2, TRUE);
    } else if (strcmp(name, "retab") == 0) {
        ex_retab(e, l1, l2, c.args);
    } else if (strcmp(name, "print") == 0) {
        ex_print(e, l1, l2);
    } else if (strcmp(name, "=") == 0) {
        vim_e_message(e, FALSE, "%d", c.naddr == 0 ? last + 1 : l2 + 1);
    } else if (strcmp(name, "file") == 0) {
        feed_one(e, VIM_KEY_CTRL('g'));
    } else {
        err = NULL;
        if (!host_command(e, &c, &err)) {
            vim_e_message(e, TRUE, "%s", err != NULL ? err : "E492: Not an editor command");
        } else if (err != NULL) {
            vim_e_message(e, TRUE, "%s", err);
        }
        g_free(err);
    }
    gtk_text_buffer_end_user_action(e->buf);
    if (e->mode == VIM_MODE_NORMAL) clamp_cursor(e);
    vim_ex_cmd_clear(&c);
}
