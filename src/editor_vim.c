/* Vim editor mode: status line, command line and the host side of the Vim
 * engine (vim_engine.h), which does the editing itself. */
#include "editor_modes_private.h"
#include "editor.h"
#include "editor_view.h"
#include "palette.h"
#include "color_column.h"
#include "diff_gutter.h"
#include "code_nav.h"
#include "code_help.h"
#include "code_refs.h"
#include "code_symbols.h"
#include "vim_engine.h"
#include "vim_keys.h"
#include "nav_keys.h"
#include "app_events.h"
#include "text_encoding.h"
#include <string.h>

static EditorModeState* state_of(EditorTab* tab) {
    return (EditorModeState*)tab->mode_state;
}

static void set_echo(EditorTab* tab, const gchar* msg, gboolean error) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = state_of(tab);
    if (st->vim_cl_echo == NULL) return;
    gtk_label_set_text(GTK_LABEL(st->vim_cl_echo), msg != NULL ? msg : "");
    if (error) gtk_widget_add_css_class(st->vim_cl_echo, "vim-message-error");
    else gtk_widget_remove_css_class(st->vim_cl_echo, "vim-message-error");
    st->vim_echo_sticky = msg != NULL && strchr(msg, '\n') != NULL;
}

void vim_echo(EditorTab* tab, const gchar* msg) {
    set_echo(tab, msg, FALSE);
}

void vim_cursor_get(EditorTab* tab, GtkTextIter* it) {
    EditorModeState* st = state_of(tab);
    if (st != NULL && st->vim != NULL) {
        vim_engine_get_cursor(st->vim, it);
    } else {
        gtk_text_buffer_get_iter_at_mark(tab->buffer, it, gtk_text_buffer_get_insert(tab->buffer));
    }
}

gboolean vim_is_inserting(EditorTab* tab) {
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL) return FALSE;
    VimMode m = vim_engine_mode(st->vim);
    return m == VIM_MODE_INSERT || m == VIM_MODE_REPLACE;
}

/* ------------------------------------------------------------------------- */
/* Status line                                                               */
/* ------------------------------------------------------------------------- */

static const gchar* badge_text(VimMode m, const gchar* label) {
    switch (m) {
        case VIM_MODE_INSERT: return strstr(label, "(insert)") != NULL ? "(INSERT)" : "INSERT";
        case VIM_MODE_REPLACE: return "REPLACE";
        case VIM_MODE_VISUAL: return "VISUAL";
        case VIM_MODE_VISUAL_LINE: return "V-LINE";
        case VIM_MODE_VISUAL_BLOCK: return "V-BLOCK";
        case VIM_MODE_CMDLINE: return "COMMAND";
        case VIM_MODE_OPERATOR_PENDING: return "O-PENDING";
        default: return strstr(label, "(insert)") != NULL ? "(INSERT)" : "NORMAL";
    }
}

static const gchar* badge_class(VimMode m) {
    switch (m) {
        case VIM_MODE_INSERT: return "vim-mode-insert";
        case VIM_MODE_REPLACE: return "vim-mode-replace";
        case VIM_MODE_VISUAL: case VIM_MODE_VISUAL_LINE: case VIM_MODE_VISUAL_BLOCK: return "vim-mode-visual";
        case VIM_MODE_CMDLINE: return "vim-mode-command";
        default: return "vim-mode-normal";
    }
}

/* ------------------------------------------------------------------------- */
/* Wildmenu: Tab completion of the Ex command line                           */
/* ------------------------------------------------------------------------- */

static void wild_free(EditorModeState* st) {
    g_clear_pointer(&st->vim_wild, g_strfreev);
    g_clear_pointer(&st->vim_wild_orig, g_free);
}

/* Hide the completion list and show the statusline again */
static void wild_clear(EditorModeState* st) {
    if (st->vim_wild == NULL) return;
    wild_free(st);
    for (GtkWidget* c = gtk_widget_get_first_child(st->vim_statusline); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        gtk_widget_set_visible(c, c != st->vim_wild_label);
    }
}

static gint text_width(GtkWidget* w, const gchar* text) {
    PangoLayout* layout = gtk_widget_create_pango_layout(w, text);
    gint width = 0;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

/* The completions that fit in the statusline, around the current one
 * ("<" / ">" mark more on either side), the current one highlighted */
static void wild_render(EditorModeState* st) {
    GtkWidget* label = st->vim_wild_label;
    gint n = (gint)g_strv_length(st->vim_wild);
    gint cur = MAX(st->vim_wild_index, 0);
    gint* widths = g_new(gint, n);
    for (gint i = 0; i < n; i++) widths[i] = text_width(label, st->vim_wild[i]);
    gint sep = text_width(label, "  ");
    gint arrow = text_width(label, "< ");
    gint avail = gtk_widget_get_width(st->vim_statusline) - 24;
    if (avail <= 0) avail = G_MAXINT / 2; /* not allocated yet: show everything */

    if (cur < st->vim_wild_first) st->vim_wild_first = cur;
    gint first = st->vim_wild_first;
    for (;;) {
        gint w = first > 0 ? arrow : 0;
        for (gint i = first; i <= cur; i++) w += widths[i] + (i > first ? sep : 0);
        if (cur < n - 1) w += arrow;
        if (w <= avail || first == cur) break;
        first++;
    }
    st->vim_wild_first = first;
    gint used = first > 0 ? arrow : 0;
    gint last = first;
    for (gint i = first; i < n; i++) {
        gint more = (i < n - 1) ? arrow : 0;
        gint w = used + widths[i] + (i > first ? sep : 0);
        if (i > cur && w + more > avail) break;
        used = w;
        last = i;
    }
    g_free(widths);

    GString* markup = g_string_new(first > 0 ? "&lt; " : "");
    for (gint i = first; i <= last; i++) {
        if (i > first) g_string_append(markup, "  ");
        gchar* item = i == st->vim_wild_index
            ? g_markup_printf_escaped("<span background=\"%s\" foreground=\"%s\">%s</span>",
                                      palette_color(PAL_HIGHLIGHT), palette_color(PAL_HIGHLIGHT_FG), st->vim_wild[i])
            : g_markup_escape_text(st->vim_wild[i], -1);
        g_string_append(markup, item);
        g_free(item);
    }
    if (last < n - 1) g_string_append(markup, " &gt;");
    gtk_label_set_markup(GTK_LABEL(label), markup->str);
    g_string_free(markup, TRUE);
}

static void hide_cmdline(EditorModeState* st) {
    wild_clear(st);
    gtk_widget_set_visible(st->vim_cl_prompt, FALSE);
    gtk_widget_set_visible(st->vim_cl_entry, FALSE);
    gtk_widget_set_visible(st->vim_cl_echo, TRUE);
}

static void on_state_changed(gpointer data) {
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL) return;
    VimMode m = vim_engine_mode(st->vim);
    const gchar* label = vim_engine_mode_label(st->vim);
    static const gchar* const CLASSES[] = {
        "vim-mode-normal", "vim-mode-insert", "vim-mode-replace", "vim-mode-visual", "vim-mode-command",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(CLASSES); i++) gtk_widget_remove_css_class(st->vim_sl_mode, CLASSES[i]);
    gtk_widget_add_css_class(st->vim_sl_mode, badge_class(m));
    gtk_label_set_text(GTK_LABEL(st->vim_sl_mode), badge_text(m, label));

    gchar* pending = vim_engine_pending_keys(st->vim);
    gunichar rec = vim_engine_recording(st->vim);
    gchar* showcmd = rec != 0 ? g_strdup_printf("recording @%c  %s", (gchar)rec, pending) : g_strdup(pending);
    gtk_label_set_text(GTK_LABEL(st->vim_sl_showcmd), showcmd);
    g_free(showcmd);
    g_free(pending);

    if (m != VIM_MODE_CMDLINE && gtk_widget_get_visible(st->vim_cl_entry)) {
        hide_cmdline(st);
        if (tab->view != NULL) gtk_widget_grab_focus(tab->view);
    }
    gboolean inserting = (m == VIM_MODE_INSERT || m == VIM_MODE_REPLACE);
    if (st->emacs_block_on == inserting) emacs_block_cursor_set(tab, !inserting);
    gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), m == VIM_MODE_REPLACE);
    if (m != VIM_MODE_CMDLINE) {
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer));
    }
    editor_modes_update_status(tab);
}

/* ------------------------------------------------------------------------- */
/* Host callbacks                                                            */
/* ------------------------------------------------------------------------- */

static void on_message(gpointer data, const gchar* text, gboolean error) {
    set_echo(data, text, error);
}

static void on_cmdline_open(gpointer data, gunichar prompt, const gchar* text) {
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    gchar p[8] = {0};
    g_unichar_to_utf8(prompt, p);
    gtk_label_set_text(GTK_LABEL(st->vim_cl_prompt), p);
    st->vim_cl_setting = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(st->vim_cl_entry), text != NULL ? text : "");
    st->vim_cl_setting = FALSE;
    g_free(st->vim_hist_prefix);
    st->vim_hist_prefix = NULL;
    wild_clear(st);
    gtk_widget_set_visible(st->vim_cl_echo, FALSE);
    gtk_widget_set_visible(st->vim_cl_prompt, TRUE);
    gtk_widget_set_visible(st->vim_cl_entry, TRUE);
    gtk_widget_grab_focus(st->vim_cl_entry);
    gtk_editable_set_position(GTK_EDITABLE(st->vim_cl_entry), -1);
}

static void report_written(EditorTab* tab) {
    gint lines = gtk_text_buffer_get_line_count(tab->buffer);
    gint chars = gtk_text_buffer_get_char_count(tab->buffer);
    gchar* msg = g_strdup_printf("\"%s\" %dL, %dB written", tab->name, lines, chars);
    vim_echo(tab, msg);
    g_free(msg);
}

static gboolean save_tab(EditorTab* tab, const gchar* path, gchar** error) {
    AppData* app = (AppData*)tab->app;
    if (path != NULL && path[0] != '\0') return editor_save_tab_as(app, tab, path);
    if (tab->path == NULL) {
        if (error) *error = g_strdup("E32: No file name");
        return FALSE;
    }
    app->last_focused_editor = tab;
    editor_save_current(app);
    if (gtk_text_buffer_get_modified(tab->buffer)) {
        if (error) *error = g_strdup_printf("E212: Cannot open file for writing: %s", tab->path);
        return FALSE;
    }
    report_written(tab);
    return TRUE;
}

/* Closing the tab frees the engine, which is still running the command: it
 * waits for the main loop */
typedef struct {
    AppData* app;
    EditorTab* tab;
} DeferredClose;

static gboolean run_deferred_close(gpointer data) {
    DeferredClose* d = data;
    if (g_list_find(d->app->editors, d->tab) != NULL) {
        editor_close_tab(d->app, d->tab);
    }
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void defer_close(EditorTab* tab) {
    DeferredClose* d = g_new0(DeferredClose, 1);
    d->app = (AppData*)tab->app;
    d->tab = tab;
    g_idle_add(run_deferred_close, d);
}

static gboolean close_tab(EditorTab* tab, gboolean force, gchar** error) {
    /* A view of a split file closes: the other views keep the changes */
    gboolean twin = editor_view_twin((AppData*)tab->app, tab) != NULL;
    if (!twin && !force && gtk_text_buffer_get_modified(tab->buffer) && !tab->is_buffer) {
        if (error) *error = g_strdup("E37: No write since last change (add ! to override)");
        return FALSE;
    }
    if (force && !twin) gtk_text_buffer_set_modified(tab->buffer, FALSE);
    defer_close(tab);
    return TRUE;
}

/* Splitting builds a view whose engine is this one, still running the
 * command: it waits for the main loop too */
typedef struct {
    AppData* app;
    EditorTab* tab;
    EditorMoveDirection dir;
} DeferredSplit;

static gboolean run_deferred_split(gpointer data) {
    DeferredSplit* d = data;
    if (g_list_find(d->app->editors, d->tab) != NULL) {
        editor_split_tab(d->app, d->tab, d->dir);
    }
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void defer_split(EditorTab* tab, EditorMoveDirection dir) {
    DeferredSplit* d = g_new0(DeferredSplit, 1);
    d->app = (AppData*)tab->app;
    d->tab = tab;
    d->dir = dir;
    g_idle_add(run_deferred_split, d);
}

/* :only, Ctrl+W o: the other views of the file close */
static void only_view(EditorTab* tab) {
    if (editor_close_other_views((AppData*)tab->app, tab) == 0) {
        set_echo(tab, "Already only one window", FALSE);
    }
}

static gboolean set_view_option(EditorTab* tab, const gchar* arg) {
    AppData* app = (AppData*)tab->app;
    gboolean shown = diff_gutter_get_line_numbers(tab->view);
    if (strcmp(arg, "number") == 0 || strcmp(arg, "nu") == 0) {
        if (!shown) editor_toggle_line_numbers(app, tab);
        return TRUE;
    }
    if (strcmp(arg, "nonumber") == 0 || strcmp(arg, "nonu") == 0) {
        if (shown) editor_toggle_line_numbers(app, tab);
        return TRUE;
    }
    if (strcmp(arg, "number!") == 0 || strcmp(arg, "nu!") == 0 || strcmp(arg, "invnumber") == 0 ||
        strcmp(arg, "invnu") == 0) {
        editor_toggle_line_numbers(app, tab);
        return TRUE;
    }
    if (strcmp(arg, "number?") == 0 || strcmp(arg, "nu?") == 0) {
        vim_echo(tab, shown ? "  number" : "  nonumber");
        return TRUE;
    }
    /* colorcolumn / cc: the column line of this view (one column) */
    const gchar* eq = strchr(arg, '=');
    gsize name_len = eq != NULL ? (gsize)(eq - arg) : strlen(arg);
    if (name_len > 0 && arg[name_len - 1] == '?') name_len--;
    if ((name_len == 11 && strncmp(arg, "colorcolumn", 11) == 0) || (name_len == 2 && strncmp(arg, "cc", 2) == 0)) {
        if (eq == NULL) {
            gint col = color_column_get(tab->view);
            gchar* shown = col > 0 ? g_strdup_printf("  colorcolumn=%d", col) : g_strdup("  colorcolumn=");
            vim_echo(tab, shown);
            g_free(shown);
            return TRUE;
        }
        gchar* end = NULL;
        gint64 col = g_ascii_strtoll(eq + 1, &end, 10);
        if (eq[1] != '\0' && (end == NULL || *end != '\0' || col <= 0 || col > 1000)) {
            set_echo(tab, "E474: Invalid argument: colorcolumn (one column, 1 to 1000)", TRUE);
            return TRUE;
        }
        color_column_set(tab->view, eq[1] == '\0' ? 0 : (gint)col);
        return TRUE;
    }
    GtkTextView* v = GTK_TEXT_VIEW(tab->view);
    if (strcmp(arg, "wrap") == 0) {
        gtk_text_view_set_wrap_mode(v, GTK_WRAP_WORD_CHAR);
        return TRUE;
    }
    if (strcmp(arg, "nowrap") == 0) {
        gtk_text_view_set_wrap_mode(v, GTK_WRAP_NONE);
        return TRUE;
    }
    if (strcmp(arg, "wrap!") == 0 || strcmp(arg, "invwrap") == 0) {
        gtk_text_view_set_wrap_mode(v, gtk_text_view_get_wrap_mode(v) == GTK_WRAP_NONE ? GTK_WRAP_WORD_CHAR
                                                                                       : GTK_WRAP_NONE);
        return TRUE;
    }
    /* Engine options are only notified: nothing to do in the view */
    return FALSE;
}

static gboolean on_ex_command(gpointer data, const gchar* name, gboolean bang, const gchar* args,
                              gchar** error) {
    EditorTab* tab = data;
    AppData* app = (AppData*)tab->app;
    if (strcmp(name, "set") == 0) return set_view_option(tab, args);
    if (strcmp(name, "Symbols") == 0) {
        code_symbols_show(app, tab, editor_mode_message);
        return TRUE;
    }
    if (strcmp(name, "References") == 0) {
        code_refs_find(app, tab, editor_mode_message);
        return TRUE;
    }
    if (strcmp(name, "tag") == 0) {
        /* :tag {name}: its definition; :tag! {name}: its declaration */
        code_nav_goto_symbol(app, tab, args, bang, editor_mode_message);
        return TRUE;
    }
    if (strcmp(name, "write") == 0 || strcmp(name, "update") == 0 || strcmp(name, "wall") == 0) {
        if (name[0] == 'u' && !gtk_text_buffer_get_modified(tab->buffer)) return TRUE;
        save_tab(tab, args, error);
        return TRUE;
    }
    if (strcmp(name, "saveas") == 0) {
        if (args[0] == '\0') {
            if (error) *error = g_strdup("E471: Argument required");
        } else {
            editor_save_tab_as(app, tab, args);
        }
        return TRUE;
    }
    if (strcmp(name, "wq") == 0 || strcmp(name, "wqall") == 0 || strcmp(name, "xit") == 0 ||
        strcmp(name, "xall") == 0) {
        gboolean needed = name[0] == 'w' || gtk_text_buffer_get_modified(tab->buffer);
        if (needed && !save_tab(tab, args, error)) return TRUE;
        close_tab(tab, TRUE, error);
        return TRUE;
    }
    if (strcmp(name, "split") == 0 || strcmp(name, "vsplit") == 0) {
        if (args[0] != '\0') {
            if (error) *error = g_strdup("E488: Trailing characters (the split view shows this file)");
            return TRUE;
        }
        defer_split(tab, name[0] == 'v' ? EDITOR_MOVE_RIGHT : EDITOR_MOVE_DOWN);
        return TRUE;
    }
    if (strcmp(name, "only") == 0) {
        only_view(tab);
        return TRUE;
    }
    if (strcmp(name, "quit") == 0 || strcmp(name, "qall") == 0 || strcmp(name, "close") == 0) {
        close_tab(tab, bang, error);
        return TRUE;
    }
    if (strcmp(name, "edit") == 0) {
        if (args[0] == '\0') {
            if (!bang && gtk_text_buffer_get_modified(tab->buffer)) {
                if (error) *error = g_strdup("E37: No write since last change (add ! to override)");
                return TRUE;
            }
            gchar* text = NULL;
            gsize len = 0;
            if (tab->path != NULL && g_file_get_contents(tab->path, &text, &len, NULL)) {
                tab->line_ending = text_line_ending_detect(text, len);
                if (!g_utf8_validate(text, -1, NULL)) {
                    gchar* valid = g_utf8_make_valid(text, -1);
                    g_free(text);
                    text = valid;
                }
                gtk_text_buffer_set_text(tab->buffer, text, -1);
                gtk_text_buffer_set_modified(tab->buffer, FALSE);
                GtkTextIter start;
                gtk_text_buffer_get_start_iter(tab->buffer, &start);
                gtk_text_buffer_place_cursor(tab->buffer, &start);
                g_free(text);
            }
            return TRUE;
        }
        gchar* full;
        if (g_path_is_absolute(args)) {
            full = g_strdup(args);
        } else {
            gchar* dir = tab->path != NULL ? g_path_get_dirname(tab->path) : g_strdup(app->base_dir);
            full = g_build_filename(dir, args, NULL);
            g_free(dir);
        }
        gchar* base = g_path_get_basename(full);
        editor_open_file(app, base, full);
        g_free(base);
        g_free(full);
        return TRUE;
    }
    if (strcmp(name, "tabnext") == 0 || strcmp(name, "tabprevious") == 0) {
        editor_modes_step_tab(tab, name[3] == 'n' ? 1 : -1);
        return TRUE;
    }
    return FALSE;
}

/* gd, gD, Ctrl-]: the code navigation lookup in code files */
static gboolean on_goto_definition(gpointer data, const gchar* word, gboolean declaration) {
    (void)word; /* code_nav reads the identifier at the cursor itself */
    EditorTab* tab = data;
    if (!code_nav_is_code(tab)) return FALSE;
    return code_nav_goto_definition((AppData*)tab->app, tab, declaration, editor_mode_message);
}

/* K: the help card of the identifier under the cursor */
static gboolean on_keyword_help(gpointer data, const gchar* word) {
    (void)word;
    EditorTab* tab = data;
    if (!code_nav_is_code(tab)) return FALSE;
    code_help_show((AppData*)tab->app, tab);
    return TRUE;
}

/* gr: the References tab */
static gboolean on_references(gpointer data) {
    EditorTab* tab = data;
    return code_refs_find((AppData*)tab->app, tab, editor_mode_message);
}

/* Ctrl-O / Tab past the buffer's jump list: the history across files */
static gboolean on_jump_history(gpointer data, gboolean older) {
    EditorTab* tab = data;
    AppData* app = (AppData*)tab->app;
    return older ? code_nav_back(app, tab, editor_mode_message) : code_nav_forward(app, tab, editor_mode_message);
}

static gint line_height(EditorTab* tab) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    gint y = 0, h = 0;
    gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(tab->view), &it, &y, &h);
    return h > 0 ? h : 16;
}

static void on_scroll(gpointer data, VimScroll how, gint lines) {
    EditorTab* tab = data;
    GtkTextView* v = GTK_TEXT_VIEW(tab->view);
    GtkTextMark* ins = gtk_text_buffer_get_insert(tab->buffer);
    switch (how) {
        case VIM_SCROLL_CURSOR_CENTER: gtk_text_view_scroll_to_mark(v, ins, 0.0, TRUE, 0.0, 0.5); break;
        case VIM_SCROLL_CURSOR_TOP: gtk_text_view_scroll_to_mark(v, ins, 0.0, TRUE, 0.0, 0.0); break;
        case VIM_SCROLL_CURSOR_BOTTOM: gtk_text_view_scroll_to_mark(v, ins, 0.0, TRUE, 0.0, 1.0); break;
        case VIM_SCROLL_LINES: {
            GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
            if (adj == NULL) return;
            gdouble value = gtk_adjustment_get_value(adj) + (gdouble)lines * line_height(tab);
            gdouble max = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
            gtk_adjustment_set_value(adj, CLAMP(value, 0.0, MAX(max, 0.0)));
            break;
        }
    }
}

static gboolean on_visible_lines(gpointer data, gint* first, gint* last) {
    EditorTab* tab = data;
    GtkTextView* v = GTK_TEXT_VIEW(tab->view);
    GdkRectangle r;
    gtk_text_view_get_visible_rect(v, &r);
    if (r.height <= 0) return FALSE;
    GtkTextIter a, b;
    gint top = 0, h = 0;
    gtk_text_view_get_line_at_y(v, &a, r.y, &top);
    gint f = gtk_text_iter_get_line(&a);
    if (top < r.y) f++; /* only partly visible */
    gtk_text_view_get_line_at_y(v, &b, r.y + r.height - 1, &top);
    gint l = gtk_text_iter_get_line(&b);
    gtk_text_view_get_line_yrange(v, &b, &top, &h);
    if (top + h > r.y + r.height && l > f) l--;
    *first = MIN(f, l);
    *last = l;
    return TRUE;
}

static void on_clipboard_set(gpointer data, const gchar* text) {
    EditorTab* tab = data;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(tab->view), text);
}

static const gchar* on_file_name(gpointer data) {
    EditorTab* tab = data;
    return tab->path != NULL ? tab->path : tab->name;
}

/* Text copied in another application becomes the "+ register */
static void on_clipboard_read(GObject* source, GAsyncResult* res, gpointer data) {
    gchar* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, NULL);
    EditorTab* tab = data;
    /* tab is only valid while the read was not cancelled */
    if (text != NULL && tab->mode_state != NULL && state_of(tab)->vim != NULL) {
        vim_engine_clipboard_changed(state_of(tab)->vim, text);
    }
    g_free(text);
}

static void read_clipboard(EditorTab* tab) {
    EditorModeState* st = state_of(tab);
    GdkClipboard* clip = gtk_widget_get_clipboard(tab->view);
    if (gdk_clipboard_is_local(clip) || st->vim_clip_cancel == NULL) return;
    gdk_clipboard_read_text_async(clip, st->vim_clip_cancel, on_clipboard_read, tab);
}

static void on_clipboard_changed(GdkClipboard* clip, gpointer data) {
    (void)clip;
    read_clipboard(data);
}

/* ------------------------------------------------------------------------- */
/* Command line entry                                                        */
/* ------------------------------------------------------------------------- */

static void cmdline_done(EditorTab* tab, gboolean submit) {
    EditorModeState* st = state_of(tab);
    if (st->vim == NULL || vim_engine_mode(st->vim) != VIM_MODE_CMDLINE) return;
    gchar* text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(st->vim_cl_entry)));
    hide_cmdline(st);
    set_echo(tab, "", FALSE);
    gtk_widget_grab_focus(tab->view);
    if (submit) vim_engine_cmdline_submit(st->vim, text);
    else vim_engine_cmdline_cancel(st->vim);
    g_free(text);
}

static void on_cl_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    cmdline_done(data, TRUE);
}

static void on_cl_changed(GtkEditable* editable, gpointer data) {
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st->vim_cl_setting || st->vim == NULL) return;
    g_free(st->vim_hist_prefix);
    st->vim_hist_prefix = NULL;
    wild_clear(st);
    vim_engine_cmdline_changed(st->vim, gtk_editable_get_text(editable));
}

static void set_entry_text(EditorModeState* st, const gchar* text) {
    st->vim_cl_setting = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(st->vim_cl_entry), text);
    gtk_editable_set_position(GTK_EDITABLE(st->vim_cl_entry), -1);
    st->vim_cl_setting = FALSE;
}

/* Tab: complete with the first match and list them all (Vim's wildmenu);
 * the next Tabs / Shift+Tabs cycle through them, then back to the text typed */
static void wild_complete(EditorModeState* st, const gchar* text, gboolean back) {
    if (st->vim_wild == NULL) {
        gsize at = 0;
        gchar** matches = vim_engine_cmdline_matches(st->vim, text, &at);
        if (matches == NULL) return;
        if (matches[1] == NULL) {
            gchar* line = g_strdup_printf("%.*s%s", (gint)at, text, matches[0]);
            set_entry_text(st, line);
            g_free(line);
            g_strfreev(matches);
            return;
        }
        st->vim_wild = matches;
        st->vim_wild_orig = g_strdup(text);
        st->vim_wild_at = at;
        st->vim_wild_index = -1;
        st->vim_wild_first = 0;
        for (GtkWidget* c = gtk_widget_get_first_child(st->vim_statusline); c != NULL; c = gtk_widget_get_next_sibling(c)) {
            gtk_widget_set_visible(c, c == st->vim_wild_label);
        }
    }
    gint n = (gint)g_strv_length(st->vim_wild);
    gint i = st->vim_wild_index + (back ? -1 : 1);
    if (i >= n) i = -1;
    else if (i < -1) i = n - 1;
    st->vim_wild_index = i;
    gchar* line = i < 0 ? g_strdup(st->vim_wild_orig)
                        : g_strdup_printf("%.*s%s", (gint)st->vim_wild_at, st->vim_wild_orig, st->vim_wild[i]);
    set_entry_text(st, line);
    g_free(line);
    wild_render(st);
}

static gboolean on_cl_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                          GdkModifierType state, gpointer data) {
    (void)ctrl;
    (void)keycode;
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st->vim == NULL) return FALSE;
    gboolean ctrl_down = (state & GDK_CONTROL_MASK) != 0;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(st->vim_cl_entry));
    if (st->vim_cl_ctrl_r) {
        /* Ctrl-R {register}: insert the register text */
        st->vim_cl_ctrl_r = FALSE;
        gunichar k = vim_key_from_gdk(keyval, state);
        if (k == 0) return TRUE;
        if (k == VIM_KEY_CTRL('w')) {
            GtkTextIter ws, we;
            vim_engine_get_cursor(st->vim, &ws);
            we = ws;
            if (!gtk_text_iter_starts_word(&ws)) gtk_text_iter_backward_word_start(&ws);
            gtk_text_iter_forward_word_end(&we);
            gchar* word = gtk_text_iter_get_slice(&ws, &we);
            gint pos = gtk_editable_get_position(GTK_EDITABLE(st->vim_cl_entry));
            gtk_editable_insert_text(GTK_EDITABLE(st->vim_cl_entry), word, -1, &pos);
            gtk_editable_set_position(GTK_EDITABLE(st->vim_cl_entry), pos);
            g_free(word);
            return TRUE;
        }
        const gchar* reg = vim_engine_register(st->vim, k, NULL);
        if (reg != NULL) {
            gchar* one = g_strdup(reg);
            g_strchomp(one);
            gint pos = gtk_editable_get_position(GTK_EDITABLE(st->vim_cl_entry));
            gtk_editable_insert_text(GTK_EDITABLE(st->vim_cl_entry), one, -1, &pos);
            gtk_editable_set_position(GTK_EDITABLE(st->vim_cl_entry), pos);
            g_free(one);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape || (ctrl_down && (keyval == GDK_KEY_c || keyval == GDK_KEY_bracketleft))) {
        cmdline_done(tab, FALSE);
        return TRUE;
    }
    if (keyval == GDK_KEY_BackSpace && text[0] == '\0') {
        cmdline_done(tab, FALSE);
        return TRUE;
    }
    if (ctrl_down && keyval == GDK_KEY_r) {
        st->vim_cl_ctrl_r = TRUE;
        return TRUE;
    }
    if (ctrl_down && keyval == GDK_KEY_u) {
        wild_clear(st);
        set_entry_text(st, "");
        vim_engine_cmdline_changed(st->vim, "");
        return TRUE;
    }
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down || (ctrl_down && (keyval == GDK_KEY_p || keyval == GDK_KEY_n))) {
        wild_clear(st);
        if (st->vim_hist_prefix == NULL) st->vim_hist_prefix = g_strdup(text);
        gboolean older = keyval == GDK_KEY_Up || keyval == GDK_KEY_p;
        gchar* h = vim_engine_cmdline_history(st->vim, older ? -1 : 1, st->vim_hist_prefix);
        if (h != NULL) {
            set_entry_text(st, h);
            g_free(h);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
        const gchar* prompt = gtk_label_get_text(GTK_LABEL(st->vim_cl_prompt));
        if (strcmp(prompt, ":") == 0) wild_complete(st, text, keyval == GDK_KEY_ISO_Left_Tab || (state & GDK_SHIFT_MASK));
        return TRUE;
    }
    return FALSE;
}

static void on_cl_focus_leave(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL || !gtk_widget_get_visible(st->vim_cl_entry)) return;
    cmdline_done(tab, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Mode lifecycle                                                            */
/* ------------------------------------------------------------------------- */

static void on_view_focus_enter(GtkEventControllerFocus* ctrl, gpointer data);

void vim_build_ui(EditorTab* tab, EditorModeState* st) {
    GtkWidget* vim_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* The focused view of a split file hosts the shared engine */
    GtkEventController* view_focus = gtk_event_controller_focus_new();
    g_signal_connect(view_focus, "enter", G_CALLBACK(on_view_focus_enter), tab);
    gtk_widget_add_controller(tab->view, view_focus);

    GtkWidget* statusline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(statusline, "vim-statusline");

    st->vim_sl_mode = gtk_label_new("NORMAL");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-badge");
    gtk_widget_add_css_class(st->vim_sl_mode, "vim-mode-normal");

    st->vim_sl_file = gtk_label_new(tab->name ? tab->name : "[No Name]");
    editor_bar_label_shrinkable(st->vim_sl_file, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(st->vim_sl_file), 0.0f);
    gtk_widget_set_hexpand(st->vim_sl_file, TRUE);

    st->vim_sl_showcmd = gtk_label_new("");
    editor_bar_label_shrinkable(st->vim_sl_showcmd, PANGO_ELLIPSIZE_START);
    gtk_widget_add_css_class(st->vim_sl_showcmd, "vim-showcmd");
    /* Fixed width (like Vim's showcmd column) so typing a pending key such
     * as "g" never changes the statusline's requested width */
    gtk_label_set_width_chars(GTK_LABEL(st->vim_sl_showcmd), 10);
    gtk_label_set_max_width_chars(GTK_LABEL(st->vim_sl_showcmd), 10);

    st->vim_sl_info = gtk_label_new("");
    editor_bar_label_shrinkable(st->vim_sl_info, PANGO_ELLIPSIZE_START);

    st->vim_sl_ruler = gtk_label_new("1,1   All");
    gtk_label_set_xalign(GTK_LABEL(st->vim_sl_ruler), 1.0f);

    gtk_box_append(GTK_BOX(statusline), st->vim_sl_mode);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_file);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_showcmd);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_info);
    gtk_box_append(GTK_BOX(statusline), st->vim_sl_ruler);
    st->vim_wild_label = gtk_label_new("");
    editor_bar_label_shrinkable(st->vim_wild_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->vim_wild_label), 0.0f);
    gtk_widget_set_hexpand(st->vim_wild_label, TRUE);
    gtk_widget_set_visible(st->vim_wild_label, FALSE);
    gtk_box_append(GTK_BOX(statusline), st->vim_wild_label);
    st->vim_statusline = statusline;

    /* No spacing: the typed text follows the prompt as in Vim */
    GtkWidget* commandline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(commandline, "vim-commandline");

    st->vim_cl_prompt = gtk_label_new(":");
    gtk_widget_add_css_class(st->vim_cl_prompt, "vim-commandline-prompt");
    gtk_widget_set_visible(st->vim_cl_prompt, FALSE);

    st->vim_cl_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->vim_cl_entry, "vim-commandline-entry");
    gtk_widget_add_css_class(st->vim_cl_entry, "flat");
    gtk_widget_set_hexpand(st->vim_cl_entry, TRUE);
    gtk_widget_set_visible(st->vim_cl_entry, FALSE);

    st->vim_cl_echo = gtk_label_new("");
    editor_bar_label_shrinkable(st->vim_cl_echo, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->vim_cl_echo), 0.0f);
    gtk_widget_set_hexpand(st->vim_cl_echo, TRUE);

    g_signal_connect(st->vim_cl_entry, "activate", G_CALLBACK(on_cl_activate), tab);
    g_signal_connect(st->vim_cl_entry, "changed", G_CALLBACK(on_cl_changed), tab);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_cl_key), tab);
    gtk_widget_add_controller(st->vim_cl_entry, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_cl_focus_leave), tab);
    gtk_widget_add_controller(st->vim_cl_entry, focus);

    gtk_box_append(GTK_BOX(commandline), st->vim_cl_prompt);
    gtk_box_append(GTK_BOX(commandline), st->vim_cl_entry);
    gtk_box_append(GTK_BOX(commandline), st->vim_cl_echo);
    st->vim_commandline = commandline;

    gtk_box_append(GTK_BOX(vim_box), st->vim_statusline);
    gtk_box_append(GTK_BOX(vim_box), st->vim_commandline);
    st->vim_box = vim_box;
}

/* The views of a split file share one engine (one undo history, registers,
 * mode, like Vim's windows on a buffer): kept on the buffer with its users,
 * the focused view hosts it */
#define SHARED_ENGINE_KEY "vim-shared-engine"

typedef struct {
    VimEngine* engine;
    GList* tabs; /* the tabs using it */
} SharedEngine;

static SharedEngine* shared_of(EditorTab* tab) {
    return g_object_get_data(G_OBJECT(tab->buffer), SHARED_ENGINE_KEY);
}

static void on_view_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL || vim_engine_get_host_data(st->vim) == tab) return;
    vim_engine_set_host_data(st->vim, tab);
    on_state_changed(tab);
}

void vim_activate(EditorTab* tab) {
    EditorModeState* st = state_of(tab);
    SharedEngine* shared = st->vim == NULL ? shared_of(tab) : NULL;
    if (shared != NULL) {
        st->vim = shared->engine;
        shared->tabs = g_list_prepend(shared->tabs, tab);
        vim_engine_set_host_data(st->vim, tab);
    }
    if (st->vim == NULL) {
        VimHost host = {
            .message = on_message,
            .state_changed = on_state_changed,
            .cmdline_open = on_cmdline_open,
            .ex_command = on_ex_command,
            .scroll = on_scroll,
            .visible_lines = on_visible_lines,
            .clipboard_set = on_clipboard_set,
            .file_name = on_file_name,
            .goto_definition = on_goto_definition,
            .jump_history = on_jump_history,
            .keyword_help = on_keyword_help,
            .references = on_references,
        };
        st->vim = vim_engine_new(tab->buffer, &host, tab);
        shared = g_new0(SharedEngine, 1);
        shared->engine = st->vim;
        shared->tabs = g_list_prepend(NULL, tab);
        g_object_set_data(G_OBJECT(tab->buffer), SHARED_ENGINE_KEY, shared);
    }
    if (st->vim_clipboard == NULL) {
        st->vim_clip_cancel = g_cancellable_new();
        st->vim_clipboard = g_object_ref(gtk_widget_get_clipboard(tab->view));
        st->vim_clip_handler = g_signal_connect(st->vim_clipboard, "changed", G_CALLBACK(on_clipboard_changed), tab);
        read_clipboard(tab);
    }
    hide_cmdline(st);
    gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
    vim_engine_reset(st->vim);
    on_state_changed(tab);
    gtk_widget_grab_focus(tab->view); /* also after picking Vim in a menu */
}

static void release_engine(EditorModeState* st) {
    if (st->vim_clip_cancel != NULL) {
        g_cancellable_cancel(st->vim_clip_cancel);
        g_clear_object(&st->vim_clip_cancel);
    }
    if (st->vim_clipboard != NULL) {
        g_signal_handler_disconnect(st->vim_clipboard, st->vim_clip_handler);
        g_clear_object(&st->vim_clipboard);
        st->vim_clip_handler = 0;
    }
    SharedEngine* shared = shared_of(st->tab);
    if (shared != NULL && shared->engine == st->vim) {
        shared->tabs = g_list_remove(shared->tabs, st->tab);
        if (shared->tabs != NULL) {
            /* Another view keeps the engine: it hosts it from now on */
            if (vim_engine_get_host_data(st->vim) == st->tab) {
                vim_engine_set_host_data(st->vim, shared->tabs->data);
            }
            st->vim = NULL;
            return;
        }
        g_object_set_data(G_OBJECT(st->tab->buffer), SHARED_ENGINE_KEY, NULL);
        g_free(shared);
    }
    vim_engine_free(st->vim);
    st->vim = NULL;
}

void vim_deactivate(EditorTab* tab) {
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL) return;
    release_engine(st);
    hide_cmdline(st);
    gtk_text_view_set_overwrite(GTK_TEXT_VIEW(tab->view), FALSE);
}

/* The tab is being destroyed: its widgets may already be gone */
void vim_free(EditorTab* tab) {
    EditorModeState* st = state_of(tab);
    if (st == NULL) return;
    if (st->vim != NULL) release_engine(st);
    wild_free(st);
    g_free(st->vim_hist_prefix);
    st->vim_hist_prefix = NULL;
}

gboolean vim_claims_key(EditorTab* tab, guint keyval, GdkModifierType state) {
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->vim == NULL) return FALSE;
    if ((state & GDK_CONTROL_MASK) == 0 || (state & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0) return FALSE;
    /* Ctrl-R redoes (the window binds it otherwise) */
    return gdk_keyval_to_lower(keyval) == GDK_KEY_r;
}

/* Ctrl+W w / W / h / l...: focus another panel; Ctrl+W c: close the tab,
 * asking first when it has unsaved changes (nav_keys.h) */
static gboolean handle_window_command(EditorTab* tab, EditorModeState* st, guint keyval,
                                      GdkModifierType state) {
    /* Split view keys (Ctrl held or not): s / S split below, v to the right,
     * o keeps only this view, q quits it (like :q) */
    if ((state & GDK_ALT_MASK) == 0) {
        guint k = gdk_keyval_to_lower(keyval);
        if (k == GDK_KEY_s || k == GDK_KEY_v || k == GDK_KEY_o || k == GDK_KEY_q) {
            st->vim_ctrl_w = FALSE;
            set_echo(tab, "", FALSE);
            if (k == GDK_KEY_o) {
                only_view(tab);
            } else if (k == GDK_KEY_q) {
                gchar* err = NULL;
                if (!close_tab(tab, FALSE, &err)) set_echo(tab, err, TRUE);
                g_free(err);
            } else {
                defer_split(tab, k == GDK_KEY_v ? EDITOR_MOVE_RIGHT : EDITOR_MOVE_DOWN);
            }
            return TRUE;
        }
    }
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_CTRL_W, keyval, state, &next);
    if (action == NAV_PENDING) return TRUE; /* a modifier pressed alone */
    st->vim_ctrl_w = FALSE;
    set_echo(tab, "", FALSE);
    if (action == NAV_PANEL_NEXT) {
        app_emit((AppData*)tab->app, APP_EVENT_FOCUS_NEXT_PANEL);
    } else if (action == NAV_PANEL_PREV) {
        app_emit((AppData*)tab->app, APP_EVENT_FOCUS_PREV_PANEL);
    } else if (action == NAV_TAB_CLOSE) {
        defer_close(tab);
    }
    return TRUE;
}

gboolean vim_cancel_pending(EditorTab* tab) {
    EditorModeState* st = (EditorModeState*)tab->mode_state;
    if (st == NULL) return FALSE;
    gboolean had = st->vim_ctrl_w;
    st->vim_ctrl_w = FALSE;
    if (st->vim != NULL && vim_engine_cancel_pending(st->vim)) had = TRUE;
    if (had) set_echo(tab, "", FALSE);
    return had;
}

/* Normal mode, no command being typed */
static gboolean engine_idle(VimEngine* engine) {
    if (vim_engine_mode(engine) != VIM_MODE_NORMAL) return FALSE;
    gchar* pending = vim_engine_pending_keys(engine);
    gboolean idle = pending == NULL || pending[0] == '\0';
    g_free(pending);
    return idle;
}

gboolean vim_handle_key(EditorTab* tab, EditorModeState* st, guint keyval, GdkModifierType state) {
    if (st->vim == NULL) return FALSE;
    if (st->vim_ctrl_w) return handle_window_command(tab, st, keyval, state);
    gunichar k = vim_key_from_gdk(keyval, state);
    if (k == 0) return FALSE;
    if (k == VIM_KEY_CTRL('w') && engine_idle(st->vim)) {
        st->vim_ctrl_w = TRUE;
        set_echo(tab, "^W", FALSE);
        return TRUE;
    }
    if (st->vim_echo_sticky) set_echo(tab, "", FALSE); /* a listing (:reg) goes away */
    return vim_engine_feed(st->vim, k);
}
