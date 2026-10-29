#include "editor_modes.h"
#include "app_events.h"
#include "text_style.h"
#include "file_types.h"
#include "diff_gutter.h"
#include <ctype.h>
#include <string.h>

#include "editor_modes_private.h"

/* CSS for visual clones */
static void editor_modes_ensure_css(void) {
    static gboolean css_loaded = FALSE;
    if (css_loaded) return;
    css_loaded = TRUE;

    GtkCssProvider* css = gtk_css_provider_new();
    static const gchar* CSS_DATA =
        /* --- Emacs Visual Clone --- */
        ".emacs-modeline {\n"
        "  background: #2b2e37;\n"
        "  color: #dfdfdf;\n"
        "  border-top: 1px solid #4a4e5a;\n"
        "  border-bottom: 1px solid #1e2025;\n"
        "  box-shadow: 0 1px 2px rgba(0,0,0,0.4);\n"
        "  font-family: 'Consolas', 'DejaVu Sans Mono', 'Liberation Mono', monospace;\n"
        "  font-size: 12px;\n"
        "  padding: 2px 8px;\n"
        "  min-height: 22px;\n"
        "}\n"
        ".emacs-modeline-status {\n"
        "  color: #87afff;\n"
        "  font-weight: bold;\n"
        "}\n"
        ".emacs-modeline-bufname {\n"
        "  font-weight: bold;\n"
        "  color: #ffffff;\n"
        "}\n"
        ".emacs-modeline-mode {\n"
        "  color: #a0c0ff;\n"
        "}\n"
        ".emacs-modeline-pos {\n"
        "  color: #cccccc;\n"
        "}\n"
        ".emacs-minibuffer {\n"
        "  background: #191a1e;\n"
        "  color: #e0e0e0;\n"
        "  font-family: 'Consolas', 'DejaVu Sans Mono', 'Liberation Mono', monospace;\n"
        "  font-size: 13px;\n"
        "  padding: 2px 8px;\n"
        "  min-height: 24px;\n"
        "  border-top: 1px solid #282a30;\n"
        "}\n"
        ".emacs-minibuffer-prompt {\n"
        "  font-weight: bold;\n"
        "  color: #87d7ff;\n"
        "}\n"
        ".emacs-minibuffer-entry {\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  box-shadow: none;\n"
        "  color: #ffffff;\n"
        "  padding: 0;\n"
        "  margin: 0;\n"
        "}\n"
        ".emacs-minibuffer-echo {\n"
        "  color: #cccccc;\n"
        "}\n"
        /* --- Vim Visual Clone --- */
        ".vim-statusline {\n"
        "  background: #333333;\n"
        "  color: #d0d0d0;\n"
        "  font-family: 'Consolas', 'DejaVu Sans Mono', 'Liberation Mono', monospace;\n"
        "  font-size: 12px;\n"
        "  padding: 2px 6px;\n"
        "  min-height: 22px;\n"
        "  border-top: 1px solid #444444;\n"
        "}\n"
        ".vim-mode-badge {\n"
        "  font-weight: bold;\n"
        "  padding: 1px 6px;\n"
        "  border-radius: 2px;\n"
        "  margin-right: 6px;\n"
        "}\n"
        ".vim-mode-normal {\n"
        "  background: #857b6f;\n"
        "  color: #000000;\n"
        "}\n"
        ".vim-mode-insert {\n"
        "  background: #5f8700;\n"
        "  color: #ffffff;\n"
        "}\n"
        ".vim-mode-visual {\n"
        "  background: #d78700;\n"
        "  color: #000000;\n"
        "}\n"
        ".vim-mode-command {\n"
        "  background: #0087af;\n"
        "  color: #ffffff;\n"
        "}\n"
        ".vim-commandline {\n"
        "  background: #181818;\n"
        "  color: #ffffff;\n"
        "  font-family: 'Consolas', 'DejaVu Sans Mono', 'Liberation Mono', monospace;\n"
        "  font-size: 13px;\n"
        "  padding: 2px 8px;\n"
        "  min-height: 24px;\n"
        "  border-top: 1px solid #222222;\n"
        "}\n"
        ".vim-commandline-prompt {\n"
        "  font-weight: bold;\n"
        "  color: #ffffff;\n"
        "}\n"
        ".vim-commandline-entry {\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  box-shadow: none;\n"
        "  color: #ffffff;\n"
        "  padding: 0;\n"
        "  margin: 0;\n"
        "}\n"
        /* --- Notepad Visual Style --- */
        ".notepad-statusbar {\n"
        "  background: #252526;\n"
        "  color: #a0a0a0;\n"
        "  font-family: 'Segoe UI', 'Ubuntu', 'Cantarell', sans-serif;\n"
        "  font-size: 11px;\n"
        "  padding: 3px 12px;\n"
        "  border-top: 1px solid #333333;\n"
        "  min-height: 22px;\n"
        "}\n"
        ".notepad-bar {\n"
        "  background: #2d2d2d;\n"
        "  color: #cccccc;\n"
        "  border-top: 1px solid #3f3f46;\n"
        "  border-bottom: 1px solid #3f3f46;\n"
        "  padding: 4px 8px;\n"
        "}\n";

    gtk_css_provider_load_from_string(css, CSS_DATA);
    GdkDisplay* display = gdk_display_get_default();
    if (display != NULL) {
        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(css);
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

static void get_cursor_position(EditorTab* tab, gint* out_line, gint* out_col, gint* out_percent) {
    if (out_line) *out_line = 1;
    if (out_col) *out_col = 1;
    if (out_percent) *out_percent = 0;
    if (tab == NULL || tab->buffer == NULL) return;

    GtkTextIter iter;
    if (tab->mode == EDITOR_MODE_VIM && tab->mode_state != NULL) {
        vim_cursor_get(tab, &iter); /* not the selection end in visual mode */
    } else {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &iter,
                                         gtk_text_buffer_get_insert(tab->buffer));
    }
    gint line = gtk_text_iter_get_line(&iter) + 1;
    gint col = gtk_text_iter_get_line_offset(&iter) + 1;
    gint total_lines = gtk_text_buffer_get_line_count(tab->buffer);
    if (total_lines <= 1) total_lines = 1;

    gint percent = (line * 100) / total_lines;
    if (percent > 100) percent = 100;

    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
    if (out_percent) *out_percent = percent;
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
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    GError* err = NULL;

    if (!g_file_set_contents(full_path, text, -1, &err)) {
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
        EditorModeState* st = (EditorModeState*)tab->mode_state;
        if (st && st->np_status_label) {
            gchar* msg = g_strdup_printf("Saved to %s", tab->name);
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

/* ------------------------------------------------------------------------- */
/* Notepad Mode UI & Signals                                                 */
/* ------------------------------------------------------------------------- */

void editor_modes_update_status(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;

    gint line = 1, col = 1, percent = 0;
    get_cursor_position(tab, &line, &col, &percent);
    gboolean modified = gtk_text_buffer_get_modified(tab->buffer);

    /* 1. Notepad Status */
    if (st->np_info_label != NULL) {
        gchar* info = g_strdup_printf("Ln %d, Col %d   |   100%%   |   Windows (CRLF)   |   UTF-8", line, col);
        gtk_label_set_text(GTK_LABEL(st->np_info_label), info);
        g_free(info);
    }

    /* 2. Emacs Mode-line Visual Clone */
    if (st->emacs_modeline != NULL) {
        /* Flags: -U:--- or -U:**- */
        const gchar* flag = modified ? "-U:**- " : "-U:--- ";
        gtk_label_set_text(GTK_LABEL(st->emacs_ml_status), flag);

        /* Buffer name */
        gtk_label_set_text(GTK_LABEL(st->emacs_ml_bufname), tab->name ? tab->name : "*scratch*");

        /* Pos */
        const gchar* pos_str = (percent <= 0) ? "Top" : ((percent >= 100) ? "Bot" : "All");
        gchar* pos_label = g_strdup_printf("%s (%d%%)", pos_str, percent);
        gtk_label_set_text(GTK_LABEL(st->emacs_ml_pos), pos_label);
        g_free(pos_label);

        /* Line and Column */
        gchar* lc = g_strdup_printf("L%d C%d", line, col);
        gtk_label_set_text(GTK_LABEL(st->emacs_ml_linecol), lc);
        g_free(lc);
    }

    /* 3. Vim Statusline Visual Clone */
    if (st->vim_statusline != NULL) {
        /* File name with flags */
        gchar* file_info = g_strdup_printf("\"%s\" %s [utf-8]",
                                          tab->name ? tab->name : "[No Name]",
                                          modified ? "[+]" : "");
        gtk_label_set_text(GTK_LABEL(st->vim_sl_file), file_info);
        g_free(file_info);

        /* Ruler */
        const gchar* pos_str = (percent <= 0) ? "Top" : ((percent >= 100) ? "Bot" : "All");
        gchar* ruler = g_strdup_printf("%d,%d   %s", line, col, pos_str);
        gtk_label_set_text(GTK_LABEL(st->vim_sl_ruler), ruler);
        g_free(ruler);
    }
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

void editor_modes_init_tab(AppData* app, EditorTab* tab) {
    if (tab == NULL) return;
    editor_modes_ensure_css();

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
    orgmode_editor_init(tab);

    /* Apply initial mode */
    editor_modes_apply_mode(app, tab, tab->mode);
}

void editor_modes_free_tab(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    g_free(st->emacs_last_search);
    g_free(st->emacs_qr_find);
    g_free(st->emacs_qr_rep);
    g_free(st->vim_last_search);
    g_free(st->emacs_prompt_data);
    if (st->emacs_completions != NULL) g_ptr_array_unref(st->emacs_completions);
    if (st->emacs_is_steps != NULL) g_array_unref(st->emacs_is_steps);
    g_free(st);
    tab->mode_state = NULL;
}

void editor_modes_apply_mode(AppData* app, EditorTab* tab, EditorMode mode) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    tab->mode = mode;

    /* Hide all mode boxes initially */
    gtk_widget_set_visible(st->np_box, FALSE);
    gtk_widget_set_visible(st->emacs_box, FALSE);
    gtk_widget_set_visible(st->vim_box, FALSE);

    if (mode == EDITOR_MODE_EMACS || mode == EDITOR_MODE_ORGMODE) {
        gtk_widget_set_visible(st->emacs_box, TRUE);
        emacs_close_minibuffer(tab);
        set_view_monospace(tab->view, TRUE);
        gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
        if (is_org_file(tab->name) || mode == EDITOR_MODE_ORGMODE) {
            gtk_label_set_text(GTK_LABEL(st->emacs_ml_modes), "  (Org)  ");
        } else {
            gtk_label_set_text(GTK_LABEL(st->emacs_ml_modes), "  (Text Fill)  ");
        }
    } else if (mode == EDITOR_MODE_VIM) {
        gtk_widget_set_visible(st->vim_box, TRUE);
        vim_enter_submode(tab, VIM_SUB_NORMAL);
        set_view_monospace(tab->view, TRUE);
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

gboolean editor_modes_handle_key(AppData* app, EditorTab* tab, guint keyval,
                                 guint keycode, GdkModifierType state) {
    (void)keycode;
    if (tab == NULL || tab->mode_state == NULL) return FALSE;
    EditorModeState* st = (EditorModeState*)tab->mode_state;

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

gboolean editor_modes_claims_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->window == NULL || app->last_focused_editor == NULL) return FALSE;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(app->window));
    EditorTab* tab = app->last_focused_editor;
    /* Only the tab whose view has the focus (the pointer may be stale otherwise) */
    if (focus == NULL || focus != tab->view || tab->mode_state == NULL) return FALSE;
    return orgmode_editor_claims_key(tab, keyval, state);
}

void editor_tab_label_update(EditorTab* tab) {
    const gchar* icon = tab->is_buffer ? "📝" : file_icon(tab->name, FALSE);
    gchar* title =
        (!tab->is_buffer && gtk_text_buffer_get_modified(tab->buffer))
            ? g_strdup_printf("%s %s  \xe2\x80\xa2", icon, tab->name)
            : g_strdup_printf("%s %s", icon, tab->name);
    gtk_label_set_text(GTK_LABEL(tab->tab_label), title);
    g_free(title);
}
