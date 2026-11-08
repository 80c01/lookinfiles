/* Vim engine: modes, registers, marks, undo grouping, recording, lifecycle
 * and key feeding. Motions, Insert mode, operators, Normal and Visual
 * commands are in vim_engine_*.c; Ex commands and search in vim_engine_ex.c */
#include "vim_engine_private.h"
#include "theme/text_tags.h"
#include "vim_keys.h"
#include "vim_motion.h"
#include <stdarg.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */







void vim_e_state_changed(VimEngine* e) {
    if (e->host.state_changed != NULL) e->host.state_changed(e->hd);
}

void vim_e_message(VimEngine* e, gboolean error, const gchar* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    gchar* text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    if (error) e->beeped = TRUE;
    if (e->host.message != NULL) e->host.message(e->hd, text, error);
    g_free(text);
}

void vim_e_beep(VimEngine* e) {
    e->beeped = TRUE;
}

void vim_e_cursor(VimEngine* e, GtkTextIter* it) {
    gtk_text_buffer_get_iter_at_mark(e->buf, it, gtk_text_buffer_get_insert(e->buf));
}

gint vim_e_cursor_line(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    return gtk_text_iter_get_line(&it);
}

gint vim_e_last_line(VimEngine* e) {
    return gtk_text_buffer_get_line_count(e->buf) - 1;
}

/* Last line a Normal-mode cursor can reach: Vim does not count the empty
 * line GtkTextBuffer shows after the final newline. */
gint vim_e_last_text_line(VimEngine* e) {
    gint last = vim_e_last_line(e);
    if (last > 0) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_line(e->buf, &it, last);
        if (gtk_text_iter_starts_line(&it) && gtk_text_iter_is_end(&it)) last--;
    }
    return last;
}

void vim_e_set_cursor(VimEngine* e, const GtkTextIter* it) {
    gtk_text_buffer_place_cursor(e->buf, it);
    if (vim_e_is_visual(e->mode)) vim_e_update_visual(e);
}

void vim_e_remember_column(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    e->curswant = vim_iter_vcol(&it, e->opt.tabstop);
    e->want_eol = FALSE;
}

void vim_e_set_cursor_line(VimEngine* e, gint line, gboolean first_nonblank) {
    if (line < 0) line = 0;
    gint max_line = vim_e_is_insert(e->mode) ? vim_e_last_line(e) : vim_e_last_text_line(e);
    if (line > max_line) line = max_line;
    GtkTextIter it;
    vim_e_iter_line(e, &it, line);
    if (first_nonblank) vim_iter_first_nonblank(&it);
    if (!vim_e_is_insert(e->mode)) vim_iter_clamp_normal(&it);
    vim_e_set_cursor(e, &it);
    vim_e_remember_column(e);
}

void vim_e_clamp_cursor(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    GtkTextIter c = it;
    vim_iter_clamp_normal(&c);
    if (!gtk_text_iter_equal(&c, &it)) gtk_text_buffer_place_cursor(e->buf, &c);
}

gchar* vim_e_line_text(VimEngine* e, gint line) {
    GtkTextIter s, t;
    vim_e_iter_line(e, &s, line);
    t = s;
    vim_iter_line_end(&t);
    return gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
}

gchar* vim_e_slice(VimEngine* e, gint a, gint b) {
    GtkTextIter s, t;
    vim_e_iter_at(e, &s, a);
    vim_e_iter_at(e, &t, b);
    return gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
}

void vim_e_insert_text(VimEngine* e, GtkTextIter* at, const gchar* text) {
    if (text == NULL || text[0] == '\0') return;
    e->self_edit++;
    gtk_text_buffer_insert(e->buf, at, text, -1);
    e->self_edit--;
}

void vim_e_delete(VimEngine* e, GtkTextIter* s, GtkTextIter* t) {
    if (gtk_text_iter_equal(s, t)) return;
    e->self_edit++;
    gtk_text_buffer_delete(e->buf, s, t);
    e->self_edit--;
}

void vim_e_insert_at_cursor(VimEngine* e, const gchar* text) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    vim_e_insert_text(e, &it, text);
    gtk_text_buffer_place_cursor(e->buf, &it);
}

static gchar* make_indent(VimEngine* e, gint width) {
    GString* s = g_string_new(NULL);
    if (width < 0) width = 0;
    if (!e->opt.expandtab && e->opt.tabstop > 0) {
        for (gint i = 0; i < width / e->opt.tabstop; i++) g_string_append_c(s, '\t');
        width %= e->opt.tabstop;
    }
    for (gint i = 0; i < width; i++) g_string_append_c(s, ' ');
    return g_string_free(s, FALSE);
}

/* Leading white space of a line as text */
gchar* vim_e_line_indent_text(VimEngine* e, gint line) {
    GtkTextIter s, t;
    vim_e_iter_line(e, &s, line);
    t = s;
    vim_iter_first_nonblank(&t);
    return gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
}

void vim_e_set_line_indent(VimEngine* e, gint line, gint width) {
    GtkTextIter s, t;
    vim_e_iter_line(e, &s, line);
    t = s;
    vim_iter_first_nonblank(&t);
    gchar* old = gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
    gchar* ind = make_indent(e, width);
    if (strcmp(old, ind) != 0) {
        vim_e_delete(e, &s, &t);
        vim_e_insert_text(e, &s, ind);
    }
    g_free(old);
    g_free(ind);
}

/* ------------------------------------------------------------------------- */
/* Undo grouping, marks, jumps                                               */
/* ------------------------------------------------------------------------- */

static void undo_step_free(gpointer data) {
    VimUndoStep* st = data;
    for (guint i = 0; i < st->ops->len; i++) g_free(g_array_index(st->ops, VimUndoOp, i).text);
    g_array_free(st->ops, TRUE);
    g_free(st);
}

/* Close the step being recorded: it becomes the newest undo step */
void vim_e_undo_close_step(VimEngine* e) {
    VimUndoStep* st = e->undo_cur;
    e->undo_cur = NULL;
    if (st == NULL) return;
    if (st->ops->len == 0) {
        undo_step_free(st);
        return;
    }
    if (e->undo_pos < (gint)e->undo_steps->len) {
        g_ptr_array_set_size(e->undo_steps, e->undo_pos); /* drops the redo steps */
        if (e->saved_pos > e->undo_pos) e->saved_pos = -1;
    }
    g_ptr_array_add(e->undo_steps, st);
    if (e->undo_steps->len > 1000) {
        g_ptr_array_remove_index(e->undo_steps, 0);
        if (e->saved_pos >= 0) e->saved_pos--;
    }
    e->undo_pos = (gint)e->undo_steps->len;
}

static void undo_record(VimEngine* e, gboolean insert, gint offset, const gchar* text, gint chars) {
    if (e->undoing || chars <= 0) return;
    if (e->undo_cur == NULL) {
        e->undo_cur = g_new0(VimUndoStep, 1);
        e->undo_cur->ops = g_array_new(FALSE, FALSE, sizeof(VimUndoOp));
    }
    VimUndoOp op = {insert, offset, chars, g_strdup(text)};
    g_array_append_val(e->undo_cur->ops, op);
    /* A change made outside a Vim command and outside a user action is its own step */
    if (!e->change_open && !e->ext_action) vim_e_undo_close_step(e);
}

void vim_e_begin_change(VimEngine* e) {
    if (e->change_open) return;
    e->change_open = TRUE;
    gtk_text_buffer_begin_user_action(e->buf);
}

/* Someone else's user action (an Org command, a mouse paste): one undo step */
static void on_begin_user_action(GtkTextBuffer* buf, gpointer data) {
    (void)buf;
    VimEngine* e = data;
    if (!e->change_open && !e->undoing) e->ext_action = TRUE;
}

static void on_end_user_action(GtkTextBuffer* buf, gpointer data) {
    (void)buf;
    VimEngine* e = data;
    if (!e->ext_action) return;
    e->ext_action = FALSE;
    if (!e->change_open) vim_e_undo_close_step(e);
}

void vim_e_end_change(VimEngine* e) {
    if (!e->change_open) return;
    e->change_open = FALSE;
    vim_e_undo_close_step(e);
    gtk_text_buffer_end_user_action(e->buf);
}

static gunichar mark_slot(gunichar name) {
    if (name >= 'A' && name <= 'Z') return name; /* file marks: kept per buffer */
    if (name == '`') return '\'';
    return name;
}

void vim_e_set_mark(VimEngine* e, gunichar name, const GtkTextIter* it) {
    name = mark_slot(name);
    if (name >= 128) return;
    if (e->marks[name] == NULL) {
        e->marks[name] = gtk_text_buffer_create_mark(e->buf, NULL, it, TRUE);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->marks[name], it);
    }
}

gboolean vim_e_get_mark(VimEngine* e, gunichar name, GtkTextIter* it) {
    name = mark_slot(name);
    if (name >= 128 || e->marks[name] == NULL) return FALSE;
    gtk_text_buffer_get_iter_at_mark(e->buf, it, e->marks[name]);
    return TRUE;
}

void vim_e_push_jump(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    vim_e_set_mark(e, '\'', &it);
    /* Drop the jumps after the current position, and an equal last entry */
    while ((gint)e->jumps->len > e->jump_pos) {
        GtkTextMark* m = g_ptr_array_index(e->jumps, e->jumps->len - 1);
        gtk_text_buffer_delete_mark(e->buf, m);
        g_ptr_array_remove_index(e->jumps, e->jumps->len - 1);
    }
    for (guint i = 0; i < e->jumps->len; i++) {
        GtkTextIter j;
        gtk_text_buffer_get_iter_at_mark(e->buf, &j, g_ptr_array_index(e->jumps, i));
        if (gtk_text_iter_get_line(&j) == gtk_text_iter_get_line(&it)) {
            gtk_text_buffer_delete_mark(e->buf, g_ptr_array_index(e->jumps, i));
            g_ptr_array_remove_index(e->jumps, i);
            break;
        }
    }
    if (e->jumps->len >= 100) {
        gtk_text_buffer_delete_mark(e->buf, g_ptr_array_index(e->jumps, 0));
        g_ptr_array_remove_index(e->jumps, 0);
    }
    g_ptr_array_add(e->jumps, gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE));
    e->jump_pos = (gint)e->jumps->len;
}


void vim_e_jump_older(VimEngine* e, gint count, gboolean older) {
    if (older && e->jump_pos == (gint)e->jumps->len) {
        /* Remember where we are so Tab can come back */
        GtkTextIter it;
        vim_e_cursor(e, &it);
        vim_e_push_jump(e);
        e->jump_pos = (gint)e->jumps->len - 1;
    }
    gint target = e->jump_pos + (older ? -count : count);
    if (target < 0 || target >= (gint)e->jumps->len) {
        /* Past the ends: the host's history across files */
        if (e->host.jump_history == NULL || !e->host.jump_history(e->hd, older)) vim_e_beep(e);
        return;
    }
    e->jump_pos = target;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(e->buf, &it, g_ptr_array_index(e->jumps, target));
    vim_iter_clamp_normal(&it);
    vim_e_set_cursor(e, &it);
    vim_e_remember_column(e);
}

/* ------------------------------------------------------------------------- */
/* Registers                                                                 */
/* ------------------------------------------------------------------------- */

void vim_e_reg_store(VimEngine* e, gunichar name, const gchar* text, RegType type) {
    if (name >= 128) return;
    gchar* copy = g_strdup(text); /* text may be this register's own */
    g_free(e->regs[name].text);
    e->regs[name].text = copy;
    e->regs[name].type = type;
}

void vim_e_reg_set(VimEngine* e, gunichar name, const gchar* text, RegType type, RegKind kind) {
    if (name == '_') return;
    if (text == NULL) text = "";
    gboolean explicit_reg = name != 0 && name != '"';
    if (name >= 'A' && name <= 'Z') {
        gunichar lower = name - 'A' + 'a';  /* ASCII: name is A..Z */
        VimRegister* r = &e->regs[lower];
        if (r->text != NULL) {
            gchar* joined;
            if (type == REG_LINE && r->type != REG_LINE) {
                joined = g_strconcat(r->text, "\n", text, NULL);
            } else if (r->type == REG_LINE && type != REG_LINE) {
                joined = g_strconcat(r->text, text, "\n", NULL);
                type = REG_LINE;
            } else {
                joined = g_strconcat(r->text, text, NULL);
            }
            if (r->type == REG_LINE) type = REG_LINE;
            vim_e_reg_store(e, lower, joined, type);
            g_free(joined);
        } else {
            vim_e_reg_store(e, lower, text, type);
        }
        name = lower;
        text = e->regs[lower].text;
    } else if (explicit_reg) {
        vim_e_reg_store(e, name, text, type);
    }
    if (explicit_reg && name < 128 && e->regs[name].text != NULL) type = e->regs[name].type;
    gchar* copy = g_strdup(text);
    vim_e_reg_store(e, '"', copy, type);
    g_free(copy);
    text = e->regs['"'].text;
    if (!explicit_reg) {
        if (kind == REG_YANK) {
            vim_e_reg_store(e, '0', text, type);
        } else if (type == REG_LINE || strchr(text, '\n') != NULL) {
            for (gunichar r = '9'; r > '1'; r--) {
                g_free(e->regs[r].text);
                e->regs[r] = e->regs[r - 1];
                e->regs[r - 1].text = NULL;
            }
            vim_e_reg_store(e, '1', text, type);
        } else {
            vim_e_reg_store(e, '-', text, type);
        }
    }
    if (name == '+' || name == '*' || (!explicit_reg && e->opt.unnamedplus)) {
        vim_e_reg_store(e, '+', text, type);
        vim_e_reg_store(e, '*', text, type);
        g_free(e->clip_own);
        e->clip_own = g_strdup(text);
        if (e->host.clipboard_set != NULL) e->host.clipboard_set(e->hd, text);
    }
}

gboolean vim_e_reg_get(VimEngine* e, gunichar name, VimRegister* out) {
    out->text = NULL;
    out->type = REG_CHAR;
    if (name == 0) name = '"';
    if (name >= 'A' && name <= 'Z') name = g_unichar_tolower(name);
    if (name == '%') {
        const gchar* f = e->host.file_name != NULL ? e->host.file_name(e->hd) : NULL;
        out->text = (gchar*)(f != NULL ? f : "");
        return f != NULL;
    }
    if (name == '/') {
        out->text = e->last_pat;
        return e->last_pat != NULL;
    }
    if (name == ':') {
        out->text = e->last_ex;
        return e->last_ex != NULL;
    }
    if (name >= 128) return FALSE;
    *out = e->regs[name];
    return out->text != NULL;
}

void vim_engine_clipboard_changed(VimEngine* e, const gchar* text) {
    if (text == NULL) return;
    if (e->clip_own != NULL && strcmp(e->clip_own, text) == 0) return;
    RegType type = g_str_has_suffix(text, "\n") ? REG_LINE : REG_CHAR;
    vim_e_reg_store(e, '+', text, type);
    vim_e_reg_store(e, '*', text, type);
    if (e->opt.unnamedplus) vim_e_reg_store(e, '"', text, type);
    g_free(e->clip_own);
    e->clip_own = g_strdup(text);
}

const gchar* vim_engine_register(VimEngine* e, gunichar name, gboolean* linewise) {
    VimRegister r;
    gboolean ok = vim_e_reg_get(e, name, &r);
    if (linewise != NULL) *linewise = ok && r.type == REG_LINE;
    return ok ? r.text : NULL;
}

/* ------------------------------------------------------------------------- */
/* Recording what the text view types in Insert mode                         */
/* ------------------------------------------------------------------------- */

void vim_e_record_insert_keys(VimEngine* e, const gchar* keys) {
    if (!e->ins_repeating) g_string_append(e->ins_keys, keys);
    if (e->rec_reg != 0 && e->replaying == 0) g_string_append(e->rec, keys);
}

void vim_e_record_insert_key(VimEngine* e, gunichar k) {
    gchar buf[8];
    buf[g_unichar_to_utf8(k, buf)] = '\0';
    vim_e_record_insert_keys(e, buf);
}

void vim_e_on_insert_text(GtkTextBuffer* buf, GtkTextIter* location, gchar* text, gint len,
                           gpointer data) {
    (void)buf;
    VimEngine* e = data;
    e->buffer_dirty = TRUE;
    gchar* t = g_strndup(text, len);
    undo_record(e, TRUE, vim_e_off(location), t, (gint)g_utf8_strlen(t, -1));
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    if (e->self_edit > 0 || e->replaying > 0 || !vim_e_is_insert(e->mode) || !gtk_text_iter_equal(&cur, location)) {
        g_free(t);
        return;
    }
    vim_e_record_insert_keys(e, t);
    g_free(t);
}

static void on_delete_range(GtkTextBuffer* buf, GtkTextIter* start, GtkTextIter* end, gpointer data) {
    (void)buf;
    VimEngine* e = data;
    e->buffer_dirty = TRUE;
    if (!e->undoing) {
        gchar* t = gtk_text_iter_get_slice(start, end);
        undo_record(e, FALSE, vim_e_off(start), t, vim_e_off(end) - vim_e_off(start));
        g_free(t);
    }
    if (e->self_edit > 0 || e->replaying > 0 || !vim_e_is_insert(e->mode)) return;
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    gint n = vim_e_off(end) - vim_e_off(start);
    gunichar k = 0;
    if (gtk_text_iter_equal(&cur, end)) k = VIM_KEY_BS;
    else if (gtk_text_iter_equal(&cur, start)) k = VIM_KEY_DEL;
    if (k == 0) return;
    for (gint i = 0; i < n; i++) vim_e_record_insert_key(e, k);
}

static void on_modified_changed(GtkTextBuffer* buf, gpointer data) {
    VimEngine* e = data;
    if (!gtk_text_buffer_get_modified(buf) && !e->undoing) {
        vim_e_undo_close_step(e);
        e->saved_pos = e->undo_pos;
    }
}

static void on_mark_set(GtkTextBuffer* buf, GtkTextIter* location, GtkTextMark* mark, gpointer data) {
    (void)location;
    VimEngine* e = data;
    if (mark != gtk_text_buffer_get_insert(buf) || e->updating_visual) return;
    if (vim_e_is_visual(e->mode)) vim_e_update_visual(e);
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

static GtkTextTag* ensure_tag(GtkTextBuffer* buf, const gchar* name, const gchar* bg) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    /* bg: a palette role ("@editor_selection"), followed on theme changes */
    GtkTextTag* tag = text_tag_ensure(buf, name, "background", bg, NULL);
    gtk_text_tag_set_priority(tag, gtk_text_tag_table_get_size(table) - 1);
    return tag;
}

VimEngine* vim_engine_new(GtkTextBuffer* buffer, const VimHost* host, gpointer host_data) {
    VimEngine* e = g_new0(VimEngine, 1);
    e->buf = g_object_ref(buffer);
    if (host != NULL) e->host = *host;
    e->hd = host_data;
    e->opt.ignorecase = TRUE;
    e->opt.smartcase = TRUE;
    e->opt.hlsearch = TRUE;
    e->opt.incsearch = TRUE;
    e->opt.wrapscan = TRUE;
    e->opt.expandtab = TRUE;
    e->opt.autoindent = TRUE;
    e->opt.unnamedplus = TRUE;
    e->opt.shiftwidth = 4;
    e->opt.tabstop = 8;
    e->opt.softtabstop = -1;
    e->opt.textwidth = 79;
    e->pending = g_array_new(FALSE, FALSE, sizeof(gunichar));
    e->cl_text = g_string_new(NULL);
    e->ins_keys = g_string_new(NULL);
    e->replace_stack = g_string_new(NULL);
    e->dot = g_string_new(NULL);
    e->rec = g_string_new(NULL);
    e->jumps = g_ptr_array_new();
    e->hist_cmd = g_ptr_array_new_with_free_func(g_free);
    e->hist_search = g_ptr_array_new_with_free_func(g_free);
    e->last_fwd = TRUE;
    e->last_cursor_off = -1;
    e->tag_visual = ensure_tag(buffer, "vim-visual", "@editor_selection");
    e->tag_search = ensure_tag(buffer, "vim-search", "@editor_match");
    e->tag_incsearch = ensure_tag(buffer, "vim-incsearch", "@editor_match_current");
    e->sig_insert = g_signal_connect(buffer, "insert-text", G_CALLBACK(vim_e_on_insert_text), e);
    e->sig_delete = g_signal_connect(buffer, "delete-range", G_CALLBACK(on_delete_range), e);
    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_mark_set), e);
    g_signal_connect(buffer, "modified-changed", G_CALLBACK(on_modified_changed), e);
    g_signal_connect(buffer, "begin-user-action", G_CALLBACK(on_begin_user_action), e);
    g_signal_connect(buffer, "end-user-action", G_CALLBACK(on_end_user_action), e);
    e->undo_steps = g_ptr_array_new_with_free_func(undo_step_free);
    e->saved_pos = gtk_text_buffer_get_modified(buffer) ? -1 : 0;
    GtkTextIter it;
    vim_e_cursor(e, &it);
    vim_iter_clamp_normal(&it);
    gtk_text_buffer_place_cursor(buffer, &it);
    return e;
}

void vim_engine_set_host_data(VimEngine* e, gpointer host_data) {
    if (e != NULL) e->hd = host_data;
}

gpointer vim_engine_get_host_data(VimEngine* e) {
    return e != NULL ? e->hd : NULL;
}

void vim_engine_free(VimEngine* e) {
    if (e == NULL) return;
    vim_e_end_change(e);
    g_signal_handlers_disconnect_by_data(e->buf, e);
    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(e->buf, &s, &t);
    gtk_text_buffer_remove_tag(e->buf, e->tag_visual, &s, &t);
    gtk_text_buffer_remove_tag(e->buf, e->tag_search, &s, &t);
    gtk_text_buffer_remove_tag(e->buf, e->tag_incsearch, &s, &t);
    for (gint i = 0; i < 128; i++) {
        g_free(e->regs[i].text);
        if (e->marks[i] != NULL) gtk_text_buffer_delete_mark(e->buf, e->marks[i]);
    }
    for (guint i = 0; i < e->jumps->len; i++) {
        gtk_text_buffer_delete_mark(e->buf, g_ptr_array_index(e->jumps, i));
    }
    if (e->vis_anchor != NULL) gtk_text_buffer_delete_mark(e->buf, e->vis_anchor);
    if (e->ins_start != NULL) gtk_text_buffer_delete_mark(e->buf, e->ins_start);
    g_ptr_array_free(e->jumps, TRUE);
    if (e->undo_cur != NULL) undo_step_free(e->undo_cur);
    g_ptr_array_free(e->undo_steps, TRUE);
    g_array_free(e->pending, TRUE);
    g_string_free(e->cl_text, TRUE);
    g_string_free(e->ins_keys, TRUE);
    g_string_free(e->replace_stack, TRUE);
    g_string_free(e->dot, TRUE);
    if (e->dot_pending != NULL) g_string_free(e->dot_pending, TRUE);
    g_string_free(e->rec, TRUE);
    if (e->compl_words != NULL) g_ptr_array_free(e->compl_words, TRUE);
    g_free(e->compl_prefix);
    g_ptr_array_free(e->hist_cmd, TRUE);
    g_ptr_array_free(e->hist_search, TRUE);
    g_free(e->clip_own);
    g_free(e->cl_prefill);
    g_free(e->last_pat);
    g_free(e->sub_pat);
    g_free(e->sub_rep);
    g_free(e->sub_flags);
    g_free(e->last_ex);
    g_object_unref(e->buf);
    g_free(e);
}

VimOptions* vim_engine_options(VimEngine* e) {
    return &e->opt;
}

VimMode vim_engine_mode(VimEngine* e) {
    if (e->mode == VIM_MODE_NORMAL && e->pending->len > 0) {
        for (guint i = 0; i < e->pending->len; i++) {
            gunichar k = g_array_index(e->pending, gunichar, i);
            if (k == 'd' || k == 'c' || k == 'y' || k == '<' || k == '>' || k == '=') {
                return VIM_MODE_OPERATOR_PENDING;
            }
        }
    }
    return e->mode;
}

const gchar* vim_engine_mode_label(VimEngine* e) {
    switch (e->mode) {
        case VIM_MODE_INSERT: return e->ins_ctrl_o ? "-- (insert) --" : "-- INSERT --";
        case VIM_MODE_REPLACE: return "-- REPLACE --";
        case VIM_MODE_VISUAL: return "-- VISUAL --";
        case VIM_MODE_VISUAL_LINE: return "-- VISUAL LINE --";
        case VIM_MODE_VISUAL_BLOCK: return "-- VISUAL BLOCK --";
        case VIM_MODE_CMDLINE: return "COMMAND";
        default: return e->ins_ctrl_o ? "-- (insert) --" : "NORMAL";
    }
}

gchar* vim_engine_pending_keys(VimEngine* e) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < e->pending->len; i++) {
        g_string_append_unichar(s, g_array_index(e->pending, gunichar, i));
    }
    gchar* raw = g_string_free(s, FALSE);
    gchar* shown = vim_keys_to_display(raw);
    g_free(raw);
    return shown;
}

gunichar vim_engine_recording(VimEngine* e) {
    return e->rec_reg;
}

void vim_engine_get_cursor(VimEngine* e, GtkTextIter* iter) {
    vim_e_cursor(e, iter);
}

/* ------------------------------------------------------------------------- */
/* Feeding keys                                                              */
/* ------------------------------------------------------------------------- */

void vim_e_set_mode(VimEngine* e, VimMode mode) {
    if (e->mode == mode) return;
    e->mode = mode;
    vim_e_state_changed(e);
}

/* A selection made with the mouse starts Visual mode */
static void sync_mouse_selection(VimEngine* e) {
    GtkTextIter ins, bound;
    vim_e_cursor(e, &ins);
    if (vim_e_off(&ins) != e->last_cursor_off) {
        /* Moved by a click or by the application: j / k start from here */
        e->curswant = vim_iter_vcol(&ins, e->opt.tabstop);
        e->want_eol = FALSE;
    }
    gtk_text_buffer_get_iter_at_mark(e->buf, &bound, gtk_text_buffer_get_selection_bound(e->buf));
    if (gtk_text_iter_equal(&ins, &bound)) {
        if (e->mode == VIM_MODE_NORMAL) vim_e_clamp_cursor(e);
        return;
    }
    if (e->mode != VIM_MODE_NORMAL && !vim_e_is_visual(e->mode)) return;
    GtkTextIter cur = ins;
    if (gtk_text_iter_compare(&ins, &bound) > 0) gtk_text_iter_backward_char(&cur);
    e->updating_visual = TRUE;
    gtk_text_buffer_place_cursor(e->buf, &bound);
    e->updating_visual = FALSE;
    if (!vim_e_is_visual(e->mode)) {
        vim_e_enter_visual(e, VIM_MODE_VISUAL);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &bound);
    }
    gtk_text_buffer_place_cursor(e->buf, &cur);
    vim_e_update_visual(e);
}

/* Common end of a key: undo step, cursor, highlight. resume_insert: a
 * Normal command ran, so a pending Ctrl-O goes back to Insert mode. */
static void finish_key(VimEngine* e, gboolean resume_insert) {
    if (!vim_e_is_insert(e->mode) && e->mode != VIM_MODE_CMDLINE) vim_e_end_change(e);
    if (e->mode == VIM_MODE_NORMAL && !e->ins_ctrl_o) vim_e_clamp_cursor(e);
    if (resume_insert && e->ins_ctrl_o && e->mode == VIM_MODE_NORMAL) {
        e->ins_ctrl_o = FALSE;
        if (e->want_eol) {
            /* <C-o>$ goes after the last character */
            GtkTextIter it;
            vim_e_cursor(e, &it);
            vim_iter_line_end(&it);
            gtk_text_buffer_place_cursor(e->buf, &it);
        }
        vim_e_start_insert(e, 'i', 1, VIM_MODE_INSERT);
        if (e->dot_pending != NULL) g_string_free(e->dot_pending, TRUE);
        e->dot_pending = g_string_new("i");
    }
    if (e->buffer_dirty && e->hl_active && !vim_e_is_insert(e->mode)) {
        e->buffer_dirty = FALSE;
        vim_e_highlight_search(e);
    }
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    e->last_cursor_off = vim_e_off(&cur);
    vim_e_state_changed(e);
}

static void after_command(VimEngine* e) {
    finish_key(e, TRUE);
}

/* Ask the host for the definition of the word under the cursor; the jump
 * list remembers where it started. FALSE when the host does not look up. */
gboolean vim_e_host_goto_definition(VimEngine* e, gboolean declaration) {
    if (e->host.goto_definition == NULL) return FALSE;
    GtkTextIter ws, we;
    if (!vim_e_word_under_cursor(e, &ws, &we)) return FALSE;
    gchar* word = gtk_text_iter_get_slice(&ws, &we);
    /* The lookup is asynchronous: the cursor has not moved yet */
    gboolean handled = e->host.goto_definition(e->hd, word, declaration);
    if (handled) vim_e_push_jump(e);
    g_free(word);
    return handled;
}

static void exec_command(VimEngine* e, const VimCmd* cmd) {
    vim_e_begin_change(e);
    if (vim_e_is_visual(e->mode)) {
        vim_e_exec_visual(e, cmd);
    } else {
        switch (cmd->kind) {
            case VIM_CMD_MOTION: {
                if (cmd->key[0] == '/' || cmd->key[0] == '?') {
                    vim_e_open_cmdline_for(e, cmd->key[0], NULL);
                    break;
                }
                /* gd / gD in a code file: the host's definition lookup */
                if (cmd->key[0] == 'g' && (cmd->key[1] == 'd' || cmd->key[1] == 'D') &&
                    vim_e_host_goto_definition(e, cmd->key[1] == 'D')) {
                    break;
                }
                GtkTextIter cur;
                vim_e_cursor(e, &cur);
                Motion m;
                if (vim_e_do_motion(e, cmd, FALSE, &cur, &m)) vim_e_move_cursor(e, &m);
                else vim_e_beep(e);
                break;
            }
            case VIM_CMD_OPERATOR:
                if (cmd->key[0] == '/' || cmd->key[0] == '?') {
                    vim_e_open_cmdline_for(e, cmd->key[0], cmd);
                    break;
                }
                vim_e_exec_operator(e, cmd);
                if (cmd->op[0] != 'y' && !e->beeped) vim_e_start_recording_dot(e, cmd, NULL);
                break;
            case VIM_CMD_OPERATOR_LINES:
                vim_e_exec_operator_lines(e, cmd);
                if (cmd->op[0] != 'y') vim_e_start_recording_dot(e, cmd, NULL);
                break;
            case VIM_CMD_SIMPLE:
                vim_e_exec_simple(e, cmd);
                break;
            default:
                break;
        }
    }
    after_command(e);
}

/* After a / or ? typed for an operator: apply it up to the match */
void vim_e_run_motion_after_search(VimEngine* e, const GtkTextIter* target) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    if (!e->cl_has_op) {
        Motion m = {0};
        m.pos = *target;
        m.jump = TRUE;
        vim_e_move_cursor(e, &m);
        return;
    }
    e->cl_has_op = FALSE;
    VimCmd cmd = e->cl_op;
    Motion m = {0};
    m.pos = *target;
    m.numbered = TRUE;
    vim_e_push_jump(e);
    vim_e_begin_change(e);
    VimRange r;
    vim_e_motion_range(e, &cmd, &m, &cur, &r);
    vim_e_apply_operator(e, &cmd, &r, TRUE);
    if (cmd.op[0] != 'y') {
        GString* extra = g_string_new(NULL);
        g_string_append(extra, e->last_pat != NULL ? e->last_pat : "");
        g_string_append_unichar(extra, VIM_KEY_CR);
        vim_e_start_recording_dot(e, &cmd, extra->str);
        g_string_free(extra, TRUE);
    }
    after_command(e);
}

static gboolean feed_key(VimEngine* e, gunichar key) {
    if (e->mode == VIM_MODE_CMDLINE) {
        if (!e->cl_internal) return TRUE;
        switch (key) {
            case VIM_KEY_CR: case VIM_KEY_NL: {
                gchar* text = g_strdup(e->cl_text->str);
                vim_engine_cmdline_submit(e, text);
                g_free(text);
                break;
            }
            case VIM_KEY_ESC: case VIM_KEY_CTRL('c'):
                vim_engine_cmdline_cancel(e);
                break;
            case VIM_KEY_BS:
                if (e->cl_text->len == 0) {
                    vim_engine_cmdline_cancel(e);
                } else {
                    const gchar* last = g_utf8_find_prev_char(e->cl_text->str, e->cl_text->str + e->cl_text->len);
                    g_string_truncate(e->cl_text, last - e->cl_text->str);
                }
                break;
            default:
                if (!VIM_KEY_IS_SPECIAL(key)) g_string_append_unichar(e->cl_text, key);
                else if (key == VIM_KEY_TAB) g_string_append_c(e->cl_text, '\t');
                break;
        }
        return TRUE;
    }
    if (vim_e_is_insert(e->mode)) {
        gboolean handled = vim_e_insert_key(e, key);
        if (!vim_e_is_insert(e->mode)) finish_key(e, FALSE);
        return handled;
    }
    sync_mouse_selection(e);
    if (e->pending->len == 0 && (key == VIM_KEY_ESC || key == VIM_KEY_CTRL('c')) && !vim_e_is_visual(e->mode)) {
        return TRUE;
    }
    g_array_append_val(e->pending, key);
    VimCmd cmd;
    VimParseStatus st = vim_parse((const gunichar*)e->pending->data, e->pending->len, vim_e_is_visual(e->mode),
                                  e->rec_reg != 0, &cmd);
    if (st == VIM_PARSE_INCOMPLETE) {
        vim_e_state_changed(e);
        return TRUE;
    }
    guint npending = e->pending->len;
    g_array_set_size(e->pending, 0);
    if (st == VIM_PARSE_INVALID) {
        vim_e_state_changed(e);
        /* A lone key Vim does not use (Ctrl+S...) goes to the application */
        return !(npending == 1 && VIM_KEY_IS_CTRL(key));
    }
    e->beeped = FALSE;
    exec_command(e, &cmd);
    return TRUE;
}

gboolean vim_engine_feed(VimEngine* e, gunichar key) {
    key = vim_key_normalize(key);
    if (key == 0) return FALSE;
    gboolean handled = feed_key(e, key);
    if (e->rec_reg != 0 && e->replaying == 0) {
        if (e->rec_started) {
            e->rec_started = FALSE;
        } else if (handled) {
            g_string_append_unichar(e->rec, key);
        }
    }
    return handled;
}

void vim_e_feed_internal(VimEngine* e, const gchar* keys) {
    e->replaying++;
    for (const gchar* p = keys; p != NULL && *p != '\0'; p = g_utf8_next_char(p)) {
        vim_engine_feed(e, g_utf8_get_char(p));
    }
    e->replaying--;
}

void vim_engine_feed_keys(VimEngine* e, const gchar* keys) {
    vim_e_feed_internal(e, keys);
}

gboolean vim_engine_cancel_pending(VimEngine* e) {
    if (e == NULL || e->pending->len == 0) return FALSE;
    g_array_set_size(e->pending, 0);
    vim_e_state_changed(e);
    return TRUE;
}

void vim_engine_reset(VimEngine* e) {
    g_array_set_size(e->pending, 0);
    if (e->mode == VIM_MODE_CMDLINE) vim_engine_cmdline_cancel(e);
    if (vim_e_is_insert(e->mode)) vim_e_leave_insert(e, TRUE);
    if (vim_e_is_visual(e->mode)) vim_e_exit_visual(e);
    e->ins_ctrl_o = FALSE;
    vim_e_end_change(e);
    vim_e_set_mode(e, VIM_MODE_NORMAL);
    vim_e_clamp_cursor(e);
    vim_e_state_changed(e);
}
