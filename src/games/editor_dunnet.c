/* Emacs *dungeon* buffer integration for Dunnet (M-x dunnet). */
#include "games/editor_dunnet.h"
#include "games/dunnet.h"
#include "editor/editor.h"
#include "widgets/diff_gutter.h"
#include "widgets/color_column.h"
#include <string.h>

#define DUNNET_BUF_KEY "dunnet-buffer-state"
#define DUNNET_BUFFER_NAME "*dungeon*"

typedef struct {
    DunnetState* game;
    GtkTextMark* prompt_mark; /* left-gravity mark right after the active prompt */
    GPtrArray* history;       /* gchar* command history */
    gint hist_pos;
} DunnetBuffer;

static void dunnet_buffer_free(gpointer data) {
    DunnetBuffer* db = (DunnetBuffer*)data;
    if (db == NULL) return;
    dunnet_free(db->game);
    if (db->history != NULL) g_ptr_array_unref(db->history);
    g_free(db);
}

static DunnetBuffer* dunnet_buffer_get(const EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return NULL;
    return (DunnetBuffer*)g_object_get_data(G_OBJECT(tab->buffer), DUNNET_BUF_KEY);
}

static void dunnet_buffer_start(EditorTab* tab, DunnetBuffer* db) {
    GtkTextBuffer* buf = tab->buffer;
    dunnet_reset(db->game);
    gchar* opening = dunnet_opening_text(db->game);
    const gchar* prompt = dunnet_prompt(db->game);
    gchar* initial = g_strdup_printf("%s\n%s", opening, prompt);
    gtk_text_buffer_set_text(buf, initial, -1);
    g_free(initial);
    g_free(opening);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    if (db->prompt_mark == NULL) {
        db->prompt_mark = gtk_text_buffer_create_mark(buf, "dunnet_prompt", &end, TRUE);
    } else {
        gtk_text_buffer_move_mark(buf, db->prompt_mark, &end);
    }
    gtk_text_buffer_place_cursor(buf, &end);
    gtk_text_buffer_set_modified(buf, FALSE);
    if (tab->view != NULL) {
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view),
                                     gtk_text_buffer_get_insert(buf),
                                     0.0, FALSE, 0.0, 1.0);
    }
}

EditorTab* editor_dunnet_open(AppData* app) {
    if (app == NULL) return NULL;
    EditorTab* tab = editor_open_buffer(app, DUNNET_BUFFER_NAME, EDITOR_MODE_EMACS);
    if (tab == NULL || tab->buffer == NULL) return NULL;

    if (tab->view != NULL) {
        diff_gutter_set_line_numbers(tab->view, -1);
        color_column_set(tab->view, 0);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tab->view), GTK_WRAP_WORD_CHAR);
    }
    if (tab->mode != EDITOR_MODE_EMACS) {
        editor_set_tab_mode(app, tab, EDITOR_MODE_EMACS);
    }

    DunnetBuffer* db = dunnet_buffer_get(tab);
    if (db == NULL) {
        db = g_new0(DunnetBuffer, 1);
        db->game = dunnet_new();
        db->history = g_ptr_array_new_with_free_func(g_free);
        g_object_set_data_full(G_OBJECT(tab->buffer), DUNNET_BUF_KEY, db, dunnet_buffer_free);
        dunnet_buffer_start(tab, db);
    } else if (dunnet_is_over(db->game)) {
        dunnet_buffer_start(tab, db);
    }

    editor_modes_update_status(tab);
    editor_mode_message(tab, "Dunnet by Ron Schnell (type 'help' for commands)");
    if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    return tab;
}

gboolean editor_dunnet_is_dungeon(const EditorTab* tab) {
    return dunnet_buffer_get(tab) != NULL;
}

static void replace_input(GtkTextBuffer* buf, DunnetBuffer* db, const gchar* text) {
    GtkTextIter p_it, end_it;
    gtk_text_buffer_get_iter_at_mark(buf, &p_it, db->prompt_mark);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_delete(buf, &p_it, &end_it);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_insert(buf, &end_it, text, -1);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_place_cursor(buf, &end_it);
    gtk_text_buffer_set_modified(buf, FALSE);
}

static void submit_line(EditorTab* tab, DunnetBuffer* db) {
    GtkTextBuffer* buf = tab->buffer;
    GtkTextIter p_it, end_it;
    gtk_text_buffer_get_iter_at_mark(buf, &p_it, db->prompt_mark);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gchar* cmd = gtk_text_buffer_get_text(buf, &p_it, &end_it, FALSE);

    if (dunnet_is_password_prompt(db->game)) {
        /* Do not leave passwords visible in the buffer transcript */
        gtk_text_buffer_delete(buf, &p_it, &end_it);
    } else if (cmd[0] != '\0') {
        if (db->history->len == 0 ||
            g_strcmp0(g_ptr_array_index(db->history, db->history->len - 1), cmd) != 0) {
            g_ptr_array_add(db->history, g_strdup(cmd));
        }
        db->hist_pos = (gint)db->history->len;
    }

    gchar* reply = dunnet_command(db->game, cmd);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_insert(buf, &end_it, "\n", 1);
    if (reply != NULL && reply[0] != '\0') {
        gtk_text_buffer_get_end_iter(buf, &end_it);
        gtk_text_buffer_insert(buf, &end_it, reply, -1);
        gtk_text_buffer_get_end_iter(buf, &end_it);
        gtk_text_buffer_insert(buf, &end_it, "\n", 1);
    }
    const gchar* prompt = dunnet_prompt(db->game);
    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_insert(buf, &end_it, prompt, -1);

    gtk_text_buffer_get_end_iter(buf, &end_it);
    gtk_text_buffer_move_mark(buf, db->prompt_mark, &end_it);
    gtk_text_buffer_place_cursor(buf, &end_it);
    gtk_text_buffer_set_modified(buf, FALSE);

    if (tab->view != NULL) {
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view),
                                     gtk_text_buffer_get_insert(buf),
                                     0.0, FALSE, 0.0, 1.0);
    }
    g_free(reply);
    g_free(cmd);
}

gboolean editor_dunnet_handle_key(EditorTab* tab, guint keyval, GdkModifierType state) {
    DunnetBuffer* db = dunnet_buffer_get(tab);
    if (db == NULL || db->prompt_mark == NULL) return FALSE;

    GtkTextBuffer* buf = tab->buffer;
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    guint k = gdk_keyval_to_lower(keyval);

    /* RET executes the command on the prompt line */
    if (!ctrl && !alt &&
        (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter)) {
        submit_line(tab, db);
        return TRUE;
    }

    GtkTextIter p_it, ins_it, sel_s, sel_e;
    gtk_text_buffer_get_iter_at_mark(buf, &p_it, db->prompt_mark);
    gtk_text_buffer_get_iter_at_mark(buf, &ins_it, gtk_text_buffer_get_insert(buf));
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(buf, &sel_s, &sel_e);
    gint prompt_off = gtk_text_iter_get_offset(&p_it);
    gint ins_off = gtk_text_iter_get_offset(&ins_it);
    gboolean on_prompt_line = (gtk_text_iter_get_line(&ins_it) == gtk_text_iter_get_line(&p_it));

    /* C-a / Home on the prompt line stops right after the prompt */
    if ((ctrl && !alt && k == GDK_KEY_a) || (!ctrl && !alt && keyval == GDK_KEY_Home)) {
        if (on_prompt_line) {
            gtk_text_buffer_place_cursor(buf, &p_it);
            return TRUE;
        }
        return FALSE;
    }

    /* Command history: M-p / M-n, or Up / Down when on the prompt line */
    if ((alt && !ctrl && (k == GDK_KEY_p || k == GDK_KEY_n)) ||
        (!ctrl && !alt && !has_sel && on_prompt_line &&
         (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down))) {
        gboolean prev = (k == GDK_KEY_p || keyval == GDK_KEY_Up);
        if (prev) {
            if (db->history->len > 0 && db->hist_pos > 0) {
                db->hist_pos--;
                replace_input(buf, db, g_ptr_array_index(db->history, db->hist_pos));
            }
        } else {
            if (db->hist_pos < (gint)db->history->len - 1) {
                db->hist_pos++;
                replace_input(buf, db, g_ptr_array_index(db->history, db->hist_pos));
            } else if (db->hist_pos == (gint)db->history->len - 1) {
                db->hist_pos++;
                replace_input(buf, db, "");
            }
        }
        return TRUE;
    }

    /* Protect the prompt and earlier output from Backspace / Delete / Kill */
    if (keyval == GDK_KEY_BackSpace) {
        if ((has_sel && gtk_text_iter_get_offset(&sel_s) < prompt_off) ||
            (!has_sel && ins_off <= prompt_off)) {
            return TRUE;
        }
        return FALSE;
    }
    if (keyval == GDK_KEY_Delete || (ctrl && !alt && k == GDK_KEY_d)) {
        if ((has_sel && gtk_text_iter_get_offset(&sel_s) < prompt_off) ||
            (!has_sel && ins_off < prompt_off)) {
            return TRUE;
        }
        return FALSE;
    }
    if ((ctrl && !alt && (k == GDK_KEY_k || k == GDK_KEY_w)) ||
        (alt && !ctrl && k == GDK_KEY_d)) {
        if ((has_sel && gtk_text_iter_get_offset(&sel_s) < prompt_off) ||
            (!has_sel && ins_off < prompt_off)) {
            return TRUE;
        }
        return FALSE;
    }

    /* Yank (C-y) or printable typing while point is in the transcript jumps to the prompt */
    gunichar ch = gdk_keyval_to_unicode(keyval);
    if ((ctrl && !alt && k == GDK_KEY_y) || (!ctrl && !alt && ch >= 0x20 && ch != 0x7f)) {
        if (ins_off < prompt_off || (has_sel && gtk_text_iter_get_offset(&sel_s) < prompt_off)) {
            GtkTextIter end_it;
            gtk_text_buffer_get_end_iter(buf, &end_it);
            gtk_text_buffer_place_cursor(buf, &end_it);
        }
    }

    return FALSE;
}
