/* Emacs editor mode (visual clone): modeline, minibuffer, prefix keys and commands. */
#include "editor/editor_modes_private.h"
#include "editor/editor.h"
#include "theme/text_tags.h"
#include "code/code_nav.h"
#include "code/code_help.h"
#include "code/code_refs.h"
#include "code/code_symbols.h"
#include "editor/editor_tagbar.h"
#include "core/app_events.h"
#include "explorer/nav_keys.h"
#include "search/quick_open.h"
#include "editor_dunnet.h"
#include <ctype.h>
#include <string.h>

void emacs_echo(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    if (st->emacs_mb_echo != NULL) {
        gtk_label_set_text(GTK_LABEL(st->emacs_mb_echo), msg ? msg : "");
    }
}

/* ------------------------------------------------------------------------- */
/* Block cursor                                                              */
/* ------------------------------------------------------------------------- */

#define BLOCK_CURSOR_TAG "emacs_block_cursor"

/* GtkTextView only draws a block cursor in overwrite mode, which would
 * replace text while typing: the character under the cursor gets an
 * inverted-colour tag instead, and a box stands in at line ends. */
static void block_cursor_update(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    if (st == NULL || tab->view == NULL) return;
    /* The tag is the buffer's: drawn by the view of a split file that has the cursor */
    if (tab->view_passive) return;
    GtkTextBuffer* b = tab->buffer;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(b);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, BLOCK_CURSOR_TAG);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    if (tag != NULL) gtk_text_buffer_remove_tag(b, tag, &s, &e);
    if (!st->emacs_block_on) {
        if (st->emacs_block_eol != NULL) gtk_widget_set_visible(st->emacs_block_eol, FALSE);
        return;
    }
    if (tag == NULL) {
        tag = text_tag_create(b, BLOCK_CURSOR_TAG, "background", "@editor_cursor",
                                         "foreground", "@bg_view", NULL);
    }
    gtk_text_tag_set_priority(tag, gtk_text_tag_table_get_size(table) - 1);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(b, &it, gtk_text_buffer_get_insert(b));
    if (!gtk_text_iter_is_end(&it) && !gtk_text_iter_ends_line(&it)) {
        GtkTextIter next = it;
        gtk_text_iter_forward_char(&next);
        gtk_text_buffer_apply_tag(b, tag, &it, &next);
        if (st->emacs_block_eol != NULL) gtk_widget_set_visible(st->emacs_block_eol, FALSE);
        return;
    }
    /* Line end: a box one character wide */
    GdkRectangle r;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(tab->view), &it, &r);
    PangoLayout* layout = gtk_widget_create_pango_layout(tab->view, "M");
    gint cw = 8, ch = 16;
    pango_layout_get_pixel_size(layout, &cw, &ch);
    g_object_unref(layout);
    if (st->emacs_block_eol == NULL) {
        st->emacs_block_eol = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(st->emacs_block_eol, "emacs-block-cursor-eol");
        gtk_widget_set_can_target(st->emacs_block_eol, FALSE);
        gtk_text_view_add_overlay(GTK_TEXT_VIEW(tab->view), st->emacs_block_eol, r.x, r.y);
    } else {
        gtk_text_view_move_overlay(GTK_TEXT_VIEW(tab->view), st->emacs_block_eol, r.x, r.y);
    }
    gtk_widget_set_size_request(st->emacs_block_eol, MAX(cw, 2), MAX(r.height, ch));
    gtk_widget_set_visible(st->emacs_block_eol, TRUE);
}

static void on_block_cursor_moved(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    block_cursor_update(data);
}

static void on_block_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    block_cursor_update(data);
}

void emacs_block_cursor_set(EditorTab* tab, gboolean on) {
    EditorModeState* st = tab->mode_state;
    if (st == NULL || tab->view == NULL) return;
    /* Once per view: the views of a split file share the buffer */
    if (g_object_get_data(G_OBJECT(tab->view), "emacs-block-cursor-wired") == NULL) {
        g_object_set_data(G_OBJECT(tab->view), "emacs-block-cursor-wired", GINT_TO_POINTER(1));
        g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_block_cursor_moved), tab);
        g_signal_connect_after(tab->buffer, "changed", G_CALLBACK(on_block_buffer_changed), tab);
    }
    st->emacs_block_on = on;
    if (on) gtk_widget_add_css_class(tab->view, "emacs-block-cursor");
    else gtk_widget_remove_css_class(tab->view, "emacs-block-cursor");
    block_cursor_update(tab);
}

/* ------------------------------------------------------------------------- */
/* Incremental search (C-s / C-r), as in Emacs                               */
/* ------------------------------------------------------------------------- */

#define ISEARCH_TAG "emacs_isearch"
#define ISEARCH_LAZY_TAG "emacs_isearch_lazy"
#define ISEARCH_MAX_LAZY 10000

typedef struct {
    gchar* query;
    gint ms, me;
    gboolean has, failing, wrapped, forward;
} EmacsIsearchStep;

static gboolean is_isearch(EditorModeState* st) {
    return st->emacs_mini_mode == EMACS_MINI_ISEARCH_FWD || st->emacs_mini_mode == EMACS_MINI_ISEARCH_BWD;
}

static void isearch_step_clear(gpointer data) {
    g_free(((EmacsIsearchStep*)data)->query);
}

/* Case-insensitive unless the query has an upper-case letter */
static GtkTextSearchFlags isearch_flags(const gchar* q) {
    for (const gchar* p = q; *p != '\0'; p = g_utf8_next_char(p)) {
        if (g_unichar_isupper(g_utf8_get_char(p))) return GTK_TEXT_SEARCH_TEXT_ONLY;
    }
    return GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE;
}

static const gchar* isearch_query(EditorModeState* st) {
    return gtk_editable_get_text(GTK_EDITABLE(st->emacs_mb_entry));
}

/* Search q from offset from; forward finds a match starting at or after it,
 * backward one ending at or before it */
static gboolean isearch_find(EditorTab* tab, const gchar* q, gint from, gboolean forward,
                             gint* ms, gint* me) {
    GtkTextIter it, s, e;
    gtk_text_buffer_get_iter_at_offset(tab->buffer, &it, from);
    gboolean found = forward
        ? gtk_text_iter_forward_search(&it, q, isearch_flags(q), &s, &e, NULL)
        : gtk_text_iter_backward_search(&it, q, isearch_flags(q), &s, &e, NULL);
    if (!found) return FALSE;
    *ms = gtk_text_iter_get_offset(&s);
    *me = gtk_text_iter_get_offset(&e);
    return TRUE;
}

/* bg: a palette role ("@editor_match") */
static GtkTextTag* isearch_tag(GtkTextBuffer* b, const gchar* name, const gchar* bg) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(b);
    GtkTextTag* t = text_tag_ensure(b, name, "background", bg, NULL);
    /* Above the syntax highlighting and the selection-like tags */
    gtk_text_tag_set_priority(t, gtk_text_tag_table_get_size(table) - 1);
    return t;
}

static void isearch_clear_highlight(EditorTab* tab) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(tab->buffer);
    GtkTextTag* t = gtk_text_tag_table_lookup(table, ISEARCH_TAG);
    if (t != NULL) gtk_text_buffer_remove_tag(tab->buffer, t, &s, &e);
    t = gtk_text_tag_table_lookup(table, ISEARCH_LAZY_TAG);
    if (t != NULL) gtk_text_buffer_remove_tag(tab->buffer, t, &s, &e);
}

/* Show the search state: prompt, highlighted matches, match count, cursor */
static void isearch_update(EditorTab* tab, EditorModeState* st) {
    const gchar* q = isearch_query(st);
    /* "Failing wrapped I-search backward: " */
    gboolean failing = st->emacs_is_failing && q[0] != '\0';
    gchar* prompt = g_strdup_printf("%s%s%s%s: ", failing ? "Failing " : "",
                                    st->emacs_is_wrapped ? (failing ? "wrapped " : "Wrapped ") : "",
                                    "I-search",
                                    st->emacs_is_forward ? "" : " backward");
    gtk_label_set_text(GTK_LABEL(st->emacs_mb_prompt), prompt);
    g_free(prompt);
    /* The query this state belongs to: a typed character pushes it (DEL) */
    g_object_set_data_full(G_OBJECT(st->emacs_mb_entry), "isearch-query", g_strdup(q), g_free);

    isearch_clear_highlight(tab);
    gint total = 0, index = 0;
    if (q[0] != '\0') {
        GtkTextTag* lazy = isearch_tag(tab->buffer, ISEARCH_LAZY_TAG, "@editor_match");
        GtkTextIter it, s, e;
        gtk_text_buffer_get_start_iter(tab->buffer, &it);
        while (total < ISEARCH_MAX_LAZY &&
               gtk_text_iter_forward_search(&it, q, isearch_flags(q), &s, &e, NULL)) {
            total++;
            if (st->emacs_is_has && gtk_text_iter_get_offset(&s) <= st->emacs_is_ms) index = total;
            gtk_text_buffer_apply_tag(tab->buffer, lazy, &s, &e);
            if (gtk_text_iter_equal(&s, &e) && !gtk_text_iter_forward_char(&e)) break;
            it = e;
        }
    }
    GtkTextIter cur;
    if (st->emacs_is_has) {
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &s, st->emacs_is_ms);
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &e, st->emacs_is_me);
        gtk_text_buffer_apply_tag(tab->buffer, isearch_tag(tab->buffer, ISEARCH_TAG, "@editor_match_current"), &s, &e);
        cur = st->emacs_is_forward ? e : s;
    } else {
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &cur, st->emacs_is_origin);
    }
    gtk_text_buffer_place_cursor(tab->buffer, &cur);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &cur, 0.15, FALSE, 0.0, 0.0);

    gchar* hint = q[0] == '\0' ? g_strdup("")
                  : st->emacs_is_failing ? g_strdup_printf("[?/%d%s]", total, total >= ISEARCH_MAX_LAZY ? "+" : "")
                  : g_strdup_printf("[%d/%d%s]", index, total, total >= ISEARCH_MAX_LAZY ? "+" : "");
    gtk_label_set_text(GTK_LABEL(st->emacs_mb_echo), hint);
    gtk_widget_set_visible(st->emacs_mb_echo, hint[0] != '\0');
    g_free(hint);
}

/* Remember the current state, for DEL */
static void isearch_push(EditorModeState* st) {
    EmacsIsearchStep step = {g_strdup(isearch_query(st)), st->emacs_is_ms, st->emacs_is_me,
                             st->emacs_is_has, st->emacs_is_failing, st->emacs_is_wrapped,
                             st->emacs_is_forward};
    g_array_append_val(st->emacs_is_steps, step);
}

static void isearch_set_query(EditorModeState* st, const gchar* q) {
    st->emacs_is_setting = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(st->emacs_mb_entry), q);
    gtk_editable_set_position(GTK_EDITABLE(st->emacs_mb_entry), -1);
    st->emacs_is_setting = FALSE;
}

static void isearch_start(EditorTab* tab, EditorModeState* st, gboolean forward);

/* The query changed (typing, C-w): extend the match in place, or fail */
static void isearch_query_changed(EditorTab* tab, EditorModeState* st) {
    const gchar* q = isearch_query(st);
    if (q[0] == '\0') {
        st->emacs_is_has = FALSE;
        st->emacs_is_failing = FALSE;
        st->emacs_is_wrapped = FALSE;
    } else if (!st->emacs_is_failing) {
        gint from;
        if (st->emacs_is_forward) {
            from = st->emacs_is_has ? st->emacs_is_ms : st->emacs_is_origin;
        } else {
            gint end = gtk_text_buffer_get_char_count(tab->buffer);
            from = st->emacs_is_has ? MIN(st->emacs_is_ms + (gint)g_utf8_strlen(q, -1), end)
                                    : st->emacs_is_origin;
        }
        gint ms, me;
        if (isearch_find(tab, q, from, st->emacs_is_forward, &ms, &me)) {
            st->emacs_is_ms = ms;
            st->emacs_is_me = me;
            st->emacs_is_has = TRUE;
        } else {
            st->emacs_is_failing = TRUE;
        }
    }
    isearch_update(tab, st);
}

/* C-s / C-r while searching: next match, change direction, wrap, or reuse
 * the last search when the query is empty */
static void isearch_repeat(EditorTab* tab, EditorModeState* st, gboolean forward) {
    const gchar* q = isearch_query(st);
    isearch_push(st);
    if (q[0] == '\0') {
        if (st->emacs_last_search == NULL || st->emacs_last_search[0] == '\0') {
            g_array_set_size(st->emacs_is_steps, st->emacs_is_steps->len - 1);
            return;
        }
        st->emacs_is_forward = forward;
        isearch_set_query(st, st->emacs_last_search);
        isearch_query_changed(tab, st);
        return;
    }
    if (forward != st->emacs_is_forward) {
        /* Reversing keeps the match: the cursor goes to its other end */
        st->emacs_is_forward = forward;
        isearch_update(tab, st);
        return;
    }
    gint from;
    if (st->emacs_is_failing) {
        from = forward ? 0 : gtk_text_buffer_get_char_count(tab->buffer);
        st->emacs_is_wrapped = TRUE;
    } else if (st->emacs_is_has) {
        from = forward ? st->emacs_is_me : st->emacs_is_ms;
        if (st->emacs_is_ms == st->emacs_is_me) from += forward ? 1 : -1;
    } else {
        from = st->emacs_is_origin;
    }
    gint ms, me;
    if (isearch_find(tab, q, from, forward, &ms, &me)) {
        st->emacs_is_ms = ms;
        st->emacs_is_me = me;
        st->emacs_is_has = TRUE;
        st->emacs_is_failing = FALSE;
    } else {
        st->emacs_is_failing = TRUE;
    }
    isearch_update(tab, st);
}

/* DEL: undo the last search step (a typed character or a C-s / C-r) */
static void isearch_backspace(EditorTab* tab, EditorModeState* st) {
    if (st->emacs_is_steps->len == 0) return;
    EmacsIsearchStep* step = &g_array_index(st->emacs_is_steps, EmacsIsearchStep, st->emacs_is_steps->len - 1);
    isearch_set_query(st, step->query);
    st->emacs_is_ms = step->ms;
    st->emacs_is_me = step->me;
    st->emacs_is_has = step->has;
    st->emacs_is_failing = step->failing;
    st->emacs_is_wrapped = step->wrapped;
    st->emacs_is_forward = step->forward;
    g_array_set_size(st->emacs_is_steps, st->emacs_is_steps->len - 1);
    isearch_update(tab, st);
}

/* C-w: add the rest of the word after the cursor to the query */
static void isearch_yank_word(EditorTab* tab, EditorModeState* st) {
    gint from = st->emacs_is_has ? st->emacs_is_me : st->emacs_is_origin;
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_offset(tab->buffer, &s, from);
    e = s;
    if (!gtk_text_iter_forward_word_end(&e)) gtk_text_buffer_get_end_iter(tab->buffer, &e);
    gchar* word = gtk_text_buffer_get_text(tab->buffer, &s, &e, FALSE);
    if (word[0] != '\0') {
        isearch_push(st);
        gchar* q = g_strconcat(isearch_query(st), word, NULL);
        isearch_set_query(st, q);
        g_free(q);
        isearch_query_changed(tab, st);
    }
    g_free(word);
}

static void isearch_start(EditorTab* tab, EditorModeState* st, gboolean forward) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    gtk_text_buffer_place_cursor(tab->buffer, &it); /* drop the selection */
    st->emacs_mark_active = FALSE;
    st->emacs_is_origin = gtk_text_iter_get_offset(&it);
    st->emacs_is_has = FALSE;
    st->emacs_is_failing = FALSE;
    st->emacs_is_wrapped = FALSE;
    st->emacs_is_forward = forward;
    if (st->emacs_is_steps == NULL) {
        st->emacs_is_steps = g_array_new(FALSE, TRUE, sizeof(EmacsIsearchStep));
        g_array_set_clear_func(st->emacs_is_steps, isearch_step_clear);
    }
    g_array_set_size(st->emacs_is_steps, 0);
    isearch_update(tab, st);
}

static void isearch_end(EditorTab* tab, EditorModeState* st) {
    isearch_clear_highlight(tab);
    if (st->emacs_is_steps != NULL) g_array_set_size(st->emacs_is_steps, 0);
}

void emacs_close_minibuffer(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    if (is_isearch(st)) isearch_end(tab, st);
    st->emacs_mini_mode = EMACS_MINI_NONE;
    st->emacs_prefix = EMACS_PREFIX_NONE;
    st->emacs_prompt_cb = NULL;
    g_clear_pointer(&st->emacs_prompt_data, g_free);
    g_clear_pointer(&st->emacs_completions, g_ptr_array_unref);
    gtk_widget_set_visible(st->emacs_mb_prompt, FALSE);
    gtk_widget_set_visible(st->emacs_mb_entry, FALSE);
    gtk_widget_set_visible(st->emacs_mb_echo, TRUE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

/* M-x command names offered by Tab completion */
static const gchar* MX_COMMANDS[] = {
    "display-line-numbers-mode", "dunnet", "fill-paragraph", "goto-line", "imenu-list",
    "notepad-mode", "org-mode", "query-replace", "redo", "save-buffer",
    "set-justification-center", "set-justification-full",
    "set-justification-left", "set-justification-right", "text-mode",
    "toggle-truncate-lines", "undo", "vim-mode", "visual-line-mode",
    "write-file",
};

static gint compare_names(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const gchar* const*)a, *(const gchar* const*)b);
}

void emacs_prompt_set_completions(EditorTab* tab, GPtrArray* candidates, gboolean last_word) {
    EditorModeState* st = tab->mode_state;
    if (st->emacs_completions != NULL) g_ptr_array_unref(st->emacs_completions);
    st->emacs_completions = candidates;
    st->emacs_complete_word = last_word;
}

static void emacs_open_minibuffer(EditorTab* tab, EmacsMiniMode mode,
                                  const gchar* prompt, const gchar* default_val) {
    EditorModeState* st = tab->mode_state;
    st->emacs_mini_mode = mode;
    g_clear_pointer(&st->emacs_completions, g_ptr_array_unref);
    if (mode == EMACS_MINI_MX) {
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < G_N_ELEMENTS(MX_COMMANDS); i++) g_ptr_array_add(names, g_strdup(MX_COMMANDS[i]));
        orgmode_editor_add_command_names(tab, names);
        g_ptr_array_sort(names, compare_names);
        emacs_prompt_set_completions(tab, names, FALSE);
    }
    gtk_label_set_text(GTK_LABEL(st->emacs_mb_prompt), prompt);
    gtk_editable_set_text(GTK_EDITABLE(st->emacs_mb_entry), default_val ? default_val : "");
    gtk_widget_set_visible(st->emacs_mb_prompt, TRUE);
    gtk_widget_set_visible(st->emacs_mb_entry, TRUE);
    gtk_widget_set_visible(st->emacs_mb_echo, FALSE);
    gtk_widget_grab_focus(st->emacs_mb_entry);
}

void emacs_prompt(EditorTab* tab, const gchar* prompt, const gchar* initial,
                  EmacsPromptFunc cb, gchar* data) {
    EditorModeState* st = tab->mode_state;
    g_free(st->emacs_prompt_data);
    emacs_open_minibuffer(tab, EMACS_MINI_CALLBACK, prompt, initial);
    st->emacs_prompt_cb = cb;
    st->emacs_prompt_data = data;
    gtk_editable_set_position(GTK_EDITABLE(st->emacs_mb_entry), -1);
}

static void on_emacs_mb_entry_activate(GtkEntry* entry, gpointer user_data) {
    (void)entry;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = tab->mode_state;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(st->emacs_mb_entry));

    switch (st->emacs_mini_mode) {
        case EMACS_MINI_CALLBACK: {
            /* Take the callback before closing: it may open another prompt */
            EmacsPromptFunc cb = st->emacs_prompt_cb;
            gchar* data = st->emacs_prompt_data;
            gchar* answer = g_strdup(text);
            st->emacs_prompt_cb = NULL;
            st->emacs_prompt_data = NULL;
            emacs_close_minibuffer(tab);
            if (cb != NULL) cb(tab, answer, data);
            g_free(answer);
            g_free(data);
            break;
        }
        case EMACS_MINI_ISEARCH_FWD:
        case EMACS_MINI_ISEARCH_BWD:
            emacs_isearch_exit(tab, TRUE);
            break;
        case EMACS_MINI_WRITE_FILE: {
            if (text && text[0] != '\0') {
                editor_save_tab_as(tab->app, tab, text);
            }
            emacs_close_minibuffer(tab);
            break;
        }
        case EMACS_MINI_GOTO_LINE: {
            gint line = atoi(text);
            editor_mode_goto_line(tab, line);
            emacs_close_minibuffer(tab);
            break;
        }
        case EMACS_MINI_QUERY_REPLACE_FIND: {
            g_free(st->emacs_qr_find);
            st->emacs_qr_find = g_strdup(text);
            emacs_open_minibuffer(tab, EMACS_MINI_QUERY_REPLACE_REP, "Query replace with: ", "");
            break;
        }
        case EMACS_MINI_QUERY_REPLACE_REP: {
            g_free(st->emacs_qr_rep);
            st->emacs_qr_rep = g_strdup(text);
            gint count = editor_mode_replace(tab, st->emacs_qr_find, st->emacs_qr_rep, TRUE);
            gchar* msg = g_strdup_printf("Replaced %d occurrence(s)", count);
            emacs_echo(tab, msg);
            g_free(msg);
            emacs_close_minibuffer(tab);
            break;
        }
        case EMACS_MINI_MX: {
            gchar* cmd = g_strstrip(g_ascii_strdown(text, -1));
            if (g_strcmp0(cmd, "dunnet") == 0) {
                AppData* app = tab->app;
                emacs_close_minibuffer(tab);
                editor_dunnet_open(app);
            } else if (g_strcmp0(cmd, "org-mode") == 0 || g_strcmp0(cmd, "text-mode") == 0) {
                /* Major modes: Org-mode, or plain Emacs editing even for a .org file */
                gboolean org = (cmd[0] == 'o');
                tab->text_mode = !org;
                editor_set_tab_mode(tab->app, tab, org ? EDITOR_MODE_ORGMODE : EDITOR_MODE_EMACS);
                orgmode_editor_reset_visibility(tab);
                emacs_echo(tab, org ? "Org-mode enabled" : "Text mode enabled");
            } else if (g_strcmp0(cmd, "imenu-list") == 0 || g_strcmp0(cmd, "imenu-list-smart-toggle") == 0) {
                /* imenu-list: the tagbar */
                emacs_close_minibuffer(tab);
                editor_tagbar_toggle(tab->app, tab, editor_mode_message);
            } else if (strstr(cmd, "goto-line")) {
                emacs_open_minibuffer(tab, EMACS_MINI_GOTO_LINE, "Goto line: ", "");
            } else if (strstr(cmd, "query-replace")) {
                emacs_open_minibuffer(tab, EMACS_MINI_QUERY_REPLACE_FIND, "Query replace: ", "");
            } else if (strstr(cmd, "fill-paragraph")) {
                editor_mode_format_paragraph(tab, 72);
                emacs_echo(tab, "Filled paragraph");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "save-buffer")) {
                editor_save_current(tab->app);
                emacs_echo(tab, "Wrote buffer");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "write-file")) {
                emacs_open_minibuffer(tab, EMACS_MINI_WRITE_FILE, "Write file: ", tab->path ? tab->path : "");
            } else if (strstr(cmd, "set-justification-center")) {
                editor_mode_align_text(tab, GTK_JUSTIFY_CENTER, 80);
                emacs_echo(tab, "Center justified");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "set-justification-left")) {
                editor_mode_align_text(tab, GTK_JUSTIFY_LEFT, 80);
                emacs_echo(tab, "Left justified");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "set-justification-right")) {
                editor_mode_align_text(tab, GTK_JUSTIFY_RIGHT, 80);
                emacs_echo(tab, "Right justified");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "set-justification-full")) {
                editor_mode_align_text(tab, GTK_JUSTIFY_FILL, 80);
                emacs_echo(tab, "Full justified");
                emacs_close_minibuffer(tab);
            } else if (g_str_has_prefix(cmd, "org-") && orgmode_editor_is_org(tab)) {
                gchar* name = g_strstrip(g_strdup(cmd));
                emacs_close_minibuffer(tab);
                if (!orgmode_editor_command(tab, name)) {
                    gchar* msg = g_strdup_printf("[Undefined command: %s]", name);
                    emacs_echo(tab, msg);
                    g_free(msg);
                }
                g_free(name);
            } else if (strstr(cmd, "visual-line-mode") || strstr(cmd, "toggle-truncate-lines")) {
                gboolean wrap = editor_mode_toggle_wrap(tab);
                emacs_echo(tab, wrap ? "Visual-Line mode enabled" : "Visual-Line mode disabled");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "line-numbers")) {
                /* display-line-numbers-mode */
                editor_toggle_line_numbers(tab->app, tab);
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "undo")) {
                gtk_text_buffer_undo(tab->buffer);
                emacs_echo(tab, "Undo!");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "redo")) {
                gtk_text_buffer_redo(tab->buffer);
                emacs_echo(tab, "Redo!");
                emacs_close_minibuffer(tab);
            } else {
                gchar* msg = g_strdup_printf("[Undefined command: %s]", text);
                emacs_echo(tab, msg);
                g_free(msg);
                emacs_close_minibuffer(tab);
            }
            g_free(cmd);
            break;
        }
        default:
            emacs_close_minibuffer(tab);
            break;
    }
}

static void on_emacs_mb_entry_changed(GtkEditable* editable, gpointer user_data) {
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = tab->mode_state;
    (void)editable;
    if (is_isearch(st)) {
        /* A typed character: one search step */
        if (!st->emacs_is_setting && st->emacs_is_steps != NULL) {
            const gchar* prev = g_object_get_data(G_OBJECT(st->emacs_mb_entry), "isearch-query");
            EmacsIsearchStep step = {g_strdup(prev != NULL ? prev : ""), st->emacs_is_ms, st->emacs_is_me,
                                     st->emacs_is_has, st->emacs_is_failing, st->emacs_is_wrapped,
                                     st->emacs_is_forward};
            g_array_append_val(st->emacs_is_steps, step);
            isearch_query_changed(tab, st);
        }
        return;
    }
    if (st->emacs_mini_mode != EMACS_MINI_NONE) {
        gtk_widget_set_visible(st->emacs_mb_echo, FALSE); /* typing hides the completion list */
    }
}

/* Show a hint (completions, "[No match]") beside the minibuffer entry */
static void emacs_minibuffer_hint(EditorModeState* st, const gchar* hint) {
    gtk_label_set_text(GTK_LABEL(st->emacs_mb_echo), hint);
    gtk_widget_set_visible(st->emacs_mb_echo, TRUE);
}

/* Tab in the minibuffer: complete to the longest common prefix of the
 * candidates and list them when several remain */
static void emacs_complete(EditorModeState* st) {
    if (st->emacs_completions == NULL || st->emacs_completions->len == 0) {
        emacs_minibuffer_hint(st, "[No completions]");
        return;
    }
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(st->emacs_mb_entry));
    gsize start = 0;
    if (st->emacs_complete_word) {
        for (gsize i = 0; text[i] != '\0'; i++) {
            if (text[i] == ':' || text[i] == ' ') start = i + 1;
        }
    }
    const gchar* prefix = text + start;
    GPtrArray* matches = g_ptr_array_new();
    for (guint i = 0; i < st->emacs_completions->len; i++) {
        const gchar* c = g_ptr_array_index(st->emacs_completions, i);
        if (g_str_has_prefix(c, prefix)) g_ptr_array_add(matches, (gpointer)c);
    }
    if (matches->len == 0) {
        g_ptr_array_unref(matches);
        emacs_minibuffer_hint(st, "[No match]");
        return;
    }
    /* Longest common prefix of the matches */
    const gchar* first = g_ptr_array_index(matches, 0);
    gsize common = strlen(first);
    for (guint i = 1; i < matches->len; i++) {
        const gchar* c = g_ptr_array_index(matches, i);
        gsize k = 0;
        while (k < common && c[k] == first[k]) k++;
        common = k;
    }
    while (common > 0 && ((guchar)first[common] & 0xC0) == 0x80) common--;
    gchar* head = g_strndup(text, start);
    gchar* completed = g_strdup_printf("%s%.*s", head, (gint)common, first);
    gboolean grew = strlen(completed) > strlen(text);
    if (grew) {
        gtk_editable_set_text(GTK_EDITABLE(st->emacs_mb_entry), completed);
        gtk_editable_set_position(GTK_EDITABLE(st->emacs_mb_entry), -1);
    }
    if (matches->len == 1) {
        emacs_minibuffer_hint(st, "[Sole completion]");
    } else {
        GString* list = g_string_new("{");
        for (guint i = 0; i < matches->len && i < 30; i++) {
            if (i > 0) g_string_append(list, " | ");
            g_string_append(list, g_ptr_array_index(matches, i));
        }
        if (matches->len > 30) g_string_append(list, " | ...");
        g_string_append_c(list, '}');
        emacs_minibuffer_hint(st, list->str);
        g_string_free(list, TRUE);
    }
    g_free(head);
    g_free(completed);
    g_ptr_array_unref(matches);
}

void emacs_isearch_exit(EditorTab* tab, gboolean accept) {
    EditorModeState* st = tab->mode_state;
    if (!is_isearch(st)) return;
    const gchar* q = isearch_query(st);
    if (accept) {
        if (q[0] != '\0') {
            g_free(st->emacs_last_search);
            st->emacs_last_search = g_strdup(q);
        }
        /* The mark stays where the search started (C-x C-x / C-u C-SPC) */
        GtkTextIter origin, cur;
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &origin, st->emacs_is_origin);
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
        gboolean moved = !gtk_text_iter_equal(&origin, &cur);
        if (moved) {
            if (st->emacs_mark == NULL) {
                st->emacs_mark = gtk_text_buffer_create_mark(tab->buffer, "emacs_mark", &origin, TRUE);
            } else {
                gtk_text_buffer_move_mark(tab->buffer, st->emacs_mark, &origin);
            }
        }
        emacs_close_minibuffer(tab);
        emacs_echo(tab, moved ? "Mark saved where search started" : "");
    } else {
        GtkTextIter origin;
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &origin, st->emacs_is_origin);
        gtk_text_buffer_place_cursor(tab->buffer, &origin);
        emacs_close_minibuffer(tab);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer));
        emacs_echo(tab, "Quit");
    }
}

/* Keys while searching. Editing keys of the entry stay; C-s / C-r repeat,
 * DEL undoes a step, C-w adds a word, RET accepts, C-g cancels, and any
 * other command key ends the search and runs. */
static gboolean isearch_key(EditorTab* tab, EditorModeState* st, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    guint k = gdk_keyval_to_lower(keyval);
    if ((keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) || keyval == GDK_KEY_ISO_Level3_Shift) {
        return FALSE;
    }
    if (ctrl && k == GDK_KEY_s) {
        isearch_repeat(tab, st, TRUE);
        return TRUE;
    }
    if (ctrl && k == GDK_KEY_r) {
        isearch_repeat(tab, st, FALSE);
        return TRUE;
    }
    if (ctrl && k == GDK_KEY_w) {
        isearch_yank_word(tab, st);
        return TRUE;
    }
    if ((ctrl && k == GDK_KEY_g) || keyval == GDK_KEY_Escape) {
        /* C-g first drops the part that fails, then cancels */
        if (st->emacs_is_failing && isearch_query(st)[0] != '\0') {
            while (st->emacs_is_steps->len > 0 && st->emacs_is_failing) isearch_backspace(tab, st);
            if (!st->emacs_is_failing) return TRUE;
        }
        emacs_isearch_exit(tab, FALSE);
        return TRUE;
    }
    if (keyval == GDK_KEY_BackSpace && !ctrl && !alt) {
        isearch_backspace(tab, st);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        emacs_isearch_exit(tab, TRUE);
        return TRUE;
    }
    gboolean motion = keyval == GDK_KEY_Left || keyval == GDK_KEY_Right || keyval == GDK_KEY_Up ||
                      keyval == GDK_KEY_Down || keyval == GDK_KEY_Home || keyval == GDK_KEY_End ||
                      keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_Page_Down ||
                      keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab;
    if (ctrl || alt || motion) {
        /* Leave the search at the match, then run the key in the buffer */
        emacs_isearch_exit(tab, TRUE);
        if (!emacs_handle_key(tab, st, keyval, state) && motion) {
            static const struct { guint key; GtkMovementStep step; gint count; } MOVES[] = {
                {GDK_KEY_Left, GTK_MOVEMENT_VISUAL_POSITIONS, -1},
                {GDK_KEY_Right, GTK_MOVEMENT_VISUAL_POSITIONS, 1},
                {GDK_KEY_Up, GTK_MOVEMENT_DISPLAY_LINES, -1},
                {GDK_KEY_Down, GTK_MOVEMENT_DISPLAY_LINES, 1},
                {GDK_KEY_Home, GTK_MOVEMENT_DISPLAY_LINE_ENDS, -1},
                {GDK_KEY_End, GTK_MOVEMENT_DISPLAY_LINE_ENDS, 1},
                {GDK_KEY_Page_Up, GTK_MOVEMENT_PAGES, -1},
                {GDK_KEY_Page_Down, GTK_MOVEMENT_PAGES, 1},
            };
            for (guint i = 0; i < G_N_ELEMENTS(MOVES); i++) {
                if (MOVES[i].key == keyval) {
                    g_signal_emit_by_name(tab->view, "move-cursor", MOVES[i].step, MOVES[i].count, FALSE);
                }
            }
        }
        return TRUE;
    }
    return FALSE; /* a character: the entry inserts it (one search step) */
}

/* Keys of the minibuffer entry: Tab completes (GTK would move the focus
 * away), C-g / Escape quit */
static gboolean on_emacs_mb_key_press(GtkEventControllerKey* controller, guint keyval,
                                      guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    EditorTab* tab = data;
    EditorModeState* st = tab->mode_state;
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    if (is_isearch(st)) return isearch_key(tab, st, keyval, state);
    if (keyval == GDK_KEY_Escape || (ctrl && (keyval == GDK_KEY_g || keyval == GDK_KEY_G))) {
        emacs_close_minibuffer(tab);
        emacs_echo(tab, "Quit");
        return TRUE;
    }
    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_KP_Tab) {
        if (!ctrl && !(state & GDK_ALT_MASK)) emacs_complete(st);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Vim Visual Clone Commandline & Helpers                                    */
/* ------------------------------------------------------------------------- */

/* Build the emacs bars (stored in st) below the editor view */
void emacs_build_ui(EditorTab* tab, EditorModeState* st) {
    GtkWidget* emacs_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Modeline */
    GtkWidget* modeline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(modeline, "emacs-modeline");

    st->emacs_ml_status = gtk_label_new("-U:--- ");
    gtk_widget_add_css_class(st->emacs_ml_status, "emacs-modeline-status");

    st->emacs_ml_bufname = gtk_label_new(tab->name ? tab->name : "*scratch*");
    gtk_widget_add_css_class(st->emacs_ml_bufname, "emacs-modeline-bufname");

    st->emacs_ml_modes = gtk_label_new("  (Text Fill)  ");
    gtk_widget_add_css_class(st->emacs_ml_modes, "emacs-modeline-mode");

    st->emacs_ml_pos = gtk_label_new("Top");
    gtk_widget_add_css_class(st->emacs_ml_pos, "emacs-modeline-pos");

    st->emacs_ml_linecol = gtk_label_new("L1 C1");
    gtk_widget_add_css_class(st->emacs_ml_linecol, "emacs-modeline-pos");

    st->emacs_ml_vc = gtk_label_new("");
    gtk_widget_add_css_class(st->emacs_ml_vc, "emacs-modeline-pos");
    editor_bar_label_shrinkable(st->emacs_ml_vc, PANGO_ELLIPSIZE_END);
    editor_bar_label_shrinkable(st->emacs_ml_modes, PANGO_ELLIPSIZE_END);

    GtkWidget* ml_dash = gtk_label_new("--------------------------------------------------------------------------------");
    gtk_widget_set_hexpand(ml_dash, TRUE);
    gtk_label_set_xalign(GTK_LABEL(ml_dash), 0.0f);
    gtk_widget_add_css_class(ml_dash, "emacs-modeline-pos");

    editor_bar_label_shrinkable(st->emacs_ml_bufname, PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_status);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_bufname);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_pos);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_linecol);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_vc);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_modes);
    gtk_box_append(GTK_BOX(modeline), editor_bar_clip(ml_dash));
    st->emacs_modeline = modeline;

    /* Minibuffer */
    /* No spacing: the typed text follows the prompt as in Emacs */
    GtkWidget* minibuffer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(minibuffer, "emacs-minibuffer");

    st->emacs_mb_prompt = gtk_label_new("");
    gtk_widget_add_css_class(st->emacs_mb_prompt, "emacs-minibuffer-prompt");
    gtk_widget_set_visible(st->emacs_mb_prompt, FALSE);

    st->emacs_mb_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->emacs_mb_entry, "emacs-minibuffer-entry");
    gtk_widget_add_css_class(st->emacs_mb_entry, "flat");
    gtk_widget_set_margin_end(st->emacs_mb_entry, 8); /* before the match count or completions */
    gtk_widget_set_hexpand(st->emacs_mb_entry, TRUE);
    gtk_widget_set_visible(st->emacs_mb_entry, FALSE);

    st->emacs_mb_echo = gtk_label_new("Emacs mode ready");
    editor_bar_label_shrinkable(st->emacs_mb_echo, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->emacs_mb_echo), 0.0f);
    gtk_widget_set_hexpand(st->emacs_mb_echo, TRUE);
    gtk_widget_add_css_class(st->emacs_mb_echo, "emacs-minibuffer-echo");

    g_signal_connect(st->emacs_mb_entry, "activate", G_CALLBACK(on_emacs_mb_entry_activate), tab);
    g_signal_connect(st->emacs_mb_entry, "changed", G_CALLBACK(on_emacs_mb_entry_changed), tab);
    GtkEventController* mb_keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(mb_keys, GTK_PHASE_CAPTURE);
    g_signal_connect(mb_keys, "key-pressed", G_CALLBACK(on_emacs_mb_key_press), tab);
    gtk_widget_add_controller(st->emacs_mb_entry, mb_keys);

    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_prompt);
    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_entry);
    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_echo);
    st->emacs_minibuffer = minibuffer;

    gtk_box_append(GTK_BOX(emacs_box), st->emacs_modeline);
    gtk_box_append(GTK_BOX(emacs_box), st->emacs_minibuffer);
    st->emacs_box = emacs_box;
}

/* Move point; while the mark is active the region between mark and point
 * is the selection, so only the insert mark moves. */
static void emacs_move_point(EditorTab* tab, EditorModeState* st, const GtkTextIter* it) {
    if (st->emacs_mark_active && st->emacs_mark != NULL) {
        GtkTextIter mark;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &mark, st->emacs_mark);
        gtk_text_buffer_select_range(tab->buffer, it, &mark);
    } else {
        gtk_text_buffer_place_cursor(tab->buffer, it);
    }
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer));
}

/* Deactivate the mark and drop the visible region, keeping point. */
static void emacs_deactivate_mark(EditorTab* tab, EditorModeState* st) {
    st->emacs_mark_active = FALSE;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    gtk_text_buffer_place_cursor(tab->buffer, &it);
}

/* C-l (recenter-top-bottom): put the cursor line in the middle of the
 * window, then at the top, then at the bottom on repeated presses */
static void emacs_recenter(EditorTab* tab, EditorModeState* st) {
    GtkTextView* view = GTK_TEXT_VIEW(tab->view);
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
    if (adj == NULL) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    gint y = 0, h = 0;
    gtk_text_view_get_line_yrange(view, &it, &y, &h);
    GdkRectangle vis;
    gtk_text_view_get_visible_rect(view, &vis);
    gint top;
    switch (st->emacs_recenter_step) {
        case 1: top = y; break;
        case 2: top = y + h - vis.height; break;
        default: top = y + h / 2 - vis.height / 2; break;
    }
    /* The adjustment clamps at both ends of the buffer, like Emacs at its start */
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + (top - vis.y));
    st->emacs_recenter_step = (st->emacs_recenter_step + 1) % 3;
}

gboolean emacs_handle_key(EditorTab* tab, EditorModeState* st,
                                 guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    /* Ctrl / Alt / Shift pressed alone: a prefix (C-x, M-g) waits for the
     * real key (C-x, release Ctrl, then C-s) */
    if ((keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) ||
        keyval == GDK_KEY_ISO_Level3_Shift) {
        return FALSE;
    }

    /* Handle Emacs minibuffer cancel / quit */
    if (ctrl && (keyval == GDK_KEY_g || keyval == GDK_KEY_G)) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        if (st->emacs_mark_active) emacs_deactivate_mark(tab, st);
        emacs_echo(tab, "Quit");
        emacs_close_minibuffer(tab);
        return TRUE;
    }

    /* 1. Emacs Prefix C-x */
    if (ctrl && (keyval == GDK_KEY_x || keyval == GDK_KEY_X)) {
        st->emacs_prefix = EMACS_PREFIX_CTRL_X;
        emacs_echo(tab, "C-x-");
        return TRUE;
    }

    /* 2. Emacs Prefix M-g */
    if (alt && (keyval == GDK_KEY_g || keyval == GDK_KEY_G)) {
        st->emacs_prefix = EMACS_PREFIX_META_G;
        emacs_echo(tab, "M-g-");
        return TRUE;
    }

    /* 2b. Emacs Prefix C-h (help) */
    if (ctrl && !alt && (keyval == GDK_KEY_h || keyval == GDK_KEY_H) && st->emacs_prefix == EMACS_PREFIX_NONE) {
        st->emacs_prefix = EMACS_PREFIX_CTRL_H;
        emacs_echo(tab, "C-h-");
        return TRUE;
    }
    if (st->emacs_prefix == EMACS_PREFIX_CTRL_H) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        if (keyval == GDK_KEY_period) {
            /* display-local-help: what the identifier at point is */
            code_help_show(tab->app, tab);
        } else {
            emacs_echo(tab, "C-h: no help command for this key (C-h . describes the symbol at point)");
        }
        return TRUE;
    }

    /* Resolve C-x combinations */
    if (st->emacs_prefix == EMACS_PREFIX_CTRL_X) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        if (ctrl && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
            editor_save_current(tab->app);
            emacs_echo(tab, "Wrote buffer");
            return TRUE;
        }
        if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
            emacs_open_minibuffer(tab, EMACS_MINI_WRITE_FILE, "Write file: ", tab->path ? tab->path : "");
            return TRUE;
        }
        if (keyval == GDK_KEY_u || keyval == GDK_KEY_U) {
            gtk_text_buffer_undo(tab->buffer);
            emacs_echo(tab, "Undo!");
            return TRUE;
        }
        if (keyval == GDK_KEY_h || keyval == GDK_KEY_H) {
            GtkTextIter s, e;
            gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
            gtk_text_buffer_select_range(tab->buffer, &s, &e);
            emacs_echo(tab, "Mark set (buffer)");
            return TRUE;
        }
        if (ctrl && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
            editor_close_tab(tab->app, tab);
            return TRUE;
        }
        /* Windows: split below / to the right (a second view of the file),
         * delete this one, keep only this one (the other views of the file) */
        if (!ctrl && !alt && (keyval == GDK_KEY_2 || keyval == GDK_KEY_3)) {
            emacs_echo(tab, "");
            editor_split_tab(tab->app, tab, keyval == GDK_KEY_3 ? EDITOR_MOVE_RIGHT : EDITOR_MOVE_DOWN);
            return TRUE;
        }
        if (!ctrl && !alt && keyval == GDK_KEY_0) {
            editor_close_tab(tab->app, tab);
            return TRUE;
        }
        if (!ctrl && !alt && keyval == GDK_KEY_1) {
            emacs_echo(tab, "");
            editor_close_other_views(tab->app, tab);
            return TRUE;
        }
        NavAction action = nav_keys_ctrl_x(keyval, state);
        switch (action) {
        case NAV_PANEL_NEXT: /* other-window: the next panel */
            app_emit(tab->app, APP_EVENT_FOCUS_NEXT_PANEL);
            return TRUE;
        case NAV_TAB_NEXT: /* next-buffer / previous-buffer: the tabs of the notebook */
        case NAV_TAB_PREV:
            emacs_echo(tab, "");
            editor_modes_step_tab(tab, action == NAV_TAB_NEXT ? 1 : -1);
            return TRUE;
        case NAV_QUICK_OPEN: /* find-file: a file of the project */
            quick_open_show(tab->app);
            return TRUE;
        default:
            break;
        }
        emacs_echo(tab, "[C-x command complete]");
        return TRUE;
    }

    /* Resolve M-g combinations */
    if (st->emacs_prefix == EMACS_PREFIX_META_G) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        if (keyval == GDK_KEY_g || keyval == GDK_KEY_G) {
            emacs_open_minibuffer(tab, EMACS_MINI_GOTO_LINE, "Goto line: ", "");
            return TRUE;
        }
        if (keyval == GDK_KEY_i || keyval == GDK_KEY_I) {
            /* imenu: the symbols of the buffer */
            code_symbols_show(tab->app, tab, editor_mode_message);
            return TRUE;
        }
        return TRUE;
    }

    /* 2c. *dungeon* buffer (M-x dunnet): RET runs the prompt line, C-a stops
     * after the prompt, Backspace/Delete cannot erase earlier output */
    if (!alt || (keyval != GDK_KEY_x && keyval != GDK_KEY_X)) {
        if (editor_dunnet_is_dungeon(tab) &&
            editor_dunnet_handle_key(tab, keyval, state)) {
            return TRUE;
        }
    }

    /* 3. Navigation */
    /* Plain arrows/Home/End extend the region while the mark is active
     * (GtkTextView would otherwise drop the selection). */
    if (st->emacs_mark_active && !ctrl && !alt && !shift &&
        (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right || keyval == GDK_KEY_Up ||
         keyval == GDK_KEY_Down || keyval == GDK_KEY_Home || keyval == GDK_KEY_End)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        switch (keyval) {
            case GDK_KEY_Left: gtk_text_iter_backward_cursor_position(&it); break;
            case GDK_KEY_Right: gtk_text_iter_forward_cursor_position(&it); break;
            case GDK_KEY_Up: gtk_text_iter_backward_line(&it); break;
            case GDK_KEY_Down: gtk_text_iter_forward_line(&it); break;
            case GDK_KEY_Home: gtk_text_iter_set_line_offset(&it, 0); break;
            default:
                if (!gtk_text_iter_ends_line(&it)) gtk_text_iter_forward_to_line_end(&it);
                break;
        }
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_cursor_position(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_cursor_position(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_line(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_line(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_word_end(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_word_start(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_a || keyval == GDK_KEY_A)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_e || keyval == GDK_KEY_E)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        if (!gtk_text_iter_ends_line(&it)) gtk_text_iter_forward_to_line_end(&it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    /* xref: M-. definition (C-u M-. declaration), M-, back, C-M-, forward */
    if (alt && keyval == GDK_KEY_period) {
        code_nav_goto_definition(tab->app, tab, st->emacs_universal_arg, editor_mode_message);
        return TRUE;
    }
    if (alt && keyval == GDK_KEY_question) {
        /* xref-find-references */
        code_refs_find(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (alt && keyval == GDK_KEY_comma) {
        if (ctrl) code_nav_forward(tab->app, tab, editor_mode_message);
        else code_nav_back(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (alt && keyval == GDK_KEY_less) {
        GtkTextIter it;
        gtk_text_buffer_get_start_iter(tab->buffer, &it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (alt && keyval == GDK_KEY_greater) {
        GtkTextIter it;
        gtk_text_buffer_get_end_iter(tab->buffer, &it);
        emacs_move_point(tab, st, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        /* Page down */
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) {
            gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj));
        }
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        /* Page up */
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) {
            gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) - gtk_adjustment_get_page_size(adj));
        }
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        emacs_recenter(tab, st);
        return TRUE;
    }

    /* 4. Edit & Selection: Mark, Cut, Copy, Paste, Delete */
    if (ctrl && (keyval == GDK_KEY_space || keyval == GDK_KEY_at)) {
        /* C-SPC right after C-SPC toggles the mark without moving it */
        if (st->emacs_last_set_mark && st->emacs_mark != NULL) {
            if (st->emacs_mark_active) {
                emacs_deactivate_mark(tab, st);
                emacs_echo(tab, "Mark deactivated");
            } else {
                GtkTextIter it;
                gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
                st->emacs_mark_active = TRUE;
                emacs_move_point(tab, st, &it);
                emacs_echo(tab, "Mark activated");
            }
            return TRUE;
        }
        st->emacs_last_set_mark = TRUE;
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        if (st->emacs_mark == NULL) {
            st->emacs_mark = gtk_text_buffer_create_mark(tab->buffer, "emacs_mark", &it, TRUE);
        } else {
            gtk_text_buffer_move_mark(tab->buffer, st->emacs_mark, &it);
        }
        st->emacs_mark_active = TRUE;
        emacs_move_point(tab, st, &it);
        emacs_echo(tab, "Mark set");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        /* Cut (Kill-region) */
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gtk_text_buffer_cut_clipboard(tab->buffer, clip, TRUE);
        st->emacs_mark_active = FALSE;
        emacs_echo(tab, "Kill region");
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        /* Copy (Kill-ring-save) */
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gtk_text_buffer_copy_clipboard(tab->buffer, clip);
        emacs_deactivate_mark(tab, st);
        emacs_echo(tab, "Kill ring save");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) {
        /* Paste (Yank) at point, not over the region */
        if (st->emacs_mark_active) emacs_deactivate_mark(tab, st);
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gtk_text_buffer_paste_clipboard(tab->buffer, clip, NULL, TRUE);
        emacs_echo(tab, "Yank");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        /* Kill line */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        if (gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_char(&end);
        } else {
            gtk_text_iter_forward_to_line_end(&end);
        }
        gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gdk_clipboard_set_text(clip, text);
        g_free(text);
        gtk_text_buffer_delete(tab->buffer, &start, &end);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        /* Delete char */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        if (!gtk_text_iter_is_end(&end)) {
            gtk_text_iter_forward_cursor_position(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
        }
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        /* Kill word forward */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        gtk_text_iter_forward_word_end(&end);
        gtk_text_buffer_delete(tab->buffer, &start, &end);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete)) {
        /* Backward kill word */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &end, gtk_text_buffer_get_insert(tab->buffer));
        start = end;
        gtk_text_iter_backward_word_start(&start);
        gtk_text_buffer_delete(tab->buffer, &start, &end);
        return TRUE;
    }

    /* 5. Undo & Redo */
    if (ctrl && (keyval == GDK_KEY_slash || keyval == GDK_KEY_underscore)) {
        gtk_text_buffer_undo(tab->buffer);
        emacs_echo(tab, "Undo!");
        return TRUE;
    }
    if ((ctrl && shift && keyval == GDK_KEY_underscore) || (alt && keyval == GDK_KEY_underscore)) {
        gtk_text_buffer_redo(tab->buffer);
        emacs_echo(tab, "Redo!");
        return TRUE;
    }

    /* 6. Find & Replace (I-search / Query Replace) */
    if (ctrl && (keyval == GDK_KEY_s || keyval == GDK_KEY_S || keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        gboolean fwd = keyval == GDK_KEY_s || keyval == GDK_KEY_S;
        emacs_open_minibuffer(tab, fwd ? EMACS_MINI_ISEARCH_FWD : EMACS_MINI_ISEARCH_BWD,
                              fwd ? "I-search: " : "I-search backward: ", "");
        isearch_start(tab, st, fwd);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_percent || (shift && keyval == GDK_KEY_5))) {
        emacs_open_minibuffer(tab, EMACS_MINI_QUERY_REPLACE_FIND, "Query replace: ", "");
        return TRUE;
    }

    /* 7. Format Text */
    if (alt && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        editor_mode_format_paragraph(tab, 72);
        emacs_echo(tab, "fill-paragraph (72 cols)");
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_u || keyval == GDK_KEY_U)) {
        editor_mode_change_case(tab, CASE_UPPER);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        editor_mode_change_case(tab, CASE_LOWER);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        editor_mode_change_case(tab, CASE_CAPITALIZE);
        return TRUE;
    }

    /* 8. M-x Command Prompt */
    if (alt && (keyval == GDK_KEY_x || keyval == GDK_KEY_X)) {
        emacs_open_minibuffer(tab, EMACS_MINI_MX, "M-x ", "");
        return TRUE;
    }

    return FALSE;
}
