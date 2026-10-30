/* Vim engine: modes, Normal / Visual / Insert commands, operators,
 * registers, marks, dot repeat and macros (Ex commands and search are in
 * vim_engine_ex.c) */
#include "vim_engine_private.h"
#include "vim_keys.h"
#include "vim_motion.h"
#include "vim_regex.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static gint off(const GtkTextIter* it) {
    return gtk_text_iter_get_offset(it);
}

static void iter_at(VimEngine* e, GtkTextIter* it, gint offset) {
    gtk_text_buffer_get_iter_at_offset(e->buf, it, offset);
}

static void iter_line(VimEngine* e, GtkTextIter* it, gint line) {
    gtk_text_buffer_get_iter_at_line(e->buf, it, line);
}

static gboolean is_visual(VimMode m) {
    return m == VIM_MODE_VISUAL || m == VIM_MODE_VISUAL_LINE || m == VIM_MODE_VISUAL_BLOCK;
}

static gboolean is_insert(VimMode m) {
    return m == VIM_MODE_INSERT || m == VIM_MODE_REPLACE;
}

static gboolean is_blank_char(gunichar c) {
    return c == ' ' || c == '\t';
}

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

static void beep(VimEngine* e) {
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

void vim_e_set_cursor(VimEngine* e, const GtkTextIter* it) {
    gtk_text_buffer_place_cursor(e->buf, it);
    if (is_visual(e->mode)) vim_e_update_visual(e);
}

static void remember_column(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    e->curswant = vim_iter_vcol(&it, e->opt.tabstop);
    e->want_eol = FALSE;
}

void vim_e_set_cursor_line(VimEngine* e, gint line, gboolean first_nonblank) {
    if (line < 0) line = 0;
    if (line > vim_e_last_line(e)) line = vim_e_last_line(e);
    GtkTextIter it;
    iter_line(e, &it, line);
    if (first_nonblank) vim_iter_first_nonblank(&it);
    if (!is_insert(e->mode)) vim_iter_clamp_normal(&it);
    vim_e_set_cursor(e, &it);
    remember_column(e);
}

static void clamp_cursor(VimEngine* e) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    GtkTextIter c = it;
    vim_iter_clamp_normal(&c);
    if (!gtk_text_iter_equal(&c, &it)) gtk_text_buffer_place_cursor(e->buf, &c);
}

gchar* vim_e_line_text(VimEngine* e, gint line) {
    GtkTextIter s, t;
    iter_line(e, &s, line);
    t = s;
    vim_iter_line_end(&t);
    return gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
}

static gchar* slice(VimEngine* e, gint a, gint b) {
    GtkTextIter s, t;
    iter_at(e, &s, a);
    iter_at(e, &t, b);
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

static void insert_at_cursor(VimEngine* e, const gchar* text) {
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
static gchar* line_indent_text(VimEngine* e, gint line) {
    GtkTextIter s, t;
    iter_line(e, &s, line);
    t = s;
    vim_iter_first_nonblank(&t);
    return gtk_text_buffer_get_slice(e->buf, &s, &t, TRUE);
}

static void set_line_indent(VimEngine* e, gint line, gint width) {
    GtkTextIter s, t;
    iter_line(e, &s, line);
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
static void undo_close_step(VimEngine* e) {
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
    if (!e->change_open && !e->ext_action) undo_close_step(e);
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
    if (!e->change_open) undo_close_step(e);
}

void vim_e_end_change(VimEngine* e) {
    if (!e->change_open) return;
    e->change_open = FALSE;
    undo_close_step(e);
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

static gboolean host_goto_definition(VimEngine* e, gboolean declaration);

static void jump_older(VimEngine* e, gint count, gboolean older) {
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
        if (e->host.jump_history == NULL || !e->host.jump_history(e->hd, older)) beep(e);
        return;
    }
    e->jump_pos = target;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(e->buf, &it, g_ptr_array_index(e->jumps, target));
    vim_iter_clamp_normal(&it);
    vim_e_set_cursor(e, &it);
    remember_column(e);
}

/* ------------------------------------------------------------------------- */
/* Registers                                                                 */
/* ------------------------------------------------------------------------- */

static void reg_store(VimEngine* e, gunichar name, const gchar* text, RegType type) {
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
        gunichar lower = g_unichar_tolower(name);
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
            reg_store(e, lower, joined, type);
            g_free(joined);
        } else {
            reg_store(e, lower, text, type);
        }
        name = lower;
        text = e->regs[lower].text;
    } else if (explicit_reg) {
        reg_store(e, name, text, type);
    }
    if (explicit_reg && name < 128 && e->regs[name].text != NULL) type = e->regs[name].type;
    gchar* copy = g_strdup(text);
    reg_store(e, '"', copy, type);
    g_free(copy);
    text = e->regs['"'].text;
    if (!explicit_reg) {
        if (kind == REG_YANK) {
            reg_store(e, '0', text, type);
        } else if (type == REG_LINE || strchr(text, '\n') != NULL) {
            for (gunichar r = '9'; r > '1'; r--) {
                g_free(e->regs[r].text);
                e->regs[r] = e->regs[r - 1];
                e->regs[r - 1].text = NULL;
            }
            reg_store(e, '1', text, type);
        } else {
            reg_store(e, '-', text, type);
        }
    }
    if (name == '+' || name == '*' || (!explicit_reg && e->opt.unnamedplus)) {
        reg_store(e, '+', text, type);
        reg_store(e, '*', text, type);
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
    reg_store(e, '+', text, type);
    reg_store(e, '*', text, type);
    if (e->opt.unnamedplus) reg_store(e, '"', text, type);
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

static void record_insert_keys(VimEngine* e, const gchar* keys) {
    if (!e->ins_repeating) g_string_append(e->ins_keys, keys);
    if (e->rec_reg != 0 && e->replaying == 0) g_string_append(e->rec, keys);
}

static void record_insert_key(VimEngine* e, gunichar k) {
    gchar buf[8];
    buf[g_unichar_to_utf8(k, buf)] = '\0';
    record_insert_keys(e, buf);
}

static void on_insert_text(GtkTextBuffer* buf, GtkTextIter* location, gchar* text, gint len,
                           gpointer data) {
    (void)buf;
    VimEngine* e = data;
    e->buffer_dirty = TRUE;
    gchar* t = g_strndup(text, len);
    undo_record(e, TRUE, off(location), t, (gint)g_utf8_strlen(t, -1));
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    if (e->self_edit > 0 || e->replaying > 0 || !is_insert(e->mode) || !gtk_text_iter_equal(&cur, location)) {
        g_free(t);
        return;
    }
    record_insert_keys(e, t);
    g_free(t);
}

static void on_delete_range(GtkTextBuffer* buf, GtkTextIter* start, GtkTextIter* end, gpointer data) {
    (void)buf;
    VimEngine* e = data;
    e->buffer_dirty = TRUE;
    if (!e->undoing) {
        gchar* t = gtk_text_iter_get_slice(start, end);
        undo_record(e, FALSE, off(start), t, off(end) - off(start));
        g_free(t);
    }
    if (e->self_edit > 0 || e->replaying > 0 || !is_insert(e->mode)) return;
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    gint n = off(end) - off(start);
    gunichar k = 0;
    if (gtk_text_iter_equal(&cur, end)) k = VIM_KEY_BS;
    else if (gtk_text_iter_equal(&cur, start)) k = VIM_KEY_DEL;
    if (k == 0) return;
    for (gint i = 0; i < n; i++) record_insert_key(e, k);
}

static void on_modified_changed(GtkTextBuffer* buf, gpointer data) {
    VimEngine* e = data;
    if (!gtk_text_buffer_get_modified(buf) && !e->undoing) {
        undo_close_step(e);
        e->saved_pos = e->undo_pos;
    }
}

static void on_mark_set(GtkTextBuffer* buf, GtkTextIter* location, GtkTextMark* mark, gpointer data) {
    (void)location;
    VimEngine* e = data;
    if (mark != gtk_text_buffer_get_insert(buf) || e->updating_visual) return;
    if (is_visual(e->mode)) vim_e_update_visual(e);
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

static GtkTextTag* ensure_tag(GtkTextBuffer* buf, const gchar* name, const gchar* bg) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, name);
    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buf, name, "background", bg, NULL);
    }
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
    e->tag_visual = ensure_tag(buffer, "vim-visual", "#264f78");
    e->tag_search = ensure_tag(buffer, "vim-search", "#613315");
    e->tag_incsearch = ensure_tag(buffer, "vim-incsearch", "#9e6a03");
    e->sig_insert = g_signal_connect(buffer, "insert-text", G_CALLBACK(on_insert_text), e);
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
static gboolean block_line_span(VimEngine* e, gint line, gint c1, gint c2, GtkTextIter* s, GtkTextIter* t) {
    gint ts = e->opt.tabstop;
    iter_line(e, s, line);
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
    if (is_visual(e->mode) && e->vis_anchor != NULL) {
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
                if (block_line_span(e, l, c1, c2, &a, &b)) {
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

static void enter_visual(VimEngine* e, VimMode mode) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    if (!is_visual(e->mode)) {
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
static void exit_visual(VimEngine* e) {
    if (!is_visual(e->mode)) return;
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
    e->last_vis_anchor_off = off(&a);
    e->last_vis_cursor_off = off(&c);
    vim_e_set_mode(e, VIM_MODE_NORMAL);
    vim_e_update_visual(e);
}

static void reselect_visual(VimEngine* e) {
    if (e->last_vis_mode == VIM_MODE_NORMAL) {
        beep(e);
        return;
    }
    VimMode m = e->last_vis_mode;
    gint a = e->last_vis_anchor_off, c = e->last_vis_cursor_off;
    gboolean eol = e->last_vis_eol;
    if (is_visual(e->mode)) {
        GtkTextIter ca, cc;
        visual_anchor(e, &ca);
        vim_e_cursor(e, &cc);
        e->last_vis_mode = e->mode;
        e->last_vis_anchor_off = off(&ca);
        e->last_vis_cursor_off = off(&cc);
        e->last_vis_eol = e->vis_eol;
    }
    GtkTextIter it;
    iter_at(e, &it, a);
    if (e->vis_anchor == NULL) {
        e->vis_anchor = gtk_text_buffer_create_mark(e->buf, NULL, &it, TRUE);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &it);
    }
    iter_at(e, &it, c);
    gtk_text_buffer_place_cursor(e->buf, &it);
    e->vis_eol = eol;
    vim_e_set_mode(e, m);
    vim_e_update_visual(e);
}

/* The selection as an operator range, then back to Normal mode */
static VimRange visual_range(VimEngine* e) {
    VimRange r = {0};
    GtkTextIter s, t;
    if (e->mode == VIM_MODE_VISUAL_BLOCK) {
        block_cols(e, &r.line1, &r.line2, &r.vcol1, &r.vcol2);
        r.type = REG_BLOCK;
        iter_line(e, &s, r.line1);
        iter_line(e, &t, r.line2);
        vim_iter_line_end(&t);
        r.start = off(&s);
        r.end = off(&t);
    } else {
        vim_e_visual_bounds(e, &s, &t);
        r.start = off(&s);
        r.end = off(&t);
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
/* Motions                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GtkTextIter pos;
    gboolean linewise;
    gboolean inclusive;
    gboolean jump;
    gboolean keep_want;   /* j / k: keep the remembered column */
    gboolean want_eol;    /* $ */
    gboolean numbered;    /* deletes go to "1 even within a line (%, n, `...) */
} Motion;

static gboolean motion_lines(VimEngine* e, const GtkTextIter* from, gint delta, Motion* m) {
    gint line = gtk_text_iter_get_line(from) + delta;
    gint last = vim_e_last_line(e);
    if (line < 0) line = 0;
    if (line > last) line = last;
    if (line == gtk_text_iter_get_line(from)) return FALSE;
    iter_line(e, &m->pos, line);
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
    iter_line(e, &m->pos, line);
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
            iter_line(e, &it, line);
            if (gtk_text_iter_get_char(&it) == brace) break;
        }
    }
    iter_line(e, &m->pos, line);
    if (forward && line == last && brace == '{') vim_iter_line_end(&m->pos);
    return !gtk_text_iter_equal(&m->pos, from);
}

static gboolean do_motion(VimEngine* e, const VimCmd* cmd, gboolean for_op, const GtkTextIter* from, Motion* m) {
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
                iter_line(e, &p, line);
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
                return motion_line_nonblank(e, cmd->count > 0 ? MIN(cmd->count - 1, vim_e_last_line(e))
                                                              : vim_e_last_line(e), m);
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
                iter_line(e, &p, line);
                vim_iter_line_end(&p);
                while (!gtk_text_iter_starts_line(&p)) {
                    GtkTextIter prev = p;
                    gtk_text_iter_backward_char(&prev);
                    if (!is_blank_char(gtk_text_iter_get_char(&prev))) break;
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
                gboolean ok = do_motion(e, &c, for_op, from, &mm);
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
static void move_cursor(VimEngine* e, const Motion* m) {
    if (m->jump) vim_e_push_jump(e);
    GtkTextIter pos = m->pos;
    if (e->mode == VIM_MODE_NORMAL || (is_visual(e->mode) && !m->want_eol)) {
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

/* ------------------------------------------------------------------------- */
/* Line operations shared with Ex commands                                   */
/* ------------------------------------------------------------------------- */

/* Text of lines l1..l2 with a final newline (register format) */
static gchar* lines_text(VimEngine* e, gint l1, gint l2) {
    GtkTextIter s, t;
    iter_line(e, &s, l1);
    iter_line(e, &t, l2);
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
    iter_line(e, &s, l1);
    if (l2 < last) {
        iter_line(e, &t, l2 + 1);
    } else {
        gtk_text_buffer_get_end_iter(e->buf, &t);
        if (l1 > 0) {
            /* Deleting the last lines: the newline before them goes too */
            iter_line(e, &s, l1 - 1);
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
        iter_line(e, &it, after_line + 1);
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
        set_line_indent(e, l, w);
    }
}

void vim_e_join_lines(VimEngine* e, gint l1, gint l2, gboolean spaces) {
    gint last = vim_e_last_line(e);
    if (l2 > last) l2 = last;
    GtkTextIter cursor;
    gboolean moved = FALSE;
    for (gint i = l1; i < l2; i++) {
        GtkTextIter end, next;
        iter_line(e, &end, l1);
        vim_iter_line_end(&end);
        next = end;
        gtk_text_iter_forward_char(&next);
        gboolean ends_space = FALSE;
        if (!gtk_text_iter_starts_line(&end)) {
            GtkTextIter prev = end;
            gtk_text_iter_backward_char(&prev);
            ends_space = is_blank_char(gtk_text_iter_get_char(&prev));
        }
        if (spaces) {
            while (!gtk_text_iter_ends_line(&next) && is_blank_char(gtk_text_iter_get_char(&next))) {
                gtk_text_iter_forward_char(&next);
            }
        }
        gboolean next_empty = gtk_text_iter_ends_line(&next);
        gunichar nc = gtk_text_iter_get_char(&next);
        gint pos = off(&end);
        vim_e_delete(e, &end, &next);
        /* One space between the lines, none after white space, before ')'
         * or when either side is empty; the cursor goes to the join */
        gboolean add_space = spaces && !ends_space && !next_empty && nc != ')' &&
                             !gtk_text_iter_starts_line(&end);
        if (add_space) vim_e_insert_text(e, &end, " ");
        iter_at(e, &cursor, pos);
        if (!add_space && gtk_text_iter_ends_line(&cursor) && !gtk_text_iter_starts_line(&cursor)) {
            gtk_text_iter_backward_char(&cursor);
        }
        moved = TRUE;
    }
    if (moved) {
        vim_iter_clamp_normal(&cursor);
        vim_e_set_cursor(e, &cursor);
        remember_column(e);
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
        iter_line(e, &s, l);
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
            set_line_indent(e, l, 0);
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
        set_line_indent(e, l, MAX(0, ind));
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
        gchar* indent = line_indent_text(e, l);
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
        iter_line(e, &s, l);
        iter_line(e, &t, pe);
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

static void transform_span(VimEngine* e, gint a, gint b, gunichar how) {
    if (b <= a) return;
    gchar* text = slice(e, a, b);
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
        iter_at(e, &s, a);
        iter_at(e, &t, b);
        vim_e_delete(e, &s, &t);
        vim_e_insert_text(e, &s, out->str);
    }
    g_string_free(out, TRUE);
    g_free(text);
}

/* ------------------------------------------------------------------------- */
/* Insert mode                                                               */
/* ------------------------------------------------------------------------- */

static void start_insert(VimEngine* e, gunichar kind, gint count, VimMode mode) {
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

static void open_line(VimEngine* e, gboolean below) {
    GtkTextIter it;
    vim_e_cursor(e, &it);
    gint line = gtk_text_iter_get_line(&it);
    gchar* ind = e->opt.autoindent ? line_indent_text(e, line) : g_strdup("");
    if (below) {
        vim_iter_line_end(&it);
        gchar* t = g_strconcat("\n", ind, NULL);
        vim_e_insert_text(e, &it, t);
        g_free(t);
    } else {
        gtk_text_iter_set_line_offset(&it, 0);
        GtkTextIter mark = it;
        gint start = off(&mark);
        gchar* t = g_strconcat(ind, "\n", NULL);
        vim_e_insert_text(e, &it, t);
        g_free(t);
        iter_at(e, &it, start + (gint)g_utf8_strlen(ind, -1));
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
        e->compl_start = off(&ws);
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
    iter_at(e, &s, e->compl_start);
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
        iter_line(e, &it, l);
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
    iter_line(e, &first, e->blk_first);
    vim_iter_set_vcol(&first, e->blk_vcol, ts);
    gtk_text_buffer_place_cursor(e->buf, &first);
}

static void leave_insert(VimEngine* e, gboolean move_left) {
    compl_end(e);
    e->ins_ctrl_r = FALSE;
    e->ins_ctrl_v = FALSE;
    if (e->ins_count > 1 && !e->ins_repeating && !e->blk_active) {
        gchar* keys = g_strdup(e->ins_keys->str);
        e->ins_repeating = TRUE;
        for (gint i = 1; i < e->ins_count; i++) {
            if (e->ins_kind == 'o') open_line(e, TRUE);
            else if (e->ins_kind == 'O') open_line(e, FALSE);
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
        reg_store(e, '.', text, REG_CHAR);
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
    clamp_cursor(e);
    remember_column(e);
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
        insert_at_cursor(e, "\t");
        return;
    }
    if (width <= 0) width = ts;
    GtkTextIter cur, s;
    vim_e_cursor(e, &cur);
    gint v = vim_iter_vcol(&cur, ts);
    gint target = (v / width + 1) * width;
    if (e->opt.expandtab) {
        gchar* sp = g_strnfill(target - v, ' ');
        insert_at_cursor(e, sp);
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
    if (!is_blank_char(gtk_text_iter_get_char(&prev))) return FALSE;
    gint v = vim_iter_vcol(&cur, ts);
    gint target = (v - 1) / width * width;
    s = cur;
    while (!gtk_text_iter_starts_line(&s) && vim_iter_vcol(&s, ts) > target) {
        GtkTextIter p = s;
        gtk_text_iter_backward_char(&p);
        if (!is_blank_char(gtk_text_iter_get_char(&p))) break;
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

static void insert_newline(VimEngine* e, gboolean indent) {
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    gchar* ind = (indent && e->opt.autoindent) ? line_indent_text(e, gtk_text_iter_get_line(&cur))
                                              : g_strdup("");
    /* Leading white space before the cursor does not stay on an emptied line */
    gchar* t = g_strconcat("\n", ind, NULL);
    vim_e_insert_text(e, &cur, t);
    gtk_text_buffer_place_cursor(e->buf, &cur);
    g_free(t);
    g_free(ind);
}

static gboolean insert_key(VimEngine* e, gunichar k) {
    gboolean replaying = e->replaying > 0;
    if (e->ins_ctrl_v) {
        e->ins_ctrl_v = FALSE;
        gchar buf[8] = {0};
        gunichar lit = k == VIM_KEY_TAB ? '\t' : k == VIM_KEY_CR ? '\r' : k == VIM_KEY_ESC ? 0x1b
                     : VIM_KEY_IS_CTRL(k) ? (gunichar)((k - 0xE100) & 0x1f) : k;
        g_unichar_to_utf8(lit, buf);
        insert_at_cursor(e, buf);
        record_insert_key(e, VIM_KEY_CTRL('v'));
        record_insert_key(e, k);
        return TRUE;
    }
    if (e->ins_ctrl_r) {
        e->ins_ctrl_r = FALSE;
        VimRegister r;
        if (vim_e_reg_get(e, k, &r) && r.text != NULL) {
            insert_at_cursor(e, r.text);
            record_insert_keys(e, r.text);
        } else {
            beep(e);
        }
        return TRUE;
    }
    if (e->compl_words != NULL && k != VIM_KEY_CTRL('n') && k != VIM_KEY_CTRL('p')) compl_end(e);

    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    switch (k) {
        case VIM_KEY_ESC: case VIM_KEY_CTRL('c'):
            leave_insert(e, TRUE);
            return TRUE;
        case VIM_KEY_CR:
            insert_newline(e, TRUE);
            record_insert_key(e, k);
            return TRUE;
        case VIM_KEY_NL:
            insert_newline(e, replaying ? FALSE : TRUE);
            record_insert_key(e, replaying ? k : VIM_KEY_CR);
            return TRUE;
        case VIM_KEY_TAB:
            if (e->opt.expandtab || soft_tab_width(e) > 0 || replaying) {
                insert_soft_tab(e);
                record_insert_key(e, k);
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
                record_insert_key(e, k);
                return TRUE;
            }
            if (delete_soft_tab(e)) {
                record_insert_key(e, k);
                return TRUE;
            }
            if (replaying) {
                if (!gtk_text_iter_is_start(&cur)) delete_before_cursor(e, 1);
                record_insert_key(e, k);
                return TRUE;
            }
            return FALSE;
        case VIM_KEY_DEL:
            if (replaying) {
                GtkTextIter t = cur;
                gtk_text_iter_forward_char(&t);
                vim_e_delete(e, &cur, &t);
                record_insert_key(e, k);
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
                    if (!is_blank_char(gtk_text_iter_get_char(&prev))) break;
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
            record_insert_key(e, k);
            return TRUE;
        }
        case VIM_KEY_CTRL('u'): {
            GtkTextIter s = cur, nb = cur;
            vim_iter_first_nonblank(&nb);
            if (gtk_text_iter_compare(&cur, &nb) > 0) s = nb;
            else gtk_text_iter_set_line_offset(&s, 0);
            vim_e_delete(e, &s, &cur);
            record_insert_key(e, k);
            return TRUE;
        }
        case VIM_KEY_CTRL('t'): case VIM_KEY_CTRL('d'): {
            gint line = gtk_text_iter_get_line(&cur);
            GtkTextIter end = cur;
            vim_iter_line_end(&end);
            gint from_end = off(&end) - off(&cur);
            vim_e_shift_lines(e, line, line, k == VIM_KEY_CTRL('t') ? 1 : -1);
            iter_line(e, &end, line);
            vim_iter_line_end(&end);
            GtkTextIter nc;
            iter_at(e, &nc, MAX(off(&end) - from_end, 0));
            GtkTextIter nb = end;
            vim_iter_first_nonblank(&nb);
            if (gtk_text_iter_get_line(&nc) != line || gtk_text_iter_compare(&nc, &nb) < 0) {
                if (gtk_text_iter_compare(&nc, &nb) < 0 || gtk_text_iter_get_line(&nc) != line) nc = nb;
            }
            gtk_text_buffer_place_cursor(e->buf, &nc);
            record_insert_key(e, k);
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
                insert_at_cursor(e, r.text);
                record_insert_keys(e, r.text);
            }
            if (k == VIM_KEY_CTRL('@')) leave_insert(e, TRUE);
            return TRUE;
        }
        case VIM_KEY_CTRL('e'): case VIM_KEY_CTRL('y'): {
            gint line = gtk_text_iter_get_line(&cur) + (k == VIM_KEY_CTRL('e') ? 1 : -1);
            if (line < 0 || line > vim_e_last_line(e)) {
                beep(e);
                return TRUE;
            }
            GtkTextIter src;
            iter_line(e, &src, line);
            vim_iter_set_vcol(&src, vim_iter_vcol(&cur, e->opt.tabstop), e->opt.tabstop);
            if (gtk_text_iter_ends_line(&src)) {
                beep(e);
                return TRUE;
            }
            gchar buf[8] = {0};
            g_unichar_to_utf8(gtk_text_iter_get_char(&src), buf);
            insert_at_cursor(e, buf);
            record_insert_keys(e, buf);
            return TRUE;
        }
        case VIM_KEY_CTRL('o'):
            e->ins_ctrl_o = TRUE;
            leave_insert(e, FALSE);
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
                iter_line(e, &p, line);
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
        insert_at_cursor(e, buf);
        record_insert_key(e, k);
        return TRUE;
    }
    if (replaying) {
        gchar buf[8] = {0};
        g_unichar_to_utf8(k, buf);
        insert_at_cursor(e, buf);
        record_insert_key(e, k);
        return TRUE;
    }
    return FALSE; /* the text view inserts it; on_insert_text records it */
}

/* ------------------------------------------------------------------------- */
/* Operators                                                                 */
/* ------------------------------------------------------------------------- */

static void range_iters(VimEngine* e, const VimRange* r, GtkTextIter* s, GtkTextIter* t) {
    if (r->type == REG_LINE) {
        iter_line(e, s, r->line1);
        if (r->line2 >= vim_e_last_line(e)) gtk_text_buffer_get_end_iter(e->buf, t);
        else iter_line(e, t, r->line2 + 1);
    } else {
        iter_at(e, s, r->start);
        iter_at(e, t, r->end);
    }
}

static gchar* block_text(VimEngine* e, const VimRange* r) {
    GString* out = g_string_new(NULL);
    for (gint l = r->line1; l <= r->line2; l++) {
        GtkTextIter a, b;
        if (block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) {
            gchar* t = gtk_text_iter_get_slice(&a, &b);
            g_string_append(out, t);
            g_free(t);
        }
        if (l < r->line2) g_string_append_c(out, '\n');
    }
    return g_string_free(out, FALSE);
}

static void delete_block(VimEngine* e, const VimRange* r) {
    for (gint l = r->line2; l >= r->line1; l--) {
        GtkTextIter a, b;
        if (block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) vim_e_delete(e, &a, &b);
    }
}

static void yank_range(VimEngine* e, const VimRange* r, gunichar reg, RegKind kind) {
    gchar* text;
    if (r->type == REG_BLOCK) {
        text = block_text(e, r);
    } else if (r->type == REG_LINE) {
        text = lines_text(e, r->line1, MIN(r->line2, vim_e_last_line(e)));
    } else {
        text = slice(e, r->start, r->end);
    }
    vim_e_reg_set(e, reg, text, r->type, kind);
    g_free(text);
}

static void report_lines(VimEngine* e, gint n, const gchar* what) {
    if (n > 2) vim_e_message(e, FALSE, "%d lines %s", n, what);
}

/* Run operator op on range r. Returns FALSE when it entered Insert mode. */
static void apply_operator(VimEngine* e, const VimCmd* cmd, VimRange* r, gboolean numbered) {
    gunichar o0 = cmd->op[0], o1 = cmd->op[1];
    gunichar reg = cmd->reg;
    GtkTextIter s, t;
    range_iters(e, r, &s, &t);
    vim_e_set_mark(e, '[', &s);
    vim_e_set_mark(e, ']', &t);
    gint nlines = r->line2 - r->line1 + 1;

    if (o0 == 'y') {
        yank_range(e, r, reg, REG_YANK);
        if (r->type == REG_BLOCK) {
            GtkTextIter it;
            iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
        } else if (r->type == REG_LINE) {
            if (vim_e_cursor_line(e) > r->line1) {
                GtkTextIter it;
                iter_line(e, &it, r->line1);
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
            yank_range(e, r, reg, REG_DELETE);
            delete_block(e, r);
            GtkTextIter it;
            iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
            if (change) {
                e->blk_active = TRUE;
                e->blk_first = r->line1;
                e->blk_last = r->line2;
                e->blk_vcol = r->vcol1;
                e->blk_append = FALSE;
                e->blk_eol = FALSE;
                start_insert(e, 'c', 1, VIM_MODE_INSERT);
            }
            return;
        }
        if (r->type == REG_LINE) {
            if (change) {
                yank_range(e, r, reg, REG_DELETE);
                gchar* ind = e->opt.autoindent ? line_indent_text(e, r->line1) : g_strdup("");
                GtkTextIter a, b;
                iter_line(e, &a, r->line1);
                iter_line(e, &b, MIN(r->line2, vim_e_last_line(e)));
                vim_iter_line_end(&b);
                vim_e_delete(e, &a, &b);
                vim_e_insert_text(e, &a, ind);
                gtk_text_buffer_place_cursor(e->buf, &a);
                g_free(ind);
                start_insert(e, 'c', 1, VIM_MODE_INSERT);
            } else {
                vim_e_delete_lines(e, r->line1, r->line2, reg, TRUE);
                report_lines(e, nlines, "fewer");
            }
            return;
        }
        gchar* text = slice(e, r->start, r->end);
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
            reg_store(e, '1', text, REG_CHAR);
        } else {
            vim_e_reg_set(e, reg, text, REG_CHAR, kind);
        }
        g_free(text);
        vim_e_delete(e, &s, &t);
        vim_e_set_mark(e, '.', &s);
        gtk_text_buffer_place_cursor(e->buf, &s);
        if (change) {
            start_insert(e, 'c', 1, VIM_MODE_INSERT);
        } else {
            clamp_cursor(e);
        }
        remember_column(e);
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
                if (block_line_span(e, l, r->vcol1, r->vcol2, &a, &b)) transform_span(e, off(&a), off(&b), o1);
            }
            GtkTextIter it;
            iter_line(e, &it, r->line1);
            vim_iter_set_vcol(&it, r->vcol1, e->opt.tabstop);
            vim_e_set_cursor(e, &it);
        } else {
            gint a = off(&s), b = off(&t);
            GtkTextIter keep;
            vim_e_cursor(e, &keep);
            gint keep_off = off(&keep);
            transform_span(e, a, b, o1);
            /* g~~ keeps the cursor; g~motion goes to the start */
            GtkTextIter it;
            iter_at(e, &it, cmd->kind == VIM_CMD_OPERATOR_LINES ? keep_off : a);
            vim_iter_clamp_normal(&it);
            vim_e_set_cursor(e, &it);
        }
        return;
    }
    if (o0 == 'g' && (o1 == 'q' || o1 == 'w')) {
        GtkTextIter keep;
        vim_e_cursor(e, &keep);
        gint keep_off = off(&keep);
        gint last = format_lines(e, r->line1, r->line2);
        if (o1 == 'q') {
            vim_e_set_cursor_line(e, last, TRUE);
        } else {
            GtkTextIter it;
            iter_at(e, &it, MIN(keep_off, gtk_text_buffer_get_char_count(e->buf)));
            vim_iter_clamp_normal(&it);
            vim_e_set_cursor(e, &it);
        }
        return;
    }
}

/* Range of an operator from the cursor to a motion */
static gboolean motion_range(VimEngine* e, const VimCmd* cmd, const Motion* m, const GtkTextIter* cursor,
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
        iter_line(e, &s, r->line1);
        t = s;
        r->start = off(&s);
        range_iters(e, r, &s, &t);
        r->end = off(&t);
    } else {
        r->type = REG_CHAR;
        r->start = off(&a);
        r->end = off(&b);
        r->line1 = gtk_text_iter_get_line(&a);
        r->line2 = gtk_text_iter_get_line(&b);
    }
    return TRUE;
}

static void exec_operator(VimEngine* e, const VimCmd* cmd_in) {
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
            beep(e);
            return;
        }
        memset(&r, 0, sizeof(r));
        r.start = off(&s);
        r.end = off(&t);
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
            !gtk_text_iter_ends_line(&cursor) && !is_blank_char(gtk_text_iter_get_char(&cursor))) {
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
            motion_range(e, &cmd, &m, &cursor, &r);
        } else {
            Motion m;
            if (!do_motion(e, &cmd, TRUE, &cursor, &m)) {
                beep(e);
                return;
            }
            if (m.jump) vim_e_push_jump(e);
            numbered = m.numbered;
            motion_range(e, &cmd, &m, &cursor, &r);
            if (!m.linewise && !m.inclusive && r.type == REG_CHAR && r.start == r.end && cmd.op[0] != 'c') {
                return;
            }
        }
    }
    apply_operator(e, &cmd, &r, numbered);
}

static void exec_operator_lines(VimEngine* e, const VimCmd* cmd) {
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
    range_iters(e, &r, &s, &t);
    r.start = off(&s);
    r.end = off(&t);
    apply_operator(e, cmd, &r, FALSE);
}

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
        iter_line(e, &a, first);
        vim_e_set_mark(e, '[', &a);
        GtkTextIter b;
        iter_line(e, &b, first + nl - 1);
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
            iter_line(e, &it, l);
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
        iter_line(e, &it, line);
        vim_iter_set_vcol(&it, vcol, ts);
        vim_iter_clamp_normal(&it);
        vim_e_set_cursor(e, &it);
    } else {
        GString* all = g_string_new(NULL);
        for (gint i = 0; i < count; i++) g_string_append(all, text);
        GtkTextIter at = cur;
        if (!before && !gtk_text_iter_ends_line(&at)) gtk_text_iter_forward_char(&at);
        gint start = off(&at);
        vim_e_insert_text(e, &at, all->str);
        gint end = off(&at);
        GtkTextIter a, b;
        iter_at(e, &a, start);
        iter_at(e, &b, end > start ? end - 1 : end);
        vim_e_set_mark(e, '[', &a);
        vim_e_set_mark(e, ']', &b);
        GtkTextIter c;
        if (gp) {
            iter_at(e, &c, end);
        } else if (strchr(all->str, '\n') != NULL) {
            c = a;
        } else {
            c = b;
        }
        if (!gp) vim_iter_clamp_normal(&c);
        vim_e_set_cursor(e, &c);
        remember_column(e);
        g_string_free(all, TRUE);
    }
    g_free(text);
}

/* Visual p / P: replace the selection with a register */
static void put_over_selection(VimEngine* e, gunichar reg, gint count, gboolean keep_register) {
    VimRegister r;
    if (!vim_e_reg_get(e, reg, &r) || r.text == NULL) {
        exit_visual(e);
        vim_e_message(e, TRUE, "E353: Nothing in register");
        return;
    }
    gchar* text = g_strdup(r.text);
    RegType type = r.type;
    VimRange vr = visual_range(e);
    exit_visual(e);
    GtkTextIter s, t;
    range_iters(e, &vr, &s, &t);
    /* The replaced text goes to the unnamed register (not with P) */
    if (vr.type == REG_LINE) {
        if (!keep_register) yank_range(e, &vr, 0, REG_DELETE);
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
        if (!keep_register) yank_range(e, &vr, 0, REG_DELETE);
        delete_block(e, &vr);
        GtkTextIter it;
        iter_line(e, &it, vr.line1);
        vim_iter_set_vcol(&it, vr.vcol1, e->opt.tabstop);
        gtk_text_buffer_place_cursor(e->buf, &it);
        /* Put through a scratch slot so the unnamed register keeps the deleted block */
        reg_store(e, 1, text, type);
        put_register(e, 1, count, TRUE, FALSE);
        reg_store(e, 1, NULL, REG_CHAR);
    } else {
        if (!keep_register) yank_range(e, &vr, 0, REG_DELETE);
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
            gint start = off(&s);
            vim_e_insert_text(e, &s, all->str);
            GtkTextIter c;
            iter_at(e, &c, off(&s) > start ? off(&s) - 1 : start);
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
        insert_newline(e, TRUE);
        return TRUE;
    }
    gchar buf[8] = {0};
    g_unichar_to_utf8(c, buf);
    GString* rep = g_string_new(NULL);
    for (gint i = 0; i < count; i++) g_string_append(rep, buf);
    gint start = off(&s);
    vim_e_delete(e, &s, &t);
    vim_e_insert_text(e, &s, rep->str);
    g_string_free(rep, TRUE);
    GtkTextIter c2;
    iter_at(e, &c2, start + count - 1);
    vim_e_set_cursor(e, &c2);
    return TRUE;
}

/* Ctrl-A / Ctrl-X on the number at or after the cursor, in [from, limit) */
static gboolean increment_at(VimEngine* e, GtkTextIter* from, gint limit_off, gint64 delta) {
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
    while (!gtk_text_iter_ends_line(&p) && off(&p) < limit_off && !g_ascii_isdigit(gtk_text_iter_get_char(&p))) {
        gtk_text_iter_forward_char(&p);
    }
    if (gtk_text_iter_ends_line(&p) || off(&p) >= limit_off) return FALSE;
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
    gint start = off(&s);
    vim_e_delete(e, &s, &t);
    vim_e_insert_text(e, &s, out);
    iter_at(e, from, start + (gint)g_utf8_strlen(out, -1) - 1);
    g_free(out);
    g_free(text);
    return TRUE;
}

static void scroll_page(VimEngine* e, gunichar k, gint count) {
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
        iter_line(e, &it, target);
        if (k == VIM_KEY_CTRL('e') || k == VIM_KEY_CTRL('y')) {
            vim_iter_set_vcol(&it, e->curswant, e->opt.tabstop);
        } else {
            vim_iter_first_nonblank(&it);
        }
        vim_iter_clamp_normal(&it);
        gtk_text_buffer_place_cursor(e->buf, &it);
        if (is_visual(e->mode)) vim_e_update_visual(e);
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
        iter_at(e, &a, op->offset);
        if (insert) {
            gtk_text_buffer_insert(e->buf, &a, op->text, -1);
        } else {
            GtkTextIter b;
            iter_at(e, &b, op->offset + op->chars);
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
    undo_close_step(e);
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
        iter_at(e, &it, cursor);
        vim_iter_clamp_normal(&it);
        gtk_text_buffer_place_cursor(e->buf, &it);
        remember_column(e);
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
    gint idx = 0, cur_off = off(&cur);
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

static void start_recording_dot(VimEngine* e, const VimCmd* cmd, const gchar* extra) {
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
    if (is_insert(e->mode)) {
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
        beep(e);
        return;
    }
    gchar* keys = g_strdup(e->dot->str);
    if (e->dot_visual) {
        /* Same size of selection, from the cursor */
        GtkTextIter cur;
        vim_e_cursor(e, &cur);
        VimMode m = e->dot_vis_mode;
        enter_visual(e, m);
        GtkTextIter end = cur;
        if (m == VIM_MODE_VISUAL && e->dot_vis_lines == 0) {
            for (gint i = 1; i < e->dot_vis_cols && !gtk_text_iter_ends_line(&end); i++) {
                gtk_text_iter_forward_char(&end);
            }
        } else {
            gint line = MIN(gtk_text_iter_get_line(&cur) + e->dot_vis_lines, vim_e_last_line(e));
            iter_line(e, &end, line);
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
        beep(e);
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

static void open_cmdline_for(VimEngine* e, gunichar prompt, const VimCmd* op) {
    e->cl_has_op = op != NULL;
    if (op != NULL) e->cl_op = *op;
    vim_e_cmdline_open(e, prompt, "");
}

static void exec_simple(VimEngine* e, const VimCmd* cmd) {
    gunichar k = cmd->key[0], k1 = cmd->key[1];
    gint count = cmd->count;
    gint n = vim_cmd_count1(cmd);
    GtkTextIter cur;
    vim_e_cursor(e, &cur);

    if (k == 'g') {
        switch (k1) {
            case 'J': {
                gint line = gtk_text_iter_get_line(&cur);
                vim_e_join_lines(e, line, line + MAX(n, 2) - 1, FALSE);
                start_recording_dot(e, cmd, NULL);
                return;
            }
            case 'p': case 'P':
                put_register(e, cmd->reg, n, k1 == 'P', TRUE);
                start_recording_dot(e, cmd, NULL);
                return;
            case 'v': reselect_visual(e); return;
            case 'i': {
                GtkTextIter m;
                if (vim_e_get_mark(e, '^', &m)) gtk_text_buffer_place_cursor(e->buf, &m);
                start_insert(e, 'i', count, VIM_MODE_INSERT);
                start_recording_dot(e, cmd, NULL);
                return;
            }
            case 'I':
                gtk_text_iter_set_line_offset(&cur, 0);
                gtk_text_buffer_place_cursor(e->buf, &cur);
                start_insert(e, 'I', count, VIM_MODE_INSERT);
                start_recording_dot(e, cmd, NULL);
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
            if (!shown) beep(e);
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
                    start_insert(e, 's', 1, VIM_MODE_INSERT);
                    start_recording_dot(e, cmd, NULL);
                } else {
                    beep(e);
                }
                return;
            }
            if (c.kind == VIM_CMD_OPERATOR_LINES) exec_operator_lines(e, &c);
            else exec_operator(e, &c);
            if (k != 'Y') start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'p': case 'P':
            put_register(e, cmd->reg, n, k == 'P', FALSE);
            start_recording_dot(e, cmd, NULL);
            return;
        case 'J': {
            gint line = gtk_text_iter_get_line(&cur);
            if (line >= vim_e_last_line(e)) {
                beep(e);
                return;
            }
            vim_e_join_lines(e, line, line + MAX(n, 2) - 1, TRUE);
            start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'r':
            if (!replace_chars(e, n, cmd->arg)) beep(e);
            else start_recording_dot(e, cmd, NULL);
            return;
        case 'R':
            start_insert(e, 'R', count, VIM_MODE_REPLACE);
            start_recording_dot(e, cmd, NULL);
            return;
        case '~': {
            GtkTextIter t = cur;
            for (gint i = 0; i < n && !gtk_text_iter_ends_line(&t); i++) gtk_text_iter_forward_char(&t);
            gint end = off(&t);
            transform_span(e, off(&cur), end, '~');
            GtkTextIter c;
            iter_at(e, &c, end);
            vim_iter_clamp_normal(&c);
            vim_e_set_cursor(e, &c);
            remember_column(e);
            start_recording_dot(e, cmd, NULL);
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
            start_insert(e, k, count, VIM_MODE_INSERT);
            start_recording_dot(e, cmd, NULL);
            return;
        }
        case 'o': case 'O':
            vim_e_begin_change(e);
            open_line(e, k == 'o');
            start_insert(e, k, count, VIM_MODE_INSERT);
            start_recording_dot(e, cmd, NULL);
            return;
        case 'v': enter_visual(e, VIM_MODE_VISUAL); return;
        case 'V': enter_visual(e, VIM_MODE_VISUAL_LINE); return;
        case VIM_KEY_CTRL('v'): enter_visual(e, VIM_MODE_VISUAL_BLOCK); return;
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
                else reg_store(e, r, keys, REG_CHAR);
                g_free(keys);
                vim_e_state_changed(e);
                return;
            }
            if (!(g_ascii_isalnum(cmd->arg) || cmd->arg == '"')) {
                beep(e);
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
                beep(e);
            }
            return;
        case VIM_KEY_CTRL('a'): case VIM_KEY_CTRL('x'): {
            GtkTextIter le = cur;
            vim_iter_line_end(&le);
            GtkTextIter p = cur;
            if (increment_at(e, &p, off(&le), (k == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n)) {
                vim_e_set_cursor(e, &p);
                remember_column(e);
                start_recording_dot(e, cmd, NULL);
            } else {
                beep(e);
            }
            return;
        }
        case VIM_KEY_CTRL('e'): case VIM_KEY_CTRL('y'): case VIM_KEY_CTRL('d'):
        case VIM_KEY_CTRL('u'): case VIM_KEY_CTRL('f'): case VIM_KEY_CTRL('b'):
            scroll_page(e, k, count);
            return;
        case VIM_KEY_CTRL('o'): jump_older(e, n, TRUE); return;
        case VIM_KEY_CTRL(']'):
            if (!host_goto_definition(e, FALSE)) beep(e);
            return;
        case VIM_KEY_TAB: jump_older(e, n, FALSE); return;
        case VIM_KEY_CTRL('g'): file_info(e); return;
        default:
            beep(e);
            return;
    }
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
        iter_at(e, &t, r->end);
        e->dot_vis_cols = gtk_text_iter_get_line_offset(&t);
    }
    e->dot_count = 0;
    e->dot_reg = cmd->reg;
    if (is_insert(e->mode)) {
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
        beep(e);
        return;
    }
    if (lw && e->mode == VIM_MODE_VISUAL) vim_e_set_mode(e, VIM_MODE_VISUAL_LINE);
    gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &s);
    gtk_text_iter_backward_char(&t);
    gtk_text_buffer_place_cursor(e->buf, &t);
    vim_e_update_visual(e);
    remember_column(e);
}

static void visual_block_insert(VimEngine* e, gboolean append, gboolean to_eol) {
    VimRange r = visual_range(e);
    VimMode mode = e->mode;
    exit_visual(e);
    gint ts = e->opt.tabstop;
    e->blk_active = TRUE;
    e->blk_first = r.line1;
    e->blk_last = r.line2;
    e->blk_append = append;
    e->blk_eol = to_eol || (append && r.vcol2 == VIM_MAXCOL);
    GtkTextIter it;
    iter_line(e, &it, r.line1);
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
    start_insert(e, append ? 'A' : 'I', 1, VIM_MODE_INSERT);
}

static void exec_visual(VimEngine* e, const VimCmd* cmd) {
    if (cmd->kind == VIM_CMD_MOTION) {
        if (cmd->key[0] == '/' || cmd->key[0] == '?') {
            open_cmdline_for(e, cmd->key[0], NULL);
            return;
        }
        GtkTextIter cur;
        vim_e_cursor(e, &cur);
        Motion m;
        if (!do_motion(e, cmd, FALSE, &cur, &m)) {
            beep(e);
            return;
        }
        if (e->mode == VIM_MODE_VISUAL_BLOCK && !m.want_eol && !m.keep_want) e->vis_eol = FALSE;
        move_cursor(e, &m);
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
        exit_visual(e);
        return;
    }
    if (k == 'v' || k == 'V' || k == VIM_KEY_CTRL('v')) {
        VimMode want = k == 'v' ? VIM_MODE_VISUAL : k == 'V' ? VIM_MODE_VISUAL_LINE : VIM_MODE_VISUAL_BLOCK;
        if (want == mode) exit_visual(e);
        else enter_visual(e, want);
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
        remember_column(e);
        return;
    }
    if (k == 'g' && k1 == 'v') {
        reselect_visual(e);
        return;
    }
    if (k == ':') {
        exit_visual(e);
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
        scroll_page(e, k, cmd->count);
        return;
    }
    if (k == 'I' || k == 'A') {
        if (mode == VIM_MODE_VISUAL) {
            /* Character selection: insert at its start / append after its end */
            GtkTextIter s, t;
            vim_e_visual_bounds(e, &s, &t);
            gint at = k == 'I' ? off(&s) : off(&t);
            exit_visual(e);
            GtkTextIter it;
            iter_at(e, &it, at);
            gtk_text_buffer_place_cursor(e->buf, &it);
            start_insert(e, k, 1, VIM_MODE_INSERT);
            return;
        }
        VimRange r = visual_range(e);
        visual_block_insert(e, k == 'A', FALSE);
        record_visual_dot(e, cmd, &r, mode);
        return;
    }

    /* Commands acting on the selection */
    VimRange r = visual_range(e);
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
        range_iters(e, &r, &s, &t);
        r.start = off(&s);
        r.end = off(&t);
    }
    if (op.op[0] != 0) {
        exit_visual(e);
        apply_operator(e, &op, &r, FALSE);
        if (change) record_visual_dot(e, cmd, &r, mode);
        return;
    }
    switch (k) {
        case 'r': {
            exit_visual(e);
            gchar buf[8] = {0};
            g_unichar_to_utf8(cmd->arg, buf);
            if (r.type == REG_BLOCK) {
                for (gint l = r.line1; l <= r.line2; l++) {
                    GtkTextIter a, b;
                    if (!block_line_span(e, l, r.vcol1, r.vcol2, &a, &b)) continue;
                    gint na = (gint)(off(&b) - off(&a));
                    GString* rep = g_string_new(NULL);
                    for (gint i = 0; i < na; i++) g_string_append(rep, buf);
                    vim_e_delete(e, &a, &b);
                    vim_e_insert_text(e, &a, rep->str);
                    g_string_free(rep, TRUE);
                }
            } else {
                GtkTextIter s, t;
                range_iters(e, &r, &s, &t);
                gchar* text = gtk_text_iter_get_slice(&s, &t);
                GString* rep = g_string_new(NULL);
                for (const gchar* p = text; *p; p = g_utf8_next_char(p)) {
                    if (*p == '\n') g_string_append_c(rep, '\n');
                    else g_string_append(rep, buf);
                }
                gint start = off(&s);
                vim_e_delete(e, &s, &t);
                vim_e_insert_text(e, &s, rep->str);
                iter_at(e, &s, start);
                g_string_free(rep, TRUE);
                g_free(text);
            }
            GtkTextIter it;
            iter_at(e, &it, r.start);
            if (r.type == REG_BLOCK) {
                iter_line(e, &it, r.line1);
                vim_iter_set_vcol(&it, r.vcol1, e->opt.tabstop);
            }
            vim_e_set_cursor(e, &it);
            record_visual_dot(e, cmd, &r, mode);
            return;
        }
        case 'J':
            exit_visual(e);
            vim_e_join_lines(e, r.line1, MAX(r.line2, r.line1 + 1), TRUE);
            record_visual_dot(e, cmd, &r, mode);
            return;
        case 'g':
            if (k1 == 'J') {
                exit_visual(e);
                vim_e_join_lines(e, r.line1, MAX(r.line2, r.line1 + 1), FALSE);
                record_visual_dot(e, cmd, &r, mode);
                return;
            }
            if (k1 == VIM_KEY_CTRL('a') || k1 == VIM_KEY_CTRL('x')) {
                exit_visual(e);
                gint64 step = (k1 == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n;
                gint64 delta = step;
                for (gint l = r.line1; l <= r.line2; l++) {
                    GtkTextIter p, le;
                    iter_line(e, &p, l);
                    le = p;
                    vim_iter_line_end(&le);
                    if (increment_at(e, &p, off(&le), delta)) delta += step;
                }
                vim_e_set_cursor_line(e, r.line1, FALSE);
                return;
            }
            exit_visual(e);
            return;
        case 'p': case 'P':
            put_over_selection(e, cmd->reg, n, k == 'P');
            record_visual_dot(e, cmd, &r, mode);
            return;
        case VIM_KEY_CTRL('a'): case VIM_KEY_CTRL('x'): {
            exit_visual(e);
            gint64 delta = (k == VIM_KEY_CTRL('a') ? 1 : -1) * (gint64)n;
            for (gint l = r.line1; l <= r.line2; l++) {
                GtkTextIter p, le;
                iter_line(e, &p, l);
                if (l == r.line1 && r.type == REG_CHAR) iter_at(e, &p, r.start);
                le = p;
                vim_iter_line_end(&le);
                increment_at(e, &p, off(&le), delta);
            }
            vim_e_set_cursor_line(e, r.line1, FALSE);
            record_visual_dot(e, cmd, &r, mode);
            return;
        }
        default:
            beep(e);
            return;
    }
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
    if (off(&ins) != e->last_cursor_off) {
        /* Moved by a click or by the application: j / k start from here */
        e->curswant = vim_iter_vcol(&ins, e->opt.tabstop);
        e->want_eol = FALSE;
    }
    gtk_text_buffer_get_iter_at_mark(e->buf, &bound, gtk_text_buffer_get_selection_bound(e->buf));
    if (gtk_text_iter_equal(&ins, &bound)) {
        if (e->mode == VIM_MODE_NORMAL) clamp_cursor(e);
        return;
    }
    if (e->mode != VIM_MODE_NORMAL && !is_visual(e->mode)) return;
    GtkTextIter cur = ins;
    if (gtk_text_iter_compare(&ins, &bound) > 0) gtk_text_iter_backward_char(&cur);
    e->updating_visual = TRUE;
    gtk_text_buffer_place_cursor(e->buf, &bound);
    e->updating_visual = FALSE;
    if (!is_visual(e->mode)) {
        enter_visual(e, VIM_MODE_VISUAL);
    } else {
        gtk_text_buffer_move_mark(e->buf, e->vis_anchor, &bound);
    }
    gtk_text_buffer_place_cursor(e->buf, &cur);
    vim_e_update_visual(e);
}

/* Common end of a key: undo step, cursor, highlight. resume_insert: a
 * Normal command ran, so a pending Ctrl-O goes back to Insert mode. */
static void finish_key(VimEngine* e, gboolean resume_insert) {
    if (!is_insert(e->mode) && e->mode != VIM_MODE_CMDLINE) vim_e_end_change(e);
    if (e->mode == VIM_MODE_NORMAL && !e->ins_ctrl_o) clamp_cursor(e);
    if (resume_insert && e->ins_ctrl_o && e->mode == VIM_MODE_NORMAL) {
        e->ins_ctrl_o = FALSE;
        if (e->want_eol) {
            /* <C-o>$ goes after the last character */
            GtkTextIter it;
            vim_e_cursor(e, &it);
            vim_iter_line_end(&it);
            gtk_text_buffer_place_cursor(e->buf, &it);
        }
        start_insert(e, 'i', 1, VIM_MODE_INSERT);
        if (e->dot_pending != NULL) g_string_free(e->dot_pending, TRUE);
        e->dot_pending = g_string_new("i");
    }
    if (e->buffer_dirty && e->hl_active && !is_insert(e->mode)) {
        e->buffer_dirty = FALSE;
        vim_e_highlight_search(e);
    }
    GtkTextIter cur;
    vim_e_cursor(e, &cur);
    e->last_cursor_off = off(&cur);
    vim_e_state_changed(e);
}

static void after_command(VimEngine* e) {
    finish_key(e, TRUE);
}

/* Ask the host for the definition of the word under the cursor; the jump
 * list remembers where it started. FALSE when the host does not look up. */
static gboolean host_goto_definition(VimEngine* e, gboolean declaration) {
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
    if (is_visual(e->mode)) {
        exec_visual(e, cmd);
    } else {
        switch (cmd->kind) {
            case VIM_CMD_MOTION: {
                if (cmd->key[0] == '/' || cmd->key[0] == '?') {
                    open_cmdline_for(e, cmd->key[0], NULL);
                    break;
                }
                /* gd / gD in a code file: the host's definition lookup */
                if (cmd->key[0] == 'g' && (cmd->key[1] == 'd' || cmd->key[1] == 'D') &&
                    host_goto_definition(e, cmd->key[1] == 'D')) {
                    break;
                }
                GtkTextIter cur;
                vim_e_cursor(e, &cur);
                Motion m;
                if (do_motion(e, cmd, FALSE, &cur, &m)) move_cursor(e, &m);
                else beep(e);
                break;
            }
            case VIM_CMD_OPERATOR:
                if (cmd->key[0] == '/' || cmd->key[0] == '?') {
                    open_cmdline_for(e, cmd->key[0], cmd);
                    break;
                }
                exec_operator(e, cmd);
                if (cmd->op[0] != 'y' && !e->beeped) start_recording_dot(e, cmd, NULL);
                break;
            case VIM_CMD_OPERATOR_LINES:
                exec_operator_lines(e, cmd);
                if (cmd->op[0] != 'y') start_recording_dot(e, cmd, NULL);
                break;
            case VIM_CMD_SIMPLE:
                exec_simple(e, cmd);
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
        move_cursor(e, &m);
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
    motion_range(e, &cmd, &m, &cur, &r);
    apply_operator(e, &cmd, &r, TRUE);
    if (cmd.op[0] != 'y') {
        GString* extra = g_string_new(NULL);
        g_string_append(extra, e->last_pat != NULL ? e->last_pat : "");
        g_string_append_unichar(extra, VIM_KEY_CR);
        start_recording_dot(e, &cmd, extra->str);
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
    if (is_insert(e->mode)) {
        gboolean handled = insert_key(e, key);
        if (!is_insert(e->mode)) finish_key(e, FALSE);
        return handled;
    }
    sync_mouse_selection(e);
    if (e->pending->len == 0 && (key == VIM_KEY_ESC || key == VIM_KEY_CTRL('c')) && !is_visual(e->mode)) {
        return TRUE;
    }
    g_array_append_val(e->pending, key);
    VimCmd cmd;
    VimParseStatus st = vim_parse((const gunichar*)e->pending->data, e->pending->len, is_visual(e->mode),
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

void vim_engine_reset(VimEngine* e) {
    g_array_set_size(e->pending, 0);
    if (e->mode == VIM_MODE_CMDLINE) vim_engine_cmdline_cancel(e);
    if (is_insert(e->mode)) leave_insert(e, TRUE);
    if (is_visual(e->mode)) exit_visual(e);
    e->ins_ctrl_o = FALSE;
    vim_e_end_change(e);
    vim_e_set_mode(e, VIM_MODE_NORMAL);
    clamp_cursor(e);
    vim_e_state_changed(e);
}
