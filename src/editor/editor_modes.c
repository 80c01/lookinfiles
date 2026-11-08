#include "editor/editor_modes.h"
#include "core/i18n.h"
#include "core/app_events.h"
#include "theme/text_style.h"
#include "widgets/diff_gutter.h"
#include "code/code_nav.h"
#include "code/code_help.h"
#include "code/code_intel.h"
#include "editor/editor_info.h"
#include "editor/file_save.h"
#include "theme/theme.h"
#include "theme/fonts.h"
#include "focus_route.h"
#include "explorer/nav_keys.h"
#include "theme/icons.h"
#include <ctype.h>
#include <string.h>

#include "editor/editor_modes_private.h"

/* CSS for visual clones */
void editor_modes_load_css(void) {
    static gboolean css_loaded = FALSE;
    if (css_loaded) return;
    css_loaded = TRUE;

    /* The three modes share one bar look (the app's status bar colours and
     * sizes); each keeps its own content: monospace Emacs mode line and Vim
     * status line with a mode badge, Notepad status bar in the UI font. */
    static const gchar* CSS_DATA =
        ".emacs-modeline, .vim-statusline, .notepad-statusbar {\n"
        "  background: @bg_bar;\n"
        "  color: @fg;\n"
        "  border-top: 1px solid @border;\n"
        "  font-size: @size_bar;\n"
        "  padding: 2px 8px;\n"
        "  min-height: 22px;\n"
        "}\n"
        ".emacs-modeline, .vim-statusline, .emacs-minibuffer, .vim-commandline {\n"
        "  font-family: @font_code;\n"
        "}\n"
        ".emacs-minibuffer, .vim-commandline {\n"
        "  background: @bg_view;\n"
        "  color: @fg_text;\n"
        "  font-size: @size_code;\n"
        "  padding: 2px 8px;\n"
        "  min-height: 24px;\n"
        "}\n"
        /* --- Emacs --- */
        ".emacs-modeline-status {\n"
        "  color: @accent_fg;\n"
        "  font-weight: bold;\n"
        "}\n"
        ".emacs-modeline-bufname {\n"
        "  font-weight: bold;\n"
        "  color: @fg_strong;\n"
        "}\n"
        ".emacs-modeline-mode {\n"
        "  color: @syn_variable;\n"
        "}\n"
        ".emacs-modeline-pos {\n"
        "  color: @fg;\n"
        "}\n"
        ".emacs-minibuffer-prompt {\n"
        "  font-weight: bold;\n"
        "  color: @accent_fg;\n"
        "}\n"
        /* The entries are flat (theme.c): typed text follows the prompt */
        ".emacs-minibuffer-entry, .vim-commandline-entry {\n"
        "  color: @fg_text;\n"
        "}\n"
        ".emacs-minibuffer-echo {\n"
        "  color: @fg;\n"
        "}\n"
        /* --- Vim --- */
        ".vim-mode-badge {\n"
        "  font-weight: bold;\n"
        "  padding: 1px 6px;\n"
        "  border-radius: 3px;\n"
        "  margin-right: 6px;\n"
        "  color: @badge_fg;\n"
        "}\n"
        ".vim-mode-normal { background: @badge_blue; }\n"
        ".vim-mode-insert { background: @badge_green; }\n"
        ".vim-mode-visual { background: @badge_orange; }\n"
        ".vim-mode-command { background: @accent; }\n"
        ".vim-mode-replace { background: @badge_red; }\n"
        ".vim-showcmd {\n"
        "  color: @fg_muted;\n"
        "  margin-right: 12px;\n"
        "}\n"
        ".vim-message-error {\n"
        "  color: @error;\n"
        "}\n"
        ".vim-commandline-prompt {\n"
        "  font-weight: bold;\n"
        "  color: @fg_strong;\n"
        "}\n"
        /* --- Notepad --- */
        ".notepad-statusbar {\n"
        "  color: @fg_muted;\n"
        "  font-family: @font_ui;\n"
        "  padding: 3px 12px;\n"
        "}\n"
        ".notepad-bar {\n"
        "  background: @bg_bar;\n"
        "  color: @fg;\n"
        "  border-top: 1px solid @border;\n"
        "  border-bottom: 1px solid @border;\n"
        "  padding: 4px 8px;\n"
        "}\n";
    theme_add_css(CSS_DATA);
}

/* ------------------------------------------------------------------------- */
/* Text Buffer Tag Setup                                                     */
/* ------------------------------------------------------------------------- */

/* Mode bars must never impose a minimum width on the editor tab: a label wider
 * than the panel makes GTK lay the tab out wider than the visible area, which
 * pushes the minimap out of the window. */

/* Let a label shrink with an ellipsis (END for messages, MIDDLE for file names) */
void editor_bar_label_shrinkable(GtkWidget* label, PangoEllipsizeMode mode) {
    gtk_label_set_ellipsize(GTK_LABEL(label), mode);
}

/* Wrap filler content (e.g. Emacs modeline dashes) so it is clipped instead of
 * requesting its full width */
GtkWidget* editor_bar_clip(GtkWidget* child) {
    GtkWidget* clip = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(clip), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(clip), TRUE);
    gtk_widget_set_hexpand(clip, gtk_widget_get_hexpand(child));
    gtk_widget_set_can_target(clip, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip), child);
    return clip;
}

static void init_buffer_tags(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(tab->buffer);
    if (gtk_text_tag_table_lookup(table, "align-left") == NULL) {
        gtk_text_buffer_create_tag(tab->buffer, "align-left",
                                   "justification", GTK_JUSTIFY_LEFT, NULL);
        gtk_text_buffer_create_tag(tab->buffer, "align-center",
                                   "justification", GTK_JUSTIFY_CENTER, NULL);
        gtk_text_buffer_create_tag(tab->buffer, "align-right",
                                   "justification", GTK_JUSTIFY_RIGHT, NULL);
        gtk_text_buffer_create_tag(tab->buffer, "align-fill",
                                   "justification", GTK_JUSTIFY_FILL, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Status Reporting & Helpers                                                */
/* ------------------------------------------------------------------------- */

/* The cursor of tab (the passive view's, the Vim cursor, else the insert mark) */
static void cursor_iter(EditorTab* tab, GtkTextIter* iter_out) {
    GtkTextIter iter;
    if (tab->view_passive && tab->view_insert != NULL) {
        /* Another view of the split file has the buffer's cursor */
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &iter, tab->view_insert);
    } else if (tab->mode == EDITOR_MODE_VIM && tab->mode_state != NULL) {
        vim_cursor_get(tab, &iter); /* not the selection end in visual mode */
    } else {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &iter,
                                         gtk_text_buffer_get_insert(tab->buffer));
    }
    *iter_out = iter;
}

static void get_cursor_position(EditorTab* tab, gint* out_line, gint* out_col, gint* out_percent,
                                gint* out_total) {
    if (out_line) *out_line = 1;
    if (out_col) *out_col = 1;
    if (out_percent) *out_percent = 0;
    if (out_total) *out_total = 1;
    if (tab == NULL || tab->buffer == NULL) return;

    GtkTextIter iter;
    cursor_iter(tab, &iter);
    gint line = gtk_text_iter_get_line(&iter) + 1;
    gint col = gtk_text_iter_get_line_offset(&iter) + 1;
    gint total_lines = gtk_text_buffer_get_line_count(tab->buffer);
    if (total_lines <= 1) total_lines = 1;

    gint percent = (line * 100) / total_lines;
    if (percent > 100) percent = 100;

    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
    if (out_percent) *out_percent = percent;
    if (out_total) *out_total = total_lines;
}

/* The identifier under the cursor, or NULL (free with g_free) */
static gchar* cursor_word(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return NULL;
    GtkTextIter start, end;
    cursor_iter(tab, &start);
    end = start;
    while (!gtk_text_iter_starts_line(&start)) {
        GtkTextIter prev = start;
        gtk_text_iter_backward_char(&prev);
        gunichar c = gtk_text_iter_get_char(&prev);
        if (!g_unichar_isalnum(c) && c != '_') break;
        start = prev;
    }
    while (!gtk_text_iter_ends_line(&end)) {
        gunichar c = gtk_text_iter_get_char(&end);
        if (!g_unichar_isalnum(c) && c != '_') break;
        gtk_text_iter_forward_char(&end);
    }
    if (gtk_text_iter_equal(&start, &end)) return NULL;
    return gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Save As & File Writing                                                    */
/* ------------------------------------------------------------------------- */

gboolean editor_save_tab_as(AppData* app, EditorTab* tab, const gchar* new_path) {
    if (tab == NULL || new_path == NULL || new_path[0] == '\0') {
        return FALSE;
    }

    gchar* full_path = NULL;
    if (g_path_is_absolute(new_path)) {
        full_path = g_strdup(new_path);
    } else {
        const gchar* base_dir = (tab->path != NULL && tab->path[0] != '\0')
                                    ? tab->path
                                    : (app ? app->base_dir : ".");
        gchar* parent = g_path_get_dirname(base_dir);
        full_path = g_build_filename(parent, new_path, NULL);
        g_free(parent);
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
    gchar* lines = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    gchar* text = g_strconcat(lines, tab->final_break, NULL);
    g_free(lines);
    GError* err = NULL;

    if (!file_save_text(full_path, text, -1, &err)) {
        gchar* msg = g_strdup_printf("Cannot save to %s: %s", full_path, err ? err->message : "error");
        if (err) g_error_free(err);
        if (tab->mode == EDITOR_MODE_EMACS) emacs_echo(tab, msg);
        else if (tab->mode == EDITOR_MODE_VIM) vim_echo(tab, msg);
        g_free(msg);
        g_free(text);
        g_free(full_path);
        return FALSE;
    }

    g_free(text);
    g_free(tab->path);
    g_free(tab->name);

    tab->path = full_path;
    tab->name = g_path_get_basename(full_path);
    diff_gutter_set_file(tab->view, tab->path);
    gtk_text_buffer_set_modified(tab->buffer, FALSE);

    editor_tab_label_update(tab);
    if (app != NULL) {
        app_emit(app, APP_EVENT_FILES_CHANGED);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }

    gint line_count = gtk_text_buffer_get_line_count(tab->buffer);
    GtkTextIter b_start, b_end;
    gtk_text_buffer_get_bounds(tab->buffer, &b_start, &b_end);
    gint char_count = gtk_text_iter_get_offset(&b_end);

    if (tab->mode == EDITOR_MODE_EMACS) {
        gchar* msg = g_strdup_printf("Wrote %s", tab->path);
        emacs_echo(tab, msg);
        g_free(msg);
    } else if (tab->mode == EDITOR_MODE_VIM) {
        gchar* msg = g_strdup_printf("\"%s\" %dL, %dB written", tab->name, line_count, char_count);
        vim_echo(tab, msg);
        g_free(msg);
    } else {
        EditorModeState* st = tab->mode_state;
        if (st && st->np_status_label) {
            gchar* msg = g_strdup_printf(_("Saved to %s"), tab->name);
            gtk_label_set_text(GTK_LABEL(st->np_status_label), msg);
            g_free(msg);
        }
    }

    editor_modes_update_status(tab);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Operations: Alignment, Formatting, Case, Indent, Search/Replace           */
/* ------------------------------------------------------------------------- */

void editor_mode_align_text(EditorTab* tab, GtkJustification just, gint target_width) {
    if (tab == NULL || tab->buffer == NULL) return;
    init_buffer_tags(tab);

    GtkTextIter start, end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &start, &end);
    if (!has_sel) {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start,
                                         gtk_text_buffer_get_insert(tab->buffer));
        gtk_text_iter_set_line_offset(&start, 0);
        end = start;
        gtk_text_iter_forward_to_line_end(&end);
    } else {
        gtk_text_iter_set_line_offset(&start, 0);
        if (!gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_to_line_end(&end);
        }
    }

    /* Remove previous alignment tags */
    gtk_text_buffer_remove_tag_by_name(tab->buffer, "align-left", &start, &end);
    gtk_text_buffer_remove_tag_by_name(tab->buffer, "align-center", &start, &end);
    gtk_text_buffer_remove_tag_by_name(tab->buffer, "align-right", &start, &end);
    gtk_text_buffer_remove_tag_by_name(tab->buffer, "align-fill", &start, &end);

    const gchar* tag_name = "align-left";
    if (just == GTK_JUSTIFY_CENTER) tag_name = "align-center";
    else if (just == GTK_JUSTIFY_RIGHT) tag_name = "align-right";
    else if (just == GTK_JUSTIFY_FILL) tag_name = "align-fill";

    gtk_text_buffer_apply_tag_by_name(tab->buffer, tag_name, &start, &end);

    /* Text-level formatting for center & right alignment */
    if (target_width > 0 && (just == GTK_JUSTIFY_CENTER || just == GTK_JUSTIFY_RIGHT || just == GTK_JUSTIFY_LEFT)) {
        gtk_text_buffer_begin_user_action(tab->buffer);
        gint start_line = gtk_text_iter_get_line(&start);
        gint end_line = gtk_text_iter_get_line(&end);

        for (gint l = start_line; l <= end_line; l++) {
            GtkTextIter l_start, l_end;
            gtk_text_buffer_get_iter_at_line(tab->buffer, &l_start, l);
            l_end = l_start;
            gtk_text_iter_forward_to_line_end(&l_end);

            gchar* raw = gtk_text_buffer_get_text(tab->buffer, &l_start, &l_end, FALSE);
            gchar* stripped = g_strstrip(g_strdup(raw));
            g_free(raw);

            gint len = g_utf8_strlen(stripped, -1);
            if (len > 0) {
                GString* line_buf = g_string_new(NULL);
                if (just == GTK_JUSTIFY_CENTER) {
                    gint pad = (target_width > len) ? (target_width - len) / 2 : 0;
                    for (gint i = 0; i < pad; i++) g_string_append_c(line_buf, ' ');
                } else if (just == GTK_JUSTIFY_RIGHT) {
                    gint pad = (target_width > len) ? (target_width - len) : 0;
                    for (gint i = 0; i < pad; i++) g_string_append_c(line_buf, ' ');
                }
                g_string_append(line_buf, stripped);
                gtk_text_buffer_delete(tab->buffer, &l_start, &l_end);
                gtk_text_buffer_insert(tab->buffer, &l_start, line_buf->str, line_buf->len);
                g_string_free(line_buf, TRUE);
            }
            g_free(stripped);
        }
        gtk_text_buffer_end_user_action(tab->buffer);
    }
}

void editor_mode_format_paragraph(EditorTab* tab, gint target_width) {
    if (tab == NULL || tab->buffer == NULL) return;
    if (target_width <= 20) target_width = 72;

    GtkTextIter start, end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &start, &end);
    if (!has_sel) {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start,
                                         gtk_text_buffer_get_insert(tab->buffer));
        /* Find paragraph bounds */
        while (!gtk_text_iter_is_start(&start)) {
            GtkTextIter prev = start;
            gtk_text_iter_backward_line(&start);
            gchar* line = gtk_text_buffer_get_text(tab->buffer, &start, &prev, FALSE);
            gchar* s = g_strstrip(g_strdup(line));
            g_free(line);
            gboolean blank = (s[0] == '\0');
            g_free(s);
            if (blank) {
                start = prev;
                break;
            }
        }
        end = start;
        while (!gtk_text_iter_is_end(&end)) {
            GtkTextIter next = end;
            gtk_text_iter_forward_line(&next);
            gchar* line = gtk_text_buffer_get_text(tab->buffer, &end, &next, FALSE);
            gchar* s = g_strstrip(g_strdup(line));
            g_free(line);
            gboolean blank = (s[0] == '\0');
            g_free(s);
            if (blank) {
                break;
            }
            end = next;
        }
    }

    gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    gchar** words = g_strsplit_set(text, " \t\r\n", -1);
    g_free(text);

    GString* result = g_string_new(NULL);
    gint cur_line_len = 0;

    for (gint i = 0; words[i] != NULL; i++) {
        if (words[i][0] == '\0') continue;
        gint wlen = g_utf8_strlen(words[i], -1);
        if (cur_line_len == 0) {
            g_string_append(result, words[i]);
            cur_line_len = wlen;
        } else if (cur_line_len + 1 + wlen <= target_width) {
            g_string_append_c(result, ' ');
            g_string_append(result, words[i]);
            cur_line_len += 1 + wlen;
        } else {
            g_string_append_c(result, '\n');
            g_string_append(result, words[i]);
            cur_line_len = wlen;
        }
    }
    g_strfreev(words);

    gtk_text_buffer_begin_user_action(tab->buffer);
    gtk_text_buffer_delete(tab->buffer, &start, &end);
    gtk_text_buffer_insert(tab->buffer, &start, result->str, result->len);
    gtk_text_buffer_end_user_action(tab->buffer);

    g_string_free(result, TRUE);
}

void editor_mode_change_case(EditorTab* tab, gint mode) {
    if (tab == NULL || tab->buffer == NULL) return;

    GtkTextIter start, end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &start, &end);
    if (!has_sel) {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start,
                                         gtk_text_buffer_get_insert(tab->buffer));
        end = start;
        gtk_text_iter_forward_word_end(&end);
    }

    gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    if (!text || text[0] == '\0') {
        g_free(text);
        return;
    }

    GString* out = g_string_new(NULL);
    gboolean first_word = TRUE;

    for (const gchar* p = text; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (mode == CASE_LOWER) {
            c = g_unichar_tolower(c);
        } else if (mode == CASE_UPPER) {
            c = g_unichar_toupper(c);
        } else if (mode == CASE_TOGGLE) {
            if (g_unichar_islower(c)) c = g_unichar_toupper(c);
            else if (g_unichar_isupper(c)) c = g_unichar_tolower(c);
        } else if (mode == CASE_CAPITALIZE) {
            if (g_unichar_isalpha(c)) {
                if (first_word) {
                    c = g_unichar_toupper(c);
                    first_word = FALSE;
                } else {
                    c = g_unichar_tolower(c);
                }
            } else {
                first_word = TRUE;
            }
        }
        g_string_append_unichar(out, c);
    }

    gtk_text_buffer_begin_user_action(tab->buffer);
    gtk_text_buffer_delete(tab->buffer, &start, &end);
    gtk_text_buffer_insert(tab->buffer, &start, out->str, out->len);
    gtk_text_buffer_end_user_action(tab->buffer);

    g_string_free(out, TRUE);
    g_free(text);
}

void editor_mode_indent_selection(EditorTab* tab, gboolean unindent) {
    if (tab == NULL || tab->buffer == NULL) return;

    GtkTextIter start, end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &start, &end);
    if (!has_sel) {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &start,
                                         gtk_text_buffer_get_insert(tab->buffer));
        end = start;
    }

    gint s_line = gtk_text_iter_get_line(&start);
    gint e_line = gtk_text_iter_get_line(&end);
    if (e_line < s_line) {
        gint tmp = s_line;
        s_line = e_line;
        e_line = tmp;
    }

    gtk_text_buffer_begin_user_action(tab->buffer);
    for (gint l = s_line; l <= e_line; l++) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_line(tab->buffer, &iter, l);
        if (unindent) {
            GtkTextIter cut_end = iter;
            gint count = 0;
            while (count < 4 && !gtk_text_iter_ends_line(&cut_end)) {
                gunichar c = gtk_text_iter_get_char(&cut_end);
                if (c == ' ') {
                    gtk_text_iter_forward_char(&cut_end);
                    count++;
                } else if (c == '\t') {
                    gtk_text_iter_forward_char(&cut_end);
                    break;
                } else {
                    break;
                }
            }
            if (count > 0 || !gtk_text_iter_equal(&iter, &cut_end)) {
                gtk_text_buffer_delete(tab->buffer, &iter, &cut_end);
            }
        } else {
            gtk_text_buffer_insert(tab->buffer, &iter, "    ", 4);
        }
    }
    gtk_text_buffer_end_user_action(tab->buffer);
}

gboolean editor_mode_find_next(EditorTab* tab, const gchar* query, gboolean forward, gboolean wrap) {
    if (tab == NULL || tab->buffer == NULL || query == NULL || query[0] == '\0') {
        return FALSE;
    }

    GtkTextIter cursor, match_start, match_end;
    GtkTextIter sel_start, sel_end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &sel_start, &sel_end);

    if (forward) {
        cursor = has_sel ? sel_end : sel_start;
        if (!has_sel) {
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &cursor,
                                             gtk_text_buffer_get_insert(tab->buffer));
        }
        if (gtk_text_iter_forward_search(&cursor, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                         &match_start, &match_end, NULL)) {
            gtk_text_buffer_select_range(tab->buffer, &match_start, &match_end);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &match_start, 0.1, TRUE, 0.0, 0.5);
            return TRUE;
        }
        if (wrap) {
            GtkTextIter start;
            gtk_text_buffer_get_start_iter(tab->buffer, &start);
            if (gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match_start, &match_end, &cursor)) {
                gtk_text_buffer_select_range(tab->buffer, &match_start, &match_end);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &match_start, 0.1, TRUE, 0.0, 0.5);
                return TRUE;
            }
        }
    } else {
        cursor = has_sel ? sel_start : sel_end;
        if (!has_sel) {
            gtk_text_buffer_get_iter_at_mark(tab->buffer, &cursor,
                                             gtk_text_buffer_get_insert(tab->buffer));
        }
        if (gtk_text_iter_backward_search(&cursor, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                          &match_start, &match_end, NULL)) {
            gtk_text_buffer_select_range(tab->buffer, &match_start, &match_end);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &match_start, 0.1, TRUE, 0.0, 0.5);
            return TRUE;
        }
        if (wrap) {
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(tab->buffer, &end);
            if (gtk_text_iter_backward_search(&end, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                              &match_start, &match_end, &cursor)) {
                gtk_text_buffer_select_range(tab->buffer, &match_start, &match_end);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &match_start, 0.1, TRUE, 0.0, 0.5);
                return TRUE;
            }
        }
    }
    return FALSE;
}

gint editor_mode_replace(EditorTab* tab, const gchar* query,
                         const gchar* replacement, gboolean replace_all) {
    if (tab == NULL || tab->buffer == NULL || query == NULL || query[0] == '\0') {
        return 0;
    }
    if (replacement == NULL) replacement = "";

    if (!replace_all) {
        GtkTextIter sel_start, sel_end;
        if (gtk_text_buffer_get_selection_bounds(tab->buffer, &sel_start, &sel_end)) {
            gchar* sel_text = gtk_text_buffer_get_text(tab->buffer, &sel_start, &sel_end, FALSE);
            if (g_strcmp0(sel_text, query) == 0) {
                gtk_text_buffer_begin_user_action(tab->buffer);
                gtk_text_buffer_delete(tab->buffer, &sel_start, &sel_end);
                gtk_text_buffer_insert(tab->buffer, &sel_start, replacement, -1);
                gtk_text_buffer_end_user_action(tab->buffer);
                g_free(sel_text);
                editor_mode_find_next(tab, query, TRUE, TRUE);
                return 1;
            }
            g_free(sel_text);
        }
        if (editor_mode_find_next(tab, query, TRUE, TRUE)) {
            if (gtk_text_buffer_get_selection_bounds(tab->buffer, &sel_start, &sel_end)) {
                gtk_text_buffer_begin_user_action(tab->buffer);
                gtk_text_buffer_delete(tab->buffer, &sel_start, &sel_end);
                gtk_text_buffer_insert(tab->buffer, &sel_start, replacement, -1);
                gtk_text_buffer_end_user_action(tab->buffer);
                editor_mode_find_next(tab, query, TRUE, TRUE);
                return 1;
            }
        }
        return 0;
    }

    /* Replace all occurrences */
    gint count = 0;
    gtk_text_buffer_begin_user_action(tab->buffer);

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(tab->buffer, &iter);
    GtkTextIter m_start, m_end;

    while (gtk_text_iter_forward_search(&iter, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                        &m_start, &m_end, NULL)) {
        gtk_text_buffer_delete(tab->buffer, &m_start, &m_end);
        gtk_text_buffer_insert(tab->buffer, &m_start, replacement, -1);
        count++;
        iter = m_start;
        if (gtk_text_iter_is_end(&iter)) break;
    }

    gtk_text_buffer_end_user_action(tab->buffer);
    return count;
}

void editor_mode_goto_line(EditorTab* tab, gint line) {
    if (tab == NULL || tab->buffer == NULL) return;
    if (line < 1) line = 1;
    gint total = gtk_text_buffer_get_line_count(tab->buffer);
    if (line > total) line = total;

    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &iter, line - 1);
    gtk_text_buffer_place_cursor(tab->buffer, &iter);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &iter, 0.2, TRUE, 0.0, 0.5);
    editor_modes_update_status(tab);
}

gboolean editor_mode_get_wrap(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL) return FALSE;
    return gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(tab->view)) != GTK_WRAP_NONE;
}

gboolean editor_mode_toggle_wrap(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL) return FALSE;
    gboolean wrap = !editor_mode_get_wrap(tab);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tab->view), wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    return wrap;
}

/* ------------------------------------------------------------------------- */
/* Notepad Mode UI & Signals                                                 */
/* ------------------------------------------------------------------------- */

/* Line ending in each mode's words: Notepad's, Emacs' mnemonic, Vim's 'fileformat' */
typedef enum { EOL_NOTEPAD, EOL_EMACS, EOL_VIM } EolStyle;

static const gchar* eol_label(EditorTab* tab, EolStyle style) {
    static const gchar* const names[3][3] = {
        {N_("Unix (LF)"), N_("Windows (CRLF)"), N_("Mac (CR)")},
        {":", "\\", "/"},
        {"unix", "dos", "mac"},
    };
    gint kind = 0;
    if (g_strcmp0(tab->line_ending, "Windows (CRLF)") == 0) {
        kind = 1;
    } else if (g_strcmp0(tab->line_ending, "Mac (CR)") == 0) {
        kind = 2;
    }
    return style == EOL_NOTEPAD ? _(names[style][kind]) : names[style][kind];
}

/* Notepad: "⎇ main   src/main.c •" on the left, "Ln 3, Col 7 | 100% | C | Unix (LF) | UTF-8 | clangd: 1 error" on the right */
static void notepad_update_status(EditorTab* tab, EditorModeState* st, const EditorInfo* info,
                                  gint line, gint col, gboolean modified) {
    GString* file = g_string_new(NULL);
    if (info->branch != NULL) {
        g_string_append_printf(file, "⎇ %s   ", info->branch);
    }
    g_string_append(file, info->relpath != NULL ? info->relpath : "");
    if (modified) {
        g_string_append(file, " •");
    }
    gtk_label_set_text(GTK_LABEL(st->np_file_label), file->str);
    g_string_free(file, TRUE);

    GString* text = g_string_new(NULL);
    /* TRANSLATORS: Notepad status bar: line, column, zoom, language, line ending */
    g_string_append_printf(text, _("Ln %1$d, Col %2$d   |   %3$d%%   |   %4$s   |   %5$s   |   UTF-8"), line, col,
                           fonts_zoom_percent(), info->language, eol_label(tab, EOL_NOTEPAD));
    if (info->backend != NULL) {
        g_string_append_printf(text, "   |   %s", info->backend);
        if (info->errors > 0 || info->warnings > 0) {
            gchar* errors = g_strdup_printf(ngettext("%u error", "%u errors", info->errors), info->errors);
            gchar* warnings = g_strdup_printf(ngettext("%u warning", "%u warnings", info->warnings), info->warnings);
            /* TRANSLATORS: after the language server name: "1 error, 2 warnings" */
            g_string_append_printf(text, _(": %1$s, %2$s"), errors, warnings);
            g_free(errors);
            g_free(warnings);
        }
    }
    gtk_label_set_text(GTK_LABEL(st->np_info_label), text->str);
    g_string_free(text, TRUE);
}

/* Emacs: "-U:**-  src/main.c   Top (0%)  L3 C7  Git-main  (C clangd Flymake[1 0])" */
static void emacs_update_status(EditorTab* tab, EditorModeState* st, const EditorInfo* info,
                                gint line, gint col, gint percent, gboolean modified) {
    gchar* flag = g_strdup_printf("-U%s%s- ", eol_label(tab, EOL_EMACS), modified ? "**" : "--");
    gtk_label_set_text(GTK_LABEL(st->emacs_ml_status), flag);
    g_free(flag);

    gtk_label_set_text(GTK_LABEL(st->emacs_ml_bufname),
                       info->relpath != NULL ? info->relpath : (tab->name ? tab->name : "*scratch*"));

    const gchar* pos_str = (percent <= 0) ? "Top" : ((percent >= 100) ? "Bot" : "All");
    gchar* pos_label = g_strdup_printf("%s (%d%%)", pos_str, percent);
    gtk_label_set_text(GTK_LABEL(st->emacs_ml_pos), pos_label);
    g_free(pos_label);

    gchar* lc = g_strdup_printf("L%d C%d", line, col);
    gtk_label_set_text(GTK_LABEL(st->emacs_ml_linecol), lc);
    g_free(lc);

    gchar* vc = info->branch != NULL ? g_strdup_printf("Git-%s", info->branch) : g_strdup("");
    gtk_label_set_text(GTK_LABEL(st->emacs_ml_vc), vc);
    gtk_widget_set_visible(st->emacs_ml_vc, info->branch != NULL);
    g_free(vc);

    GString* modes = g_string_new("  (");
    g_string_append(modes, info->language);
    if (info->backend != NULL) {
        g_string_append_printf(modes, " %s", info->backend);
    }
    if (info->server) {
        g_string_append_printf(modes, " Flymake[%u %u]", info->errors, info->warnings);
    }
    g_string_append(modes, ")  ");
    gtk_label_set_text(GTK_LABEL(st->emacs_ml_modes), modes->str);
    g_string_free(modes, TRUE);
}

/* Vim, as an airline statusline: "NORMAL  ᚠ main  src/main.c [+]" on the left,
 * "word  c  utf-8[unix]    42% ☰   42/100 ㏑ :  7" on the right */
static void vim_update_status(EditorTab* tab, EditorModeState* st, const EditorInfo* info,
                              gint line, gint col, gint percent, gint total, gboolean modified) {
    GString* file = g_string_new(NULL);
    if (info->branch != NULL) {
        g_string_append_printf(file, " ᚠ %s  ", info->branch);
    }
    g_string_append(file, info->relpath != NULL ? info->relpath : (tab->name ? tab->name : "[No Name]"));
    if (modified) {
        g_string_append(file, " [+]");
    }
    gtk_label_set_text(GTK_LABEL(st->vim_sl_file), file->str);
    g_string_free(file, TRUE);

    GString* text = g_string_new(NULL);
    if (info->errors > 0 || info->warnings > 0) {
        g_string_append_printf(text, "E:%u W:%u  ", info->errors, info->warnings);
    }
    gchar* word = cursor_word(tab);
    if (word != NULL) {
        g_string_append_printf(text, "%s  ", word);
        g_free(word);
    }
    const gchar* ft = g_strcmp0(info->language_id, "python3") == 0 ? "python" : info->language_id;
    g_string_append_printf(text, "%s  utf-8[%s]", ft, eol_label(tab, EOL_VIM));
    gtk_label_set_text(GTK_LABEL(st->vim_sl_info), text->str);
    g_string_free(text, TRUE);

    gchar* ruler = g_strdup_printf("%3d%% ☰ %4d/%d ㏑ :%3d", percent, line, total, col);
    gtk_label_set_text(GTK_LABEL(st->vim_sl_ruler), ruler);
    g_free(ruler);
}

void editor_modes_update_status(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;

    gint line = 1, col = 1, percent = 0, total = 1;
    get_cursor_position(tab, &line, &col, &percent, &total);
    gboolean modified = gtk_text_buffer_get_modified(tab->buffer);
    EditorInfo info;
    editor_info_get(st->app, tab, &info);

    if (st->np_info_label != NULL) {
        notepad_update_status(tab, st, &info, line, col, modified);
    }
    if (st->emacs_modeline != NULL) {
        emacs_update_status(tab, st, &info, line, col, percent, modified);
    }
    if (st->vim_statusline != NULL) {
        vim_update_status(tab, st, &info, line, col, percent, total, modified);
    }
    editor_info_clear(&info);
}

void editor_modes_refresh_bars(AppData* app) {
    for (GList* l = app != NULL ? app->editors : NULL; l != NULL; l = l->next) {
        editor_modes_update_status((EditorTab*)l->data);
    }
}

void editor_modes_refresh_git(AppData* app) {
    for (GList* l = app != NULL ? app->editors : NULL; l != NULL; l = l->next) {
        editor_info_forget((EditorTab*)l->data);
    }
    editor_modes_refresh_bars(app);
}

static void on_tab_buffer_changed(GtkTextBuffer* buffer, gpointer user_data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)user_data;
    editor_modes_update_status(tab);
}

static void on_tab_cursor_moved(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    EditorTab* tab = (EditorTab*)user_data;
    editor_modes_update_status(tab);
}

/* ------------------------------------------------------------------------- */
/* Tab Initialization & Mode Setup                                           */
/* ------------------------------------------------------------------------- */

/* Ctrl+Click: go to the definition of the identifier clicked (all modes) */
static void on_view_ctrl_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    EditorTab* tab = data;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if (n_press != 1 || (mods & GDK_CONTROL_MASK) == 0 || (mods & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0 ||
        !code_nav_is_code(tab)) {
        return;
    }
    GtkTextView* view = GTK_TEXT_VIEW(tab->view);
    gint bx = 0, by = 0;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y, &bx, &by);
    GtkTextIter it;
    if (!gtk_text_view_get_iter_at_location(view, &it, bx, by)) return;
    /* The text view must not start a selection */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_text_buffer_place_cursor(tab->buffer, &it);
    gtk_widget_grab_focus(tab->view);
    code_nav_goto_definition(tab->app, tab, FALSE, editor_mode_message);
}

void editor_modes_init_tab(AppData* app, EditorTab* tab) {
    if (tab == NULL) return;
    editor_modes_load_css();

    EditorModeState* st = g_new0(EditorModeState, 1);
    st->tab = tab;
    st->app = app;
    tab->mode_state = st;

    init_buffer_tags(tab);

    notepad_build_ui(tab, st);
    emacs_build_ui(tab, st);
    vim_build_ui(tab, st);

    /* Add all 3 boxes to tab container */
    gtk_box_append(GTK_BOX(tab->container), st->np_box);
    gtk_box_append(GTK_BOX(tab->container), st->emacs_box);
    gtk_box_append(GTK_BOX(tab->container), st->vim_box);

    /* Hide the old static status line if present */
    if (tab->status_bar != NULL) {
        gtk_widget_set_visible(tab->status_bar, FALSE);
    }

    /* Connect text buffer changes for live status update */
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_tab_buffer_changed), tab);
    g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_tab_cursor_moved), tab);
    g_signal_connect(tab->buffer, "modified-changed", G_CALLBACK(on_tab_buffer_changed), tab);
    orgmode_editor_init(tab);

    GtkGesture* ctrl_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(ctrl_click), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(ctrl_click), GTK_PHASE_CAPTURE);
    g_signal_connect(ctrl_click, "pressed", G_CALLBACK(on_view_ctrl_click), tab);
    gtk_widget_add_controller(tab->view, GTK_EVENT_CONTROLLER(ctrl_click));
    code_help_attach(app, tab, editor_mode_message);
    code_intel_attach(app, tab);

    /* Apply initial mode */
    editor_modes_apply_mode(app, tab, tab->mode);
}

void editor_modes_free_tab(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    g_free(st->emacs_last_search);
    g_free(st->emacs_qr_find);
    g_free(st->emacs_qr_rep);
    vim_free(tab);
    g_free(st->emacs_prompt_data);
    if (st->emacs_completions != NULL) g_ptr_array_unref(st->emacs_completions);
    if (st->emacs_is_steps != NULL) g_array_unref(st->emacs_is_steps);
    g_free(st);
    tab->mode_state = NULL;
}

void editor_modes_apply_mode(AppData* app, EditorTab* tab, EditorMode mode) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    tab->mode = mode;
    if (mode == EDITOR_MODE_ORGMODE) {
        tab->text_mode = FALSE;
    }

    /* Hide all mode boxes initially */
    notepad_reset(tab);
    gtk_widget_set_visible(st->np_box, FALSE);
    gtk_widget_set_visible(st->emacs_box, FALSE);
    gtk_widget_set_visible(st->vim_box, FALSE);
    if (mode != EDITOR_MODE_VIM) vim_deactivate(tab);

    emacs_block_cursor_set(tab, mode == EDITOR_MODE_EMACS || mode == EDITOR_MODE_ORGMODE);
    /* Org highlighting follows Org editing (M-x org-mode / text-mode) */
    gchar* lang_name = (tab->path != NULL) ? g_path_get_basename(tab->path) : g_strdup(tab->name);
    apply_buffer_language(tab->buffer, lang_name, orgmode_editor_is_org(tab));
    g_free(lang_name);
    if (mode == EDITOR_MODE_EMACS || mode == EDITOR_MODE_ORGMODE) {
        gtk_widget_set_visible(st->emacs_box, TRUE);
        emacs_close_minibuffer(tab);
        set_view_monospace(tab->view, TRUE);
        gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
    } else if (mode == EDITOR_MODE_VIM) {
        gtk_widget_set_visible(st->vim_box, TRUE);
        set_view_monospace(tab->view, TRUE);
        vim_activate(tab);
    } else {
        gtk_widget_set_visible(st->np_box, TRUE);
        gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
    }

    editor_modes_update_status(tab);
    if (app != NULL) {
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }
}

/* ------------------------------------------------------------------------- */
/* Key Handlers for Each Mode                                                */
/* ------------------------------------------------------------------------- */

static gboolean is_modifier_key(guint keyval) {
    switch (keyval) {
        case GDK_KEY_Control_L: case GDK_KEY_Control_R:
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
        case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
        case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
        case GDK_KEY_Super_L: case GDK_KEY_Super_R:
        case GDK_KEY_ISO_Level3_Shift: case GDK_KEY_Caps_Lock:
            return TRUE;
        default:
            return FALSE;
    }
}

static gboolean dispatch_key(AppData* app, EditorTab* tab, EditorModeState* st,
                             guint keyval, GdkModifierType state) {

    /* Any other command restarts the C-l cycle (modifier presses do not count) */
    if (!is_modifier_key(keyval) &&
        !((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_l || keyval == GDK_KEY_L))) {
        st->emacs_recenter_step = 0;
    }
    /* Likewise, C-SPC C-SPC only toggles the mark when pressed in a row */
    if (!is_modifier_key(keyval) &&
        !((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_space || keyval == GDK_KEY_at))) {
        st->emacs_last_set_mark = FALSE;
    }

    /* C-u (universal-argument) in Emacs-like modes; vim keeps C-u for scrolling */
    if ((tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) &&
        st->emacs_prefix == EMACS_PREFIX_NONE && st->org_prefix == ORG_PREFIX_NONE &&
        (state & GDK_CONTROL_MASK) && !(state & GDK_ALT_MASK) &&
        (keyval == GDK_KEY_u || keyval == GDK_KEY_U)) {
        st->emacs_universal_arg = TRUE;
        emacs_echo(tab, "C-u-");
        return TRUE;
    }

    /* Next / previous tab keys of the tab's mode that take no prefix (Vim and
     * Notepad Ctrl+PgDn / Ctrl+PgUp, Notepad Ctrl+Tab); gt and C-x <right>
     * go through the Vim engine and the Emacs C-x prefix */
    if (tab->mode == EDITOR_MODE_VIM || tab->mode == EDITOR_MODE_NOTEPAD) {
        NavStyle style = tab->mode == EDITOR_MODE_VIM ? NAV_STYLE_VIM : NAV_STYLE_NOTEPAD;
        NavAction action = nav_keys_lookup(style, NAV_PREFIX_NONE, keyval, state, NULL);
        if (action == NAV_TAB_NEXT || action == NAV_TAB_PREV) {
            editor_modes_step_tab(tab, action == NAV_TAB_NEXT ? 1 : -1);
            return TRUE;
        }
    }

    if (orgmode_editor_is_org(tab)) {
        if (orgmode_editor_handle_key(app, tab, keyval, state)) {
            return TRUE;
        }
        if (tab->mode == EDITOR_MODE_VIM) {
            return vim_handle_key(tab, st, keyval, state);
        }
        return emacs_handle_key(tab, st, keyval, state);
    }

    if (tab->mode == EDITOR_MODE_EMACS) {
        return emacs_handle_key(tab, st, keyval, state);
    }

    if (tab->mode == EDITOR_MODE_VIM) {
        return vim_handle_key(tab, st, keyval, state);
    }

    return notepad_handle_key(tab, st, keyval, state);
}

gboolean editor_modes_handle_key(AppData* app, EditorTab* tab, guint keyval,
                                 guint keycode, GdkModifierType state) {
    (void)keycode;
    if (tab == NULL || tab->mode_state == NULL) return FALSE;
    EditorModeState* st = tab->mode_state;
    code_help_on_key(tab, keyval);
    gboolean was_c_u = st->emacs_universal_arg;
    gboolean handled = dispatch_key(app, tab, st, keyval, state);
    /* The prefix argument lasts until a complete command (C-c . is two keys) */
    if (was_c_u && !is_modifier_key(keyval) &&
        st->emacs_prefix == EMACS_PREFIX_NONE && st->org_prefix == ORG_PREFIX_NONE) {
        st->emacs_universal_arg = FALSE;
    }
    return handled;
}

gboolean editor_modes_claims_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->window == NULL || app->last_focused_editor == NULL) return FALSE;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(app->window));
    EditorTab* tab = app->last_focused_editor;
    /* Only the tab whose view has the focus (the pointer may be stale otherwise) */
    if (focus == NULL || focus != tab->view || tab->mode_state == NULL) return FALSE;
    if (tab->mode == EDITOR_MODE_VIM && vim_claims_key(tab, keyval, state)) return TRUE;
    return orgmode_editor_claims_key(tab, keyval, state);
}

void editor_modes_step_tab(EditorTab* tab, gint step) {
    if (tab == NULL || tab->container == NULL) return;
    /* The notebook holding the page now (a dragged tab changed notebook) */
    GtkWidget* nb = gtk_widget_get_ancestor(tab->container, GTK_TYPE_NOTEBOOK);
    if (nb == NULL) nb = tab->nb;
    if (nb != NULL) focus_route_step_tab(tab->app, GTK_NOTEBOOK(nb), step, TRUE);
}

void editor_modes_cancel_pending(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    gboolean had = st->emacs_prefix != EMACS_PREFIX_NONE || st->org_prefix != ORG_PREFIX_NONE ||
                   st->emacs_universal_arg;
    st->emacs_prefix = EMACS_PREFIX_NONE;
    st->org_prefix = ORG_PREFIX_NONE;
    st->emacs_universal_arg = FALSE;
    if (tab->mode == EDITOR_MODE_VIM) {
        if (vim_cancel_pending(tab)) had = FALSE; /* the Vim command line is cleared */
    }
    if (had) editor_mode_message(tab, ""); /* "C-x-", "C-c-", "C-u-" go away */
}

void editor_mode_message(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
        emacs_echo(tab, msg);
    } else if (tab->mode == EDITOR_MODE_VIM) {
        vim_echo(tab, msg);
    } else if (st->np_status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), msg != NULL ? msg : "");
    }
}

/* The tab title is file_type_label_new(): the file-type icon, then the name */
void editor_tab_label_update(EditorTab* tab) {
    GtkWidget* image = gtk_widget_get_first_child(tab->tab_label);
    GtkWidget* label = gtk_widget_get_last_child(tab->tab_label);
    file_type_image_set(image, tab->name, FALSE);
    gchar* title = tab->is_buffer ? g_strdup(tab->name)
                   : gtk_text_buffer_get_modified(tab->buffer)
                       ? g_strdup_printf("%s  \xe2\x80\xa2", tab->name)
                       : g_strdup(tab->name);
    gtk_label_set_text(GTK_LABEL(label), title);
    g_free(title);
}
