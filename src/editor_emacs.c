/* Emacs editor mode (visual clone): modeline, minibuffer, prefix keys and commands. */
#include "editor_modes_private.h"
#include "editor.h"
#include "orgmode.h"
#include <ctype.h>
#include <string.h>

void emacs_echo(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st->emacs_mb_echo != NULL) {
        gtk_label_set_text(GTK_LABEL(st->emacs_mb_echo), msg ? msg : "");
    }
}

void emacs_close_minibuffer(EditorTab* tab) {
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    st->emacs_mini_mode = EMACS_MINI_NONE;
    st->emacs_prefix = EMACS_PREFIX_NONE;
    gtk_widget_set_visible(st->emacs_mb_prompt, FALSE);
    gtk_widget_set_visible(st->emacs_mb_entry, FALSE);
    gtk_widget_set_visible(st->emacs_mb_echo, TRUE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

static void emacs_open_minibuffer(EditorTab* tab, EmacsMiniMode mode,
                                  const gchar* prompt, const gchar* default_val) {
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    st->emacs_mini_mode = mode;
    gtk_label_set_text(GTK_LABEL(st->emacs_mb_prompt), prompt);
    gtk_editable_set_text(GTK_EDITABLE(st->emacs_mb_entry), default_val ? default_val : "");
    gtk_widget_set_visible(st->emacs_mb_prompt, TRUE);
    gtk_widget_set_visible(st->emacs_mb_entry, TRUE);
    gtk_widget_set_visible(st->emacs_mb_echo, FALSE);
    gtk_widget_grab_focus(st->emacs_mb_entry);
}

static void on_emacs_mb_entry_activate(GtkEntry* entry, gpointer user_data) {
    (void)entry;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(st->emacs_mb_entry));

    switch (st->emacs_mini_mode) {
        case EMACS_MINI_ISEARCH_FWD:
        case EMACS_MINI_ISEARCH_BWD: {
            g_free(st->emacs_last_search);
            st->emacs_last_search = g_strdup(text);
            emacs_echo(tab, "Search finished");
            emacs_close_minibuffer(tab);
            break;
        }
        case EMACS_MINI_WRITE_FILE: {
            if (text && text[0] != '\0') {
                editor_save_tab_as((AppData*)tab->app, tab, text);
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
            gchar* cmd = g_ascii_strdown(text, -1);
            if (strstr(cmd, "goto-line")) {
                emacs_open_minibuffer(tab, EMACS_MINI_GOTO_LINE, "Goto line: ", "");
            } else if (strstr(cmd, "query-replace")) {
                emacs_open_minibuffer(tab, EMACS_MINI_QUERY_REPLACE_FIND, "Query replace: ", "");
            } else if (strstr(cmd, "fill-paragraph")) {
                editor_mode_format_paragraph(tab, 72);
                emacs_echo(tab, "Filled paragraph");
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "save-buffer")) {
                editor_save_current((AppData*)tab->app);
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
            } else if (strstr(cmd, "org-cycle") || strstr(cmd, "fold")) {
                orgmode_editor_toggle_heading_fold(tab);
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "org-global-cycle") || strstr(cmd, "global-fold")) {
                orgmode_editor_cycle_global_fold(tab);
                emacs_close_minibuffer(tab);
            } else if (strstr(cmd, "vim-mode")) {
                editor_set_tab_mode((AppData*)tab->app, tab, EDITOR_MODE_VIM);
            } else if (strstr(cmd, "notepad-mode")) {
                editor_set_tab_mode((AppData*)tab->app, tab, EDITOR_MODE_NOTEPAD);
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
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st->emacs_mini_mode == EMACS_MINI_ISEARCH_FWD || st->emacs_mini_mode == EMACS_MINI_ISEARCH_BWD) {
        const gchar* q = gtk_editable_get_text(editable);
        if (q && q[0] != '\0') {
            gboolean fwd = (st->emacs_mini_mode == EMACS_MINI_ISEARCH_FWD);
            editor_mode_find_next(tab, q, fwd, TRUE);
        }
    }
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

    GtkWidget* ml_dash = gtk_label_new("--------------------------------------------------------------------------------");
    gtk_widget_set_hexpand(ml_dash, TRUE);
    gtk_label_set_xalign(GTK_LABEL(ml_dash), 0.0f);
    gtk_widget_add_css_class(ml_dash, "emacs-modeline-pos");

    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_status);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_bufname);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_modes);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_pos);
    gtk_box_append(GTK_BOX(modeline), st->emacs_ml_linecol);
    gtk_box_append(GTK_BOX(modeline), ml_dash);
    st->emacs_modeline = modeline;

    /* Minibuffer */
    GtkWidget* minibuffer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(minibuffer, "emacs-minibuffer");

    st->emacs_mb_prompt = gtk_label_new("");
    gtk_widget_add_css_class(st->emacs_mb_prompt, "emacs-minibuffer-prompt");
    gtk_widget_set_visible(st->emacs_mb_prompt, FALSE);

    st->emacs_mb_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->emacs_mb_entry, "emacs-minibuffer-entry");
    gtk_widget_set_hexpand(st->emacs_mb_entry, TRUE);
    gtk_widget_set_visible(st->emacs_mb_entry, FALSE);

    st->emacs_mb_echo = gtk_label_new("Emacs mode ready");
    gtk_label_set_xalign(GTK_LABEL(st->emacs_mb_echo), 0.0f);
    gtk_widget_set_hexpand(st->emacs_mb_echo, TRUE);
    gtk_widget_add_css_class(st->emacs_mb_echo, "emacs-minibuffer-echo");

    g_signal_connect(st->emacs_mb_entry, "activate", G_CALLBACK(on_emacs_mb_entry_activate), tab);
    g_signal_connect(st->emacs_mb_entry, "changed", G_CALLBACK(on_emacs_mb_entry_changed), tab);

    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_prompt);
    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_entry);
    gtk_box_append(GTK_BOX(minibuffer), st->emacs_mb_echo);
    st->emacs_minibuffer = minibuffer;

    gtk_box_append(GTK_BOX(emacs_box), st->emacs_modeline);
    gtk_box_append(GTK_BOX(emacs_box), st->emacs_minibuffer);
    st->emacs_box = emacs_box;
}

gboolean emacs_handle_key(EditorTab* tab, EditorModeState* st,
                                 guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    /* Handle Emacs minibuffer cancel / quit */
    if (ctrl && (keyval == GDK_KEY_g || keyval == GDK_KEY_G)) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        st->emacs_mark_active = FALSE;
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

    /* Resolve C-x combinations */
    if (st->emacs_prefix == EMACS_PREFIX_CTRL_X) {
        st->emacs_prefix = EMACS_PREFIX_NONE;
        if (ctrl && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
            editor_save_current((AppData*)tab->app);
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
            editor_close_tab((AppData*)tab->app, tab);
            return TRUE;
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
        return TRUE;
    }

    /* 3. Navigation */
    if (ctrl && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_cursor_position(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_cursor_position(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_line(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_line(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_word_end(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_word_start(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_a || keyval == GDK_KEY_A)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_e || keyval == GDK_KEY_E)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_to_line_end(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_less || keyval == GDK_KEY_comma)) {
        GtkTextIter it;
        gtk_text_buffer_get_start_iter(tab->buffer, &it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (alt && (keyval == GDK_KEY_greater || keyval == GDK_KEY_period)) {
        GtkTextIter it;
        gtk_text_buffer_get_end_iter(tab->buffer, &it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
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
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view),
                                     gtk_text_buffer_get_insert(tab->buffer),
                                     0.0, TRUE, 0.0, 0.5);
        return TRUE;
    }

    /* 4. Edit & Selection: Mark, Cut, Copy, Paste, Delete */
    if (ctrl && (keyval == GDK_KEY_space || keyval == GDK_KEY_at)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        if (st->emacs_mark == NULL) {
            st->emacs_mark = gtk_text_buffer_create_mark(tab->buffer, "emacs_mark", &it, TRUE);
        } else {
            gtk_text_buffer_move_mark(tab->buffer, st->emacs_mark, &it);
        }
        st->emacs_mark_active = TRUE;
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
        st->emacs_mark_active = FALSE;
        emacs_echo(tab, "Kill ring save");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) {
        /* Paste (Yank) */
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
    if (ctrl && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        emacs_open_minibuffer(tab, EMACS_MINI_ISEARCH_FWD, "I-search: ", "");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        emacs_open_minibuffer(tab, EMACS_MINI_ISEARCH_BWD, "I-search backward: ", "");
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
