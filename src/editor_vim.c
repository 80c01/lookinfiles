/* Vim editor mode (visual clone): statusline, command line, modal key handling. */
#include "editor_modes_private.h"
#include "editor.h"
#include <ctype.h>
#include <string.h>

void vim_echo(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st->vim_cl_echo != NULL) {
        gtk_label_set_text(GTK_LABEL(st->vim_cl_echo), msg ? msg : "");
    }
}

void vim_enter_submode(EditorTab* tab, VimSubMode submode) {
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    st->vim_submode = submode;
    st->vim_pending_op = VIM_OP_NONE;
    st->vim_count = 0;

    /* Visual clone cursor styling: block cursor in NORMAL/VISUAL, bar in INSERT */
    if (submode == VIM_SUB_INSERT) {
        gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
    } else {
        gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), TRUE);
    }

    /* Submode UI display */
    gtk_widget_remove_css_class(st->vim_sl_mode, "vim-mode-normal");
    gtk_widget_remove_css_class(st->vim_sl_mode, "vim-mode-insert");
    gtk_widget_remove_css_class(st->vim_sl_mode, "vim-mode-visual");
    gtk_widget_remove_css_class(st->vim_sl_mode, "vim-mode-command");

    switch (submode) {
        case VIM_SUB_NORMAL:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "NORMAL");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-normal");
            gtk_widget_set_visible(st->vim_cl_prompt, FALSE);
            gtk_widget_set_visible(st->vim_cl_entry, FALSE);
            gtk_widget_set_visible(st->vim_cl_echo, TRUE);
            if (tab->view) gtk_widget_grab_focus(tab->view);
            break;
        case VIM_SUB_INSERT:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "-- INSERT --");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-insert");
            gtk_widget_set_visible(st->vim_cl_prompt, FALSE);
            gtk_widget_set_visible(st->vim_cl_entry, FALSE);
            gtk_widget_set_visible(st->vim_cl_echo, TRUE);
            if (tab->view) gtk_widget_grab_focus(tab->view);
            break;
        case VIM_SUB_VISUAL:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "-- VISUAL --");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-visual");
            gtk_widget_set_visible(st->vim_cl_prompt, FALSE);
            gtk_widget_set_visible(st->vim_cl_entry, FALSE);
            gtk_widget_set_visible(st->vim_cl_echo, TRUE);
            if (tab->view) gtk_widget_grab_focus(tab->view);
            break;
        case VIM_SUB_VISUAL_LINE:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "-- VISUAL LINE --");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-visual");
            gtk_widget_set_visible(st->vim_cl_prompt, FALSE);
            gtk_widget_set_visible(st->vim_cl_entry, FALSE);
            gtk_widget_set_visible(st->vim_cl_echo, TRUE);
            if (tab->view) gtk_widget_grab_focus(tab->view);
            break;
        case VIM_SUB_COMMAND:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "COMMAND");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-command");
            gtk_label_set_text(GTK_LABEL(st->vim_cl_prompt), ":");
            gtk_editable_set_text(GTK_EDITABLE(st->vim_cl_entry), "");
            gtk_widget_set_visible(st->vim_cl_prompt, TRUE);
            gtk_widget_set_visible(st->vim_cl_entry, TRUE);
            gtk_widget_set_visible(st->vim_cl_echo, FALSE);
            gtk_widget_grab_focus(st->vim_cl_entry);
            break;
        case VIM_SUB_SEARCH_FWD:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "SEARCH");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-command");
            gtk_label_set_text(GTK_LABEL(st->vim_cl_prompt), "/");
            gtk_editable_set_text(GTK_EDITABLE(st->vim_cl_entry), "");
            gtk_widget_set_visible(st->vim_cl_prompt, TRUE);
            gtk_widget_set_visible(st->vim_cl_entry, TRUE);
            gtk_widget_set_visible(st->vim_cl_echo, FALSE);
            gtk_widget_grab_focus(st->vim_cl_entry);
            break;
        case VIM_SUB_SEARCH_BWD:
            gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), "SEARCH");
            gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-command");
            gtk_label_set_text(GTK_LABEL(st->vim_cl_prompt), "?");
            gtk_editable_set_text(GTK_EDITABLE(st->vim_cl_entry), "");
            gtk_widget_set_visible(st->vim_cl_prompt, TRUE);
            gtk_widget_set_visible(st->vim_cl_entry, TRUE);
            gtk_widget_set_visible(st->vim_cl_echo, FALSE);
            gtk_widget_grab_focus(st->vim_cl_entry);
            break;
    }
    editor_modes_update_status(tab);
}

static void on_vim_cl_entry_activate(GtkEntry* entry, gpointer user_data) {
    (void)entry;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* raw = gtk_editable_get_text(GTK_EDITABLE(st->vim_cl_entry));

    if (st->vim_submode == VIM_SUB_SEARCH_FWD || st->vim_submode == VIM_SUB_SEARCH_BWD) {
        if (raw && raw[0] != '\0') {
            g_free(st->vim_last_search);
            st->vim_last_search = g_strdup(raw);
            st->vim_search_fwd = (st->vim_submode == VIM_SUB_SEARCH_FWD);
            if (!editor_mode_find_next(tab, st->vim_last_search, st->vim_search_fwd, TRUE)) {
                gchar* msg = g_strdup_printf("Pattern not found: %s", st->vim_last_search);
                vim_echo(tab, msg);
                g_free(msg);
            }
        }
        vim_enter_submode(tab, VIM_SUB_NORMAL);
        return;
    }

    /* Ex-command mode (:) */
    gchar* cmd = g_strdup(raw ? raw : "");
    g_strstrip(cmd);

    if (g_strcmp0(cmd, "w") == 0 || g_strcmp0(cmd, "write") == 0) {
        editor_save_current((AppData*)tab->app);
    } else if (g_str_has_prefix(cmd, "w ") || g_str_has_prefix(cmd, "write ") || g_str_has_prefix(cmd, "saveas ")) {
        const gchar* path = strchr(cmd, ' ');
        while (path && *path == ' ') path++;
        if (path && *path != '\0') {
            editor_save_tab_as((AppData*)tab->app, tab, path);
        }
    } else if (g_strcmp0(cmd, "wq") == 0 || g_strcmp0(cmd, "x") == 0) {
        editor_save_current((AppData*)tab->app);
        editor_close_tab((AppData*)tab->app, tab);
    } else if (g_strcmp0(cmd, "q") == 0) {
        editor_close_tab((AppData*)tab->app, tab);
    } else if (g_strcmp0(cmd, "q!") == 0) {
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
        editor_close_tab((AppData*)tab->app, tab);
    } else if (isdigit(cmd[0])) {
        editor_mode_goto_line(tab, atoi(cmd));
    } else if (strstr(cmd, "s/") || strstr(cmd, "%s/")) {
        /* Search and replace: e.g. %s/foo/bar/g or s/foo/bar/ */
        const gchar* p = strstr(cmd, "s/");
        p += 2;
        const gchar* sep1 = strchr(p, '/');
        if (sep1) {
            gchar* old_str = g_strndup(p, sep1 - p);
            const gchar* p2 = sep1 + 1;
            const gchar* sep2 = strchr(p2, '/');
            gchar* new_str = sep2 ? g_strndup(p2, sep2 - p2) : g_strdup(p2);
            gboolean global = sep2 && (strchr(sep2, 'g') != NULL);

            gint count = editor_mode_replace(tab, old_str, new_str, global);
            gchar* msg = g_strdup_printf("%d substitution(s)", count);
            vim_echo(tab, msg);
            g_free(msg);
            g_free(old_str);
            g_free(new_str);
        }
    } else if (g_str_has_prefix(cmd, "center")) {
        gint width = 80;
        const gchar* sp = strchr(cmd, ' ');
        if (sp) width = atoi(sp + 1);
        if (width <= 0) width = 80;
        editor_mode_align_text(tab, GTK_JUSTIFY_CENTER, width);
        vim_echo(tab, "Centered lines");
    } else if (g_str_has_prefix(cmd, "right")) {
        gint width = 80;
        const gchar* sp = strchr(cmd, ' ');
        if (sp) width = atoi(sp + 1);
        if (width <= 0) width = 80;
        editor_mode_align_text(tab, GTK_JUSTIFY_RIGHT, width);
        vim_echo(tab, "Right aligned lines");
    } else if (g_strcmp0(cmd, "left") == 0) {
        editor_mode_align_text(tab, GTK_JUSTIFY_LEFT, 80);
        vim_echo(tab, "Left aligned lines");
    } else if (g_str_has_prefix(cmd, "set mode=")) {
        const gchar* m = cmd + 9;
        if (g_ascii_strcasecmp(m, "vim") == 0) {
            editor_set_tab_mode((AppData*)tab->app, tab, EDITOR_MODE_VIM);
        } else if (g_ascii_strcasecmp(m, "emacs") == 0) {
            editor_set_tab_mode((AppData*)tab->app, tab, EDITOR_MODE_EMACS);
        } else if (g_ascii_strcasecmp(m, "notepad") == 0) {
            editor_set_tab_mode((AppData*)tab->app, tab, EDITOR_MODE_NOTEPAD);
        }
    } else {
        gchar* msg = g_strdup_printf("Not an editor command: %s", cmd);
        vim_echo(tab, msg);
        g_free(msg);
    }

    g_free(cmd);
    vim_enter_submode(tab, VIM_SUB_NORMAL);
}

/* ------------------------------------------------------------------------- */
/* Status Update & UI Synchronization                                        */
/* ------------------------------------------------------------------------- */

/* Build the vim bars (stored in st) below the editor view */
void vim_build_ui(EditorTab* tab, EditorModeState* st) {
    GtkWidget* vim_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Vim statusline */
    GtkWidget* statusline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(statusline, "vim-statusline");

    st->vim_sl_mode = gtk_label_new("NORMAL");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-badge");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-normal");

    st->vim_sl_file = gtk_label_new(tab->name ? tab->name : "[No Name]");
    editor_bar_label_shrinkable(st->vim_sl_file, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(st->vim_sl_file), 0.0f);
    gtk_widget_set_hexpand(st->vim_sl_file, TRUE);

    st->vim_sl_ruler = gtk_label_new("1,1   All");
    gtk_label_set_xalign(GTK_LABEL(st->vim_sl_ruler), 1.0f);

    gtk_box_append(GTK_BOX(statusline), st->vim_sl_mode);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_file);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_ruler);
    st->vim_statusline = statusline;

    /* Vim commandline / echo */
    GtkWidget* commandline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(commandline, "vim-commandline");

    st->vim_cl_prompt = gtk_label_new(":");
    gtk_widget_add_css_class(st->vim_cl_prompt, "vim-commandline-prompt");
    gtk_widget_set_visible(st->vim_cl_prompt, FALSE);

    st->vim_cl_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->vim_cl_entry, "vim-commandline-entry");
    gtk_widget_set_hexpand(st->vim_cl_entry, TRUE);
    gtk_widget_set_visible(st->vim_cl_entry, FALSE);

    st->vim_cl_echo = gtk_label_new("");
    editor_bar_label_shrinkable(st->vim_cl_echo, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->vim_cl_echo), 0.0f);
    gtk_widget_set_hexpand(st->vim_cl_echo, TRUE);

    g_signal_connect(st->vim_cl_entry, "activate", G_CALLBACK(on_vim_cl_entry_activate), tab);

    gtk_box_append(GTK_BOX(commandline), st->vim_cl_prompt);
    gtk_box_append(GTK_BOX(commandline), st->vim_cl_entry);
    gtk_box_append(GTK_BOX(commandline), st->vim_cl_echo);
    st->vim_commandline = commandline;

    gtk_box_append(GTK_BOX(vim_box), st->vim_statusline);
    gtk_box_append(GTK_BOX(vim_box), st->vim_commandline);
    st->vim_box = vim_box;
}

gboolean vim_handle_key(EditorTab* tab, EditorModeState* st,
                               guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    /* INSERT mode handling */
    if (st->vim_submode == VIM_SUB_INSERT) {
        if (keyval == GDK_KEY_Escape || (ctrl && keyval == GDK_KEY_bracketleft)) {
            /* Move cursor 1 char left on leaving insert mode */
            GtkTextIter it;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
            if (!gtk_text_iter_starts_line(&it)) {
                gtk_text_iter_backward_cursor_position(&it);
                gtk_text_buffer_place_cursor(tab->buffer, &it);
            }
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
            /* Delete backward word */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &end, gtk_text_buffer_get_insert(tab->buffer));
            start = end;
            gtk_text_iter_backward_word_start(&start);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        if (ctrl && (keyval == GDK_KEY_u || keyval == GDK_KEY_U)) {
            /* Delete backward line */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &end, gtk_text_buffer_get_insert(tab->buffer));
            start = end;
            gtk_text_iter_set_line_offset(&start, 0);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        return FALSE; /* Standard typing */
    }

    /* VISUAL / VISUAL LINE mode handling */
    if (st->vim_submode == VIM_SUB_VISUAL || st->vim_submode == VIM_SUB_VISUAL_LINE) {
        if (keyval == GDK_KEY_Escape || (ctrl && keyval == GDK_KEY_bracketleft) ||
            keyval == GDK_KEY_v || keyval == GDK_KEY_V) {
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_d || keyval == GDK_KEY_x) {
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gtk_text_buffer_cut_clipboard(tab->buffer, clip, TRUE);
            vim_echo(tab, "Deleted visual selection");
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gtk_text_buffer_copy_clipboard(tab->buffer, clip);
            vim_echo(tab, "Yanked visual selection");
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_c) {
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gtk_text_buffer_cut_clipboard(tab->buffer, clip, TRUE);
            vim_enter_submode(tab, VIM_SUB_INSERT);
            return TRUE;
        }
        if (keyval == GDK_KEY_greater) {
            editor_mode_indent_selection(tab, FALSE);
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_less) {
            editor_mode_indent_selection(tab, TRUE);
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_asciitilde) {
            editor_mode_change_case(tab, CASE_TOGGLE);
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_u) {
            editor_mode_change_case(tab, CASE_LOWER);
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
        if (keyval == GDK_KEY_U) {
            editor_mode_change_case(tab, CASE_UPPER);
            vim_enter_submode(tab, VIM_SUB_NORMAL);
            return TRUE;
        }
    }

    /* NORMAL mode handling */
    /* Check count accumulation */
    if (st->vim_count == 0 && keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        st->vim_count = (keyval - GDK_KEY_0);
        return TRUE;
    } else if (st->vim_count > 0 && keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        st->vim_count = st->vim_count * 10 + (keyval - GDK_KEY_0);
        return TRUE;
    }

    /* Pending 'r' (replace single character) */
    if (st->vim_pending_op == VIM_OP_REPLACE_CHAR) {
        st->vim_pending_op = VIM_OP_NONE;
        gunichar uc = gdk_keyval_to_unicode(keyval);
        if (uc != 0) {
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            end = start;
            if (!gtk_text_iter_ends_line(&end)) {
                gtk_text_iter_forward_cursor_position(&end);
                gtk_text_buffer_delete(tab->buffer, &start, &end);
                gchar utf8_buf[8] = {0};
                g_unichar_to_utf8(uc, utf8_buf);
                gtk_text_buffer_insert(tab->buffer, &start, utf8_buf, -1);
            }
        }
        return TRUE;
    }

    /* Pending 'g' commands */
    if (st->vim_pending_op == VIM_OP_G) {
        st->vim_pending_op = VIM_OP_NONE;
        if (keyval == GDK_KEY_g) {
            /* gg: top of document or line count */
            if (st->vim_count > 0) {
                editor_mode_goto_line(tab, st->vim_count);
            } else {
                editor_mode_goto_line(tab, 1);
            }
            st->vim_count = 0;
            return TRUE;
        }
        if (keyval == GDK_KEY_q) {
            editor_mode_format_paragraph(tab, 80);
            vim_echo(tab, "Formatted lines");
            return TRUE;
        }
        return TRUE;
    }

    /* Pending 'z' commands */
    if (st->vim_pending_op == VIM_OP_Z) {
        st->vim_pending_op = VIM_OP_NONE;
        if (keyval == GDK_KEY_z) {
            gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view),
                                         gtk_text_buffer_get_insert(tab->buffer),
                                         0.0, TRUE, 0.0, 0.5);
            return TRUE;
        }
        if (keyval == GDK_KEY_Z) {
            /* ZZ: save and quit */
            editor_save_current((AppData*)tab->app);
            editor_close_tab((AppData*)tab->app, tab);
            return TRUE;
        }
        if (keyval == GDK_KEY_Q) {
            /* ZQ: quit without save */
            gtk_text_buffer_set_modified(tab->buffer, FALSE);
            editor_close_tab((AppData*)tab->app, tab);
            return TRUE;
        }
        return TRUE;
    }

    /* Pending 'd' (delete) operator */
    if (st->vim_pending_op == VIM_OP_DELETE) {
        st->vim_pending_op = VIM_OP_NONE;
        if (keyval == GDK_KEY_d) {
            /* dd: delete line */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            gtk_text_iter_set_line_offset(&start, 0);
            end = start;
            gint lines = st->vim_count > 0 ? st->vim_count : 1;
            for (gint i = 0; i < lines; i++) {
                if (!gtk_text_iter_forward_line(&end)) {
                    gtk_text_iter_forward_to_line_end(&end);
                    break;
                }
            }
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gdk_clipboard_set_text(clip, text);
            g_free(text);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            st->vim_count = 0;
            return TRUE;
        }
        if (keyval == GDK_KEY_w) {
            /* dw: delete word */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            end = start;
            gtk_text_iter_forward_word_end(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        if (keyval == GDK_KEY_dollar) {
            /* d$: delete to line end */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            end = start;
            gtk_text_iter_forward_to_line_end(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        if (keyval == GDK_KEY_0) {
            /* d0: delete to line start */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &end, gtk_text_buffer_get_insert(tab->buffer));
            start = end;
            gtk_text_iter_set_line_offset(&start, 0);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        if (keyval == GDK_KEY_G) {
            /* dG: delete to document end */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            gtk_text_buffer_get_end_iter(tab->buffer, &end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            return TRUE;
        }
        return TRUE;
    }

    /* Pending 'y' (yank) operator */
    if (st->vim_pending_op == VIM_OP_YANK) {
        st->vim_pending_op = VIM_OP_NONE;
        if (keyval == GDK_KEY_y) {
            /* yy: yank line */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            gtk_text_iter_set_line_offset(&start, 0);
            end = start;
            if (!gtk_text_iter_forward_line(&end)) {
                gtk_text_iter_forward_to_line_end(&end);
            }
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gdk_clipboard_set_text(clip, text);
            g_free(text);
            vim_echo(tab, "1 line yanked");
            return TRUE;
        }
        if (keyval == GDK_KEY_w) {
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            end = start;
            gtk_text_iter_forward_word_end(&end);
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gdk_clipboard_set_text(clip, text);
            g_free(text);
            vim_echo(tab, "1 word yanked");
            return TRUE;
        }
        return TRUE;
    }

    /* Pending 'c' (change) operator */
    if (st->vim_pending_op == VIM_OP_CHANGE) {
        st->vim_pending_op = VIM_OP_NONE;
        if (keyval == GDK_KEY_c) {
            /* cc: change line */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            gtk_text_iter_set_line_offset(&start, 0);
            end = start;
            gtk_text_iter_forward_to_line_end(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            vim_enter_submode(tab, VIM_SUB_INSERT);
            return TRUE;
        }
        if (keyval == GDK_KEY_w) {
            /* cw: change word */
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
            end = start;
            gtk_text_iter_forward_word_end(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            vim_enter_submode(tab, VIM_SUB_INSERT);
            return TRUE;
        }
        return TRUE;
    }

    /* NORMAL mode core commands */
    /* Switching to Insert */
    if (keyval == GDK_KEY_i) {
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_I) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        while (!gtk_text_iter_ends_line(&it) && g_unichar_isspace(gtk_text_iter_get_char(&it))) {
            gtk_text_iter_forward_char(&it);
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_a) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        if (!gtk_text_iter_ends_line(&it)) {
            gtk_text_iter_forward_cursor_position(&it);
            gtk_text_buffer_place_cursor(tab->buffer, &it);
        }
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_A) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_to_line_end(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_o) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_to_line_end(&it);
        gtk_text_buffer_insert(tab->buffer, &it, "\n", 1);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_O) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        gtk_text_buffer_insert(tab->buffer, &it, "\n", 1);
        gtk_text_iter_backward_line(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_s) {
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        if (!gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_cursor_position(&end);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
        }
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_S) {
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&start, 0);
        end = start;
        gtk_text_iter_forward_to_line_end(&end);
        gtk_text_buffer_delete(tab->buffer, &start, &end);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }
    if (keyval == GDK_KEY_C) {
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        gtk_text_iter_forward_to_line_end(&end);
        gtk_text_buffer_delete(tab->buffer, &start, &end);
        vim_enter_submode(tab, VIM_SUB_INSERT);
        return TRUE;
    }

    /* Switching to Visual */
    if (keyval == GDK_KEY_v) {
        vim_enter_submode(tab, VIM_SUB_VISUAL);
        return TRUE;
    }
    if (keyval == GDK_KEY_V) {
        vim_enter_submode(tab, VIM_SUB_VISUAL_LINE);
        return TRUE;
    }

    /* Switching to Command / Search */
    if (keyval == GDK_KEY_colon) {
        vim_enter_submode(tab, VIM_SUB_COMMAND);
        return TRUE;
    }
    if (keyval == GDK_KEY_slash) {
        vim_enter_submode(tab, VIM_SUB_SEARCH_FWD);
        return TRUE;
    }
    if (keyval == GDK_KEY_question) {
        vim_enter_submode(tab, VIM_SUB_SEARCH_BWD);
        return TRUE;
    }

    /* Basic Navigation: h, j, k, l */
    if (keyval == GDK_KEY_h) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gint count = st->vim_count > 0 ? st->vim_count : 1;
        for (gint i = 0; i < count; i++) {
            if (gtk_text_iter_starts_line(&it)) break;
            gtk_text_iter_backward_cursor_position(&it);
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        st->vim_count = 0;
        return TRUE;
    }
    if (keyval == GDK_KEY_l) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gint count = st->vim_count > 0 ? st->vim_count : 1;
        for (gint i = 0; i < count; i++) {
            if (gtk_text_iter_ends_line(&it)) break;
            gtk_text_iter_forward_cursor_position(&it);
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        st->vim_count = 0;
        return TRUE;
    }
    if (keyval == GDK_KEY_j) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gint count = st->vim_count > 0 ? st->vim_count : 1;
        for (gint i = 0; i < count; i++) {
            if (!gtk_text_iter_forward_line(&it)) break;
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        st->vim_count = 0;
        return TRUE;
    }
    if (keyval == GDK_KEY_k) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gint count = st->vim_count > 0 ? st->vim_count : 1;
        for (gint i = 0; i < count; i++) {
            if (!gtk_text_iter_backward_line(&it)) break;
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        st->vim_count = 0;
        return TRUE;
    }
    if (keyval == GDK_KEY_w) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_word_end(&it);
        gtk_text_iter_forward_char(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_b) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_backward_word_start(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_e) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_word_end(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_0) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_asciicircum) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&it, 0);
        while (!gtk_text_iter_ends_line(&it) && g_unichar_isspace(gtk_text_iter_get_char(&it))) {
            gtk_text_iter_forward_char(&it);
        }
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_dollar) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_forward_to_line_end(&it);
        gtk_text_buffer_place_cursor(tab->buffer, &it);
        return TRUE;
    }
    if (keyval == GDK_KEY_G) {
        if (st->vim_count > 0) {
            editor_mode_goto_line(tab, st->vim_count);
        } else {
            GtkTextIter it;
            gtk_text_buffer_get_end_iter(tab->buffer, &it);
            gtk_text_buffer_place_cursor(tab->buffer, &it);
        }
        st->vim_count = 0;
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj));
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) - gtk_adjustment_get_page_size(adj));
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + (gtk_adjustment_get_page_size(adj) / 2));
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_u || keyval == GDK_KEY_U)) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
        if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) - (gtk_adjustment_get_page_size(adj) / 2));
        return TRUE;
    }

    /* Operators: d, y, c, g, z, r */
    if (keyval == GDK_KEY_d) {
        st->vim_pending_op = VIM_OP_DELETE;
        return TRUE;
    }
    if (keyval == GDK_KEY_y) {
        st->vim_pending_op = VIM_OP_YANK;
        return TRUE;
    }
    if (keyval == GDK_KEY_Y) {
        /* Y: yank line */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&start, 0);
        end = start;
        if (!gtk_text_iter_forward_line(&end)) gtk_text_iter_forward_to_line_end(&end);
        gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gdk_clipboard_set_text(clip, text);
        g_free(text);
        vim_echo(tab, "1 line yanked");
        return TRUE;
    }
    if (keyval == GDK_KEY_c) {
        st->vim_pending_op = VIM_OP_CHANGE;
        return TRUE;
    }
    if (keyval == GDK_KEY_g) {
        st->vim_pending_op = VIM_OP_G;
        return TRUE;
    }
    if (keyval == GDK_KEY_z) {
        st->vim_pending_op = VIM_OP_Z;
        return TRUE;
    }
    if (keyval == GDK_KEY_Z) {
        st->vim_pending_op = VIM_OP_Z;
        return TRUE;
    }
    if (keyval == GDK_KEY_r) {
        st->vim_pending_op = VIM_OP_REPLACE_CHAR;
        return TRUE;
    }

    /* Delete single char: x, X */
    if (keyval == GDK_KEY_x) {
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        if (!gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_cursor_position(&end);
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
            GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
            gdk_clipboard_set_text(clip, text);
            g_free(text);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_X) {
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &end, gtk_text_buffer_get_insert(tab->buffer));
        start = end;
        if (!gtk_text_iter_starts_line(&start)) {
            gtk_text_iter_backward_cursor_position(&start);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
        }
        return TRUE;
    }

    /* Paste / Put: p, P */
    if (keyval == GDK_KEY_p) {
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gtk_text_buffer_paste_clipboard(tab->buffer, clip, NULL, TRUE);
        return TRUE;
    }
    if (keyval == GDK_KEY_P) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
        gtk_text_buffer_paste_clipboard(tab->buffer, clip, &it, TRUE);
        return TRUE;
    }

    /* Undo & Redo */
    if (keyval == GDK_KEY_u) {
        gtk_text_buffer_undo(tab->buffer);
        vim_echo(tab, "1 change; before #1");
        return TRUE;
    }
    if (ctrl && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        gtk_text_buffer_redo(tab->buffer);
        vim_echo(tab, "1 change; after #1");
        return TRUE;
    }

    /* Search repeat: n, N, *, # */
    if (keyval == GDK_KEY_n) {
        if (st->vim_last_search) {
            editor_mode_find_next(tab, st->vim_last_search, st->vim_search_fwd, TRUE);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_N) {
        if (st->vim_last_search) {
            editor_mode_find_next(tab, st->vim_last_search, !st->vim_search_fwd, TRUE);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_asterisk || (shift && keyval == GDK_KEY_8)) {
        /* Search forward word under cursor */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        if (!gtk_text_iter_starts_word(&start)) gtk_text_iter_backward_word_start(&start);
        end = start;
        gtk_text_iter_forward_word_end(&end);
        gchar* word = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
        if (word && word[0] != '\0') {
            g_free(st->vim_last_search);
            st->vim_last_search = word;
            st->vim_search_fwd = TRUE;
            editor_mode_find_next(tab, st->vim_last_search, TRUE, TRUE);
        } else {
            g_free(word);
        }
        return TRUE;
    }

    /* Format & Alignment */
    if (keyval == GDK_KEY_equal) {
        /* Auto-indent line */
        editor_mode_indent_selection(tab, FALSE);
        return TRUE;
    }
    if (keyval == GDK_KEY_asciitilde) {
        /* Toggle case of char */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start, gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        if (!gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_cursor_position(&end);
            gchar* ch = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
            gunichar c = g_utf8_get_char(ch);
            g_free(ch);
            if (g_unichar_islower(c)) c = g_unichar_toupper(c);
            else if (g_unichar_isupper(c)) c = g_unichar_tolower(c);
            gchar out[8] = {0};
            g_unichar_to_utf8(c, out);
            gtk_text_buffer_delete(tab->buffer, &start, &end);
            gtk_text_buffer_insert(tab->buffer, &start, out, -1);
        }
        return TRUE;
    }

    return FALSE;
}
