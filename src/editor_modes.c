#include "editor_modes.h"
#include "status_bar.h"
#include "file_types.h"
#include "editor.h"
#include "file_list.h"
#include "orgmode.h"
#include "ui.h"
#include "viewer.h"
#include <ctype.h>
#include <string.h>

/* Sub-modes for Vim */
typedef enum {
    VIM_SUB_NORMAL = 0,
    VIM_SUB_INSERT,
    VIM_SUB_VISUAL,
    VIM_SUB_VISUAL_LINE,
    VIM_SUB_COMMAND,
    VIM_SUB_SEARCH_FWD,
    VIM_SUB_SEARCH_BWD
} VimSubMode;

/* Pending operators for Vim */
typedef enum {
    VIM_OP_NONE = 0,
    VIM_OP_DELETE,
    VIM_OP_YANK,
    VIM_OP_CHANGE,
    VIM_OP_G,
    VIM_OP_Z,
    VIM_OP_REPLACE_CHAR,
    VIM_OP_INDENT,
    VIM_OP_UNINDENT
} VimPendingOp;

/* Emacs key prefix state */
typedef enum {
    EMACS_PREFIX_NONE = 0,
    EMACS_PREFIX_CTRL_X,
    EMACS_PREFIX_META_G
} EmacsPrefix;

/* Emacs minibuffer mode */
typedef enum {
    EMACS_MINI_NONE = 0,
    EMACS_MINI_ISEARCH_FWD,
    EMACS_MINI_ISEARCH_BWD,
    EMACS_MINI_QUERY_REPLACE_FIND,
    EMACS_MINI_QUERY_REPLACE_REP,
    EMACS_MINI_WRITE_FILE,
    EMACS_MINI_GOTO_LINE,
    EMACS_MINI_MX
} EmacsMiniMode;

/* Mode-specific data for one EditorTab */
typedef struct {
    EditorTab* tab;
    AppData* app;

    gboolean tags_initialized;

    /* --- Notepad UI & State --- */
    GtkWidget* np_box;
    GtkWidget* np_find_bar;
    GtkWidget* np_find_entry;
    GtkWidget* np_replace_entry;
    GtkWidget* np_replace_box;
    GtkWidget* np_status_label;
    GtkWidget* np_info_label;
    GtkWidget* np_save_bar;
    GtkWidget* np_save_entry;

    /* --- Emacs UI & State --- */
    GtkWidget* emacs_box;
    GtkWidget* emacs_modeline;
    GtkWidget* emacs_ml_status;
    GtkWidget* emacs_ml_bufname;
    GtkWidget* emacs_ml_modes;
    GtkWidget* emacs_ml_pos;
    GtkWidget* emacs_ml_linecol;
    GtkWidget* emacs_minibuffer;
    GtkWidget* emacs_mb_prompt;
    GtkWidget* emacs_mb_entry;
    GtkWidget* emacs_mb_echo;

    EmacsPrefix emacs_prefix;
    EmacsMiniMode emacs_mini_mode;
    gboolean emacs_mark_active;
    GtkTextMark* emacs_mark;
    gchar* emacs_last_search;
    gchar* emacs_qr_find;
    gchar* emacs_qr_rep;
    gint emacs_qr_count;
    GtkTextMark* emacs_search_start_mark;

    /* --- Vim UI & State --- */
    GtkWidget* vim_box;
    GtkWidget* vim_statusline;
    GtkWidget* vim_sl_mode;
    GtkWidget* vim_sl_file;
    GtkWidget* vim_sl_ruler;
    GtkWidget* vim_commandline;
    GtkWidget* vim_cl_prompt;
    GtkWidget* vim_cl_entry;
    GtkWidget* vim_cl_echo;

    VimSubMode vim_submode;
    VimPendingOp vim_pending_op;
    gint vim_count;
    gchar* vim_last_search;
    gboolean vim_search_fwd;
    GtkTextMark* vim_visual_start_mark;

} EditorModeState;

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
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &iter,
                                     gtk_text_buffer_get_insert(tab->buffer));
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
    gtk_text_buffer_set_modified(tab->buffer, FALSE);

    editor_tab_label_update(tab);
    if (app != NULL) {
        refresh_listing(app);
        update_status_bar(app);
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

static void on_np_find_next_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* q = gtk_editable_get_text(GTK_EDITABLE(st->np_find_entry));
    if (!editor_mode_find_next(tab, q, TRUE, TRUE)) {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "Search string not found");
    } else {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "");
    }
}

static void on_np_find_prev_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* q = gtk_editable_get_text(GTK_EDITABLE(st->np_find_entry));
    if (!editor_mode_find_next(tab, q, FALSE, TRUE)) {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "Search string not found");
    } else {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "");
    }
}

static void on_np_replace_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* q = gtk_editable_get_text(GTK_EDITABLE(st->np_find_entry));
    const gchar* r = gtk_editable_get_text(GTK_EDITABLE(st->np_replace_entry));
    gint count = editor_mode_replace(tab, q, r, FALSE);
    if (count > 0) {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "Occurrence replaced");
    } else {
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "No match to replace");
    }
}

static void on_np_replace_all_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* q = gtk_editable_get_text(GTK_EDITABLE(st->np_find_entry));
    const gchar* r = gtk_editable_get_text(GTK_EDITABLE(st->np_replace_entry));
    gint count = editor_mode_replace(tab, q, r, TRUE);
    gchar* msg = g_strdup_printf("Replaced %d occurrence(s)", count);
    gtk_label_set_text(GTK_LABEL(st->np_status_label), msg);
    g_free(msg);
}

static void on_np_align_btn_clicked(GtkButton* btn, gpointer user_data) {
    EditorTab* tab = (EditorTab*)user_data;
    const gchar* tag = (const gchar*)g_object_get_data(G_OBJECT(btn), "align-type");
    GtkJustification just = GTK_JUSTIFY_LEFT;
    if (g_strcmp0(tag, "center") == 0) just = GTK_JUSTIFY_CENTER;
    else if (g_strcmp0(tag, "right") == 0) just = GTK_JUSTIFY_RIGHT;
    else if (g_strcmp0(tag, "fill") == 0) just = GTK_JUSTIFY_FILL;
    editor_mode_align_text(tab, just, 80);
}

void notepad_show_find(EditorTab* tab, gboolean is_replace) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    gtk_widget_set_visible(st->np_find_bar, TRUE);
    gtk_widget_set_visible(st->np_replace_box, is_replace);
    if (is_replace) {
        gtk_widget_grab_focus(st->np_replace_entry);
    } else {
        gtk_widget_grab_focus(st->np_find_entry);
    }
}

void notepad_hide_find(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    gtk_widget_set_visible(st->np_find_bar, FALSE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

static void on_np_save_as_confirm(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    const gchar* path = gtk_editable_get_text(GTK_EDITABLE(st->np_save_entry));
    if (path && path[0] != '\0') {
        editor_save_tab_as((AppData*)tab->app, tab, path);
    }
    notepad_hide_save_as(tab);
}

void notepad_show_save_as(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    gtk_editable_set_text(GTK_EDITABLE(st->np_save_entry), tab->name ? tab->name : "untitled.txt");
    gtk_widget_set_visible(st->np_save_bar, TRUE);
    gtk_widget_grab_focus(st->np_save_entry);
}

void notepad_hide_save_as(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    gtk_widget_set_visible(st->np_save_bar, FALSE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

/* ------------------------------------------------------------------------- */
/* Emacs Visual Clone Minibuffer & Helpers                                   */
/* ------------------------------------------------------------------------- */

void emacs_echo(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st->emacs_mb_echo != NULL) {
        gtk_label_set_text(GTK_LABEL(st->emacs_mb_echo), msg ? msg : "");
    }
}

static void emacs_close_minibuffer(EditorTab* tab) {
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

void vim_echo(EditorTab* tab, const gchar* msg) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st->vim_cl_echo != NULL) {
        gtk_label_set_text(GTK_LABEL(st->vim_cl_echo), msg ? msg : "");
    }
}

static void vim_enter_submode(EditorTab* tab, VimSubMode submode) {
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

    /* --- Build Notepad UI --- */
    GtkWidget* np_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Notepad Find & Replace bar */
    GtkWidget* find_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(find_bar, "notepad-bar");
    gtk_widget_set_visible(find_bar, FALSE);

    /* Row 1: Find */
    GtkWidget* row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_f = gtk_label_new("Find:");
    st->np_find_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_find_entry, TRUE);
    GtkWidget* btn_fn = gtk_button_new_with_label("Find Next");
    GtkWidget* btn_fp = gtk_button_new_with_label("Find Prev");
    GtkWidget* btn_fc = gtk_button_new_with_label("✕");

    g_signal_connect(btn_fn, "clicked", G_CALLBACK(on_np_find_next_clicked), tab);
    g_signal_connect(btn_fp, "clicked", G_CALLBACK(on_np_find_prev_clicked), tab);
    g_signal_connect_swapped(btn_fc, "clicked", G_CALLBACK(notepad_hide_find), tab);
    g_signal_connect(st->np_find_entry, "activate", G_CALLBACK(on_np_find_next_clicked), tab);

    gtk_box_append(GTK_BOX(row1), lbl_f);
    gtk_box_append(GTK_BOX(row1), st->np_find_entry);
    gtk_box_append(GTK_BOX(row1), btn_fn);
    gtk_box_append(GTK_BOX(row1), btn_fp);
    gtk_box_append(GTK_BOX(row1), btn_fc);
    gtk_box_append(GTK_BOX(find_bar), row1);

    /* Row 2: Replace */
    GtkWidget* row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_r = gtk_label_new("Replace:");
    st->np_replace_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_replace_entry, TRUE);
    GtkWidget* btn_rep = gtk_button_new_with_label("Replace");
    GtkWidget* btn_rep_all = gtk_button_new_with_label("Replace All");

    g_signal_connect(btn_rep, "clicked", G_CALLBACK(on_np_replace_clicked), tab);
    g_signal_connect(btn_rep_all, "clicked", G_CALLBACK(on_np_replace_all_clicked), tab);
    g_signal_connect(st->np_replace_entry, "activate", G_CALLBACK(on_np_replace_clicked), tab);

    gtk_box_append(GTK_BOX(row2), lbl_r);
    gtk_box_append(GTK_BOX(row2), st->np_replace_entry);
    gtk_box_append(GTK_BOX(row2), btn_rep);
    gtk_box_append(GTK_BOX(row2), btn_rep_all);

    /* Formatting quick buttons in notepad bar */
    GtkWidget* btn_al = gtk_button_new_with_label("Left");
    g_object_set_data((GObject*)btn_al, "align-type", (gpointer)"left");
    g_signal_connect(btn_al, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);
    GtkWidget* btn_ac = gtk_button_new_with_label("Center");
    g_object_set_data((GObject*)btn_ac, "align-type", (gpointer)"center");
    g_signal_connect(btn_ac, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);
    GtkWidget* btn_ar = gtk_button_new_with_label("Right");
    g_object_set_data((GObject*)btn_ar, "align-type", (gpointer)"right");
    g_signal_connect(btn_ar, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);

    gtk_box_append(GTK_BOX(row2), btn_al);
    gtk_box_append(GTK_BOX(row2), btn_ac);
    gtk_box_append(GTK_BOX(row2), btn_ar);

    st->np_replace_box = row2;
    gtk_box_append(GTK_BOX(find_bar), row2);
    st->np_find_bar = find_bar;

    /* Save As Bar */
    GtkWidget* save_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(save_bar, "notepad-bar");
    gtk_widget_set_visible(save_bar, FALSE);
    GtkWidget* lbl_sa = gtk_label_new("Save As:");
    st->np_save_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_save_entry, TRUE);
    GtkWidget* btn_sa_save = gtk_button_new_with_label("Save");
    GtkWidget* btn_sa_canc = gtk_button_new_with_label("Cancel");

    g_signal_connect(btn_sa_save, "clicked", G_CALLBACK(on_np_save_as_confirm), tab);
    g_signal_connect_swapped(btn_sa_canc, "clicked", G_CALLBACK(notepad_hide_save_as), tab);
    g_signal_connect(st->np_save_entry, "activate", G_CALLBACK(on_np_save_as_confirm), tab);

    gtk_box_append(GTK_BOX(save_bar), lbl_sa);
    gtk_box_append(GTK_BOX(save_bar), st->np_save_entry);
    gtk_box_append(GTK_BOX(save_bar), btn_sa_save);
    gtk_box_append(GTK_BOX(save_bar), btn_sa_canc);
    st->np_save_bar = save_bar;

    /* Notepad Status Bar */
    GtkWidget* np_status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(np_status, "notepad-statusbar");
    st->np_status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(st->np_status_label), 0.0f);
    gtk_widget_set_hexpand(st->np_status_label, TRUE);

    st->np_info_label = gtk_label_new("Ln 1, Col 1   |   100%   |   Windows (CRLF)   |   UTF-8");
    gtk_label_set_xalign(GTK_LABEL(st->np_info_label), 1.0f);

    gtk_box_append(GTK_BOX(np_status), st->np_status_label);
    gtk_box_append(GTK_BOX(np_status), st->np_info_label);

    gtk_box_append(GTK_BOX(np_box), st->np_find_bar);
    gtk_box_append(GTK_BOX(np_box), st->np_save_bar);
    gtk_box_append(GTK_BOX(np_box), np_status);
    st->np_box = np_box;

    /* --- Build Emacs Visual Clone UI --- */
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

    /* --- Build Vim Visual Clone UI --- */
    GtkWidget* vim_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Vim statusline */
    GtkWidget* statusline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(statusline, "vim-statusline");

    st->vim_sl_mode = gtk_label_new("NORMAL");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-badge");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-normal");

    st->vim_sl_file = gtk_label_new(tab->name ? tab->name : "[No Name]");
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
        update_status_bar(app);
    }
}

/* ------------------------------------------------------------------------- */
/* Key Handlers for Each Mode                                                */
/* ------------------------------------------------------------------------- */

static gboolean notepad_handle_key(EditorTab* tab, EditorModeState* st,
                                   guint keyval, GdkModifierType state) {
    (void)st;
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    if (ctrl && !shift && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        notepad_show_find(tab, FALSE);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        notepad_show_find(tab, TRUE);
        return TRUE;
    }
    if (keyval == GDK_KEY_F3) {
        on_np_find_next_clicked(NULL, tab);
        return TRUE;
    }
    if (shift && keyval == GDK_KEY_F3) {
        on_np_find_prev_clicked(NULL, tab);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        if (tab->path != NULL && tab->path[0] != '\0') {
            editor_save_current((AppData*)tab->app);
        } else {
            notepad_show_save_as(tab);
        }
        return TRUE;
    }
    if (ctrl && shift && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        notepad_show_save_as(tab);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_g || keyval == GDK_KEY_G)) {
        notepad_show_find(tab, FALSE);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z)) {
        gtk_text_buffer_undo(tab->buffer);
        return TRUE;
    }
    if ((ctrl && !shift && (keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) ||
        (ctrl && shift && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z))) {
        gtk_text_buffer_redo(tab->buffer);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        editor_mode_align_text(tab, GTK_JUSTIFY_LEFT, 80);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_e || keyval == GDK_KEY_E)) {
        editor_mode_align_text(tab, GTK_JUSTIFY_CENTER, 80);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        editor_mode_align_text(tab, GTK_JUSTIFY_RIGHT, 80);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_j || keyval == GDK_KEY_J)) {
        editor_mode_align_text(tab, GTK_JUSTIFY_FILL, 80);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape) {
        notepad_hide_find(tab);
        notepad_hide_save_as(tab);
        return TRUE;
    }
    return FALSE;
}

static gboolean emacs_handle_key(EditorTab* tab, EditorModeState* st,
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

static gboolean vim_handle_key(EditorTab* tab, EditorModeState* st,
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

gboolean editor_modes_handle_key(AppData* app, EditorTab* tab, guint keyval,
                                 guint keycode, GdkModifierType state) {
    (void)keycode;
    if (tab == NULL || tab->mode_state == NULL) return FALSE;
    EditorModeState* st = (EditorModeState*)tab->mode_state;

    if (tab->mode == EDITOR_MODE_ORGMODE || (tab->name != NULL && is_org_file(tab->name))) {
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
