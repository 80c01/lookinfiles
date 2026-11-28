/* LaTeX keys of the editor tabs (see editor_modes_private.h) */
#include <string.h>

#include "code/code_symbol.h"
#include "core/i18n.h"
#include "editor/editor_modes.h"
#include "editor/editor_modes_private.h"
#include "latex/latex_edit.h"
#include "vim/vim_engine.h"
#include "widgets/dialog.h"

#define VIEW_KEY "editor-latex"
/* One level of indentation inside a new environment (as Tab indents) */
#define INDENT_UNIT "    "

typedef enum {
    PREFIX_NONE = 0,
    PREFIX_C_C,     /* Emacs C-c */
    PREFIX_C_C_C_F  /* Emacs C-c C-f (font commands) */
} LatexPrefix;

/* State of one editor view */
typedef struct {
    EditorTab* tab;
    GtkTextBuffer* buffer; /* referenced */
    /* The name being typed in a \begin / \end, and its partner's */
    GtkTextMark* own_start;
    GtkTextMark* own_end;
    GtkTextMark* other_start;
    GtkTextMark* other_end;
    gboolean linked;
    LatexPrefix prefix;
} LatexView;

/* The partner is being renamed (the views of a file share its buffer) */
static gboolean mirroring = FALSE;
/* An undo or a redo replays edits: their partners' edits come with them */
static gint replaying = 0;
/* Names offered next by the prompts */
static gchar* last_env = NULL;
static gchar* last_command = NULL;

static gboolean is_latex(EditorTab* tab) {
    if (tab == NULL || tab->path == NULL || code_lang_from_name(tab->path) != CODE_LANG_LATEX) return FALSE;
    const gchar* dot = strrchr(tab->path, '.');
    return dot == NULL || g_ascii_strcasecmp(dot, ".bib") != 0;
}

static LatexView* view_of(EditorTab* tab) {
    return tab != NULL && tab->view != NULL ? g_object_get_data(G_OBJECT(tab->view), VIEW_KEY) : NULL;
}

/* ------------------------------------------------------------------------- */
/* Text and places                                                           */
/* ------------------------------------------------------------------------- */

/* The whole text, pictures kept (U+FFFC) so that offsets match the iters */
static gchar* buffer_text(GtkTextBuffer* buffer, gsize* len) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    gchar* text = gtk_text_buffer_get_slice(buffer, &s, &e, TRUE);
    *len = strlen(text);
    return text;
}

static gsize byte_of(const gchar* text, const GtkTextIter* it) {
    return (gsize)(g_utf8_offset_to_pointer(text, gtk_text_iter_get_offset(it)) - text);
}

static void iter_at_byte(GtkTextBuffer* buffer, const gchar* text, gsize byte, GtkTextIter* it) {
    gtk_text_buffer_get_iter_at_offset(buffer, it, (gint)g_utf8_pointer_to_offset(text, text + byte));
}

static void cursor_iter(GtkTextBuffer* buffer, GtkTextIter* it) {
    gtk_text_buffer_get_iter_at_mark(buffer, it, gtk_text_buffer_get_insert(buffer));
}

/* The white space starting the line of it */
static gchar* line_indent(const GtkTextIter* it) {
    GtkTextIter s = *it, e;
    gtk_text_iter_set_line_offset(&s, 0);
    e = s;
    while (!gtk_text_iter_ends_line(&e) && (gtk_text_iter_get_char(&e) == ' ' || gtk_text_iter_get_char(&e) == '\t')) {
        gtk_text_iter_forward_char(&e);
    }
    return gtk_text_iter_get_slice(&s, &e);
}

/* Only white space from the start of the line of it up to it */
static gboolean blank_before(const GtkTextIter* it) {
    GtkTextIter s = *it;
    gtk_text_iter_set_line_offset(&s, 0);
    gchar* before = gtk_text_iter_get_slice(&s, it);
    gboolean blank = g_strstrip(before)[0] == '\0';
    g_free(before);
    return blank;
}

static gboolean blank_after(const GtkTextIter* it) {
    GtkTextIter e = *it;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    gchar* after = gtk_text_iter_get_slice(it, &e);
    gboolean blank = g_strstrip(after)[0] == '\0';
    g_free(after);
    return blank;
}

static void place_cursor(EditorTab* tab, const GtkTextIter* it) {
    gtk_text_buffer_place_cursor(tab->buffer, it);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer));
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                  */
/* ------------------------------------------------------------------------- */

/* \name{selection}, or \name{} with the cursor between the braces */
static void wrap_command(EditorTab* tab, const gchar* name) {
    GtkTextBuffer* b = tab->buffer;
    GtkTextIter s, e;
    gtk_text_buffer_begin_user_action(b);
    if (gtk_text_buffer_get_selection_bounds(b, &s, &e)) {
        gchar* sel = gtk_text_iter_get_slice(&s, &e);
        gchar* text = g_strdup_printf("\\%s{%s}", name, sel);
        gtk_text_buffer_delete(b, &s, &e);
        gtk_text_buffer_insert(b, &s, text, -1);
        place_cursor(tab, &s);
        g_free(text);
        g_free(sel);
    } else {
        cursor_iter(b, &s);
        gchar* text = g_strdup_printf("\\%s{}", name);
        gtk_text_buffer_insert(b, &s, text, -1);
        gtk_text_iter_backward_char(&s);
        place_cursor(tab, &s);
        g_free(text);
    }
    gtk_text_buffer_end_user_action(b);
}

/* The lines of the selection inside \begin{name} ... \end{name}, indented;
 * without a selection, an empty environment at the cursor */
static void insert_env(EditorTab* tab, const gchar* name) {
    GtkTextBuffer* b = tab->buffer;
    GtkTextIter s, e;
    gtk_text_buffer_begin_user_action(b);
    if (gtk_text_buffer_get_selection_bounds(b, &s, &e)) {
        gtk_text_iter_set_line_offset(&s, 0);
        if (gtk_text_iter_starts_line(&e) && gtk_text_iter_compare(&e, &s) > 0) gtk_text_iter_backward_char(&e);
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
        gchar* indent = line_indent(&s);
        gchar* body = gtk_text_iter_get_slice(&s, &e);
        gchar** lines = g_strsplit(body, "\n", -1);
        GString* out = g_string_new(NULL);
        g_string_append_printf(out, "%s\\begin{%s}\n", indent, name);
        for (gchar** l = lines; *l != NULL; l++) {
            if ((*l)[0] != '\0') g_string_append(out, INDENT_UNIT);
            g_string_append_printf(out, "%s\n", *l);
        }
        g_string_append_printf(out, "%s\\end{%s}", indent, name);
        gtk_text_buffer_delete(b, &s, &e);
        GtkTextMark* at = gtk_text_buffer_create_mark(b, NULL, &s, TRUE);
        gtk_text_buffer_insert(b, &s, out->str, -1);
        gtk_text_buffer_get_iter_at_mark(b, &s, at);
        gtk_text_buffer_delete_mark(b, at);
        if (!gtk_text_iter_ends_line(&s)) gtk_text_iter_forward_to_line_end(&s);
        place_cursor(tab, &s);
        g_string_free(out, TRUE);
        g_strfreev(lines);
        g_free(body);
        g_free(indent);
    } else {
        cursor_iter(b, &s);
        gchar* indent = line_indent(&s);
        gboolean break_before = !blank_before(&s);
        gboolean break_after = !blank_after(&s);
        GString* out = g_string_new(NULL);
        if (break_before) g_string_append_printf(out, "\n%s", indent);
        g_string_append_printf(out, "\\begin{%s}\n%s" INDENT_UNIT, name, indent);
        gsize inner = out->len;
        g_string_append_printf(out, "\n%s\\end{%s}", indent, name);
        if (break_after) g_string_append_printf(out, "\n%s", indent);
        gint offset = gtk_text_iter_get_offset(&s) + (gint)g_utf8_strlen(out->str, (gssize)inner);
        gtk_text_buffer_insert(b, &s, out->str, -1);
        gtk_text_buffer_get_iter_at_offset(b, &s, offset);
        place_cursor(tab, &s);
        g_string_free(out, TRUE);
        g_free(indent);
    }
    gtk_text_buffer_end_user_action(b);
}

/* Enter after an open \begin{name}: the new line, then \end{name} */
static gboolean close_on_enter(EditorTab* tab) {
    GtkTextBuffer* b = tab->buffer;
    if (gtk_text_buffer_get_has_selection(b)) return FALSE;
    GtkTextIter cur;
    cursor_iter(b, &cur);
    gsize len = 0;
    gchar* text = buffer_text(b, &len);
    gchar* name = latex_edit_env_to_close(text, len, byte_of(text, &cur));
    g_free(text);
    if (name == NULL) return FALSE;
    gchar* indent = line_indent(&cur);
    gchar* inner = g_strdup_printf("\n%s%s", indent, strchr(indent, '\t') != NULL ? "\t" : INDENT_UNIT);
    gchar* end = g_strdup_printf("\n%s\\end{%s}", indent, name);
    gtk_text_buffer_begin_user_action(b);
    gtk_text_buffer_insert(b, &cur, inner, -1);
    GtkTextMark* at = gtk_text_buffer_create_mark(b, NULL, &cur, TRUE);
    gtk_text_buffer_insert(b, &cur, end, -1);
    gtk_text_buffer_get_iter_at_mark(b, &cur, at);
    gtk_text_buffer_delete_mark(b, at);
    place_cursor(tab, &cur);
    gtk_text_buffer_end_user_action(b);
    g_free(end);
    g_free(inner);
    g_free(indent);
    g_free(name);
    return TRUE;
}

/* Emacs C-c ]: \end{name} of the environment left open around the cursor */
static void close_env(EditorTab* tab) {
    GtkTextBuffer* b = tab->buffer;
    GtkTextIter cur;
    cursor_iter(b, &cur);
    gsize len = 0;
    gchar* text = buffer_text(b, &len);
    LatexEnvToken begin, end;
    gboolean closed = TRUE;
    if (!latex_edit_enclosing(text, len, byte_of(text, &cur), &begin, &end, &closed) || closed) {
        editor_mode_message(tab, "No open environment here");
        g_free(text);
        return;
    }
    GtkTextIter bi;
    iter_at_byte(b, text, begin.start, &bi);
    gchar* indent = line_indent(&bi);
    gchar* name = g_strndup(text + begin.name_start, begin.name_end - begin.name_start);
    gtk_text_buffer_begin_user_action(b);
    if (blank_before(&cur)) {
        GtkTextIter ls = cur;
        gtk_text_iter_set_line_offset(&ls, 0);
        gtk_text_buffer_delete(b, &ls, &cur);
    } else {
        gtk_text_buffer_insert(b, &cur, "\n", -1);
    }
    gchar* out = g_strdup_printf("%s\\end{%s}", indent, name);
    gtk_text_buffer_insert(b, &cur, out, -1);
    place_cursor(tab, &cur);
    gtk_text_buffer_end_user_action(b);
    g_free(out);
    g_free(name);
    g_free(indent);
    g_free(text);
}

typedef enum {
    GO_MATCH, /* the partner of the \begin / \end at the cursor, else the \begin around it */
    GO_BEGIN, /* the \begin around the cursor (again: the one around that) */
    GO_END    /* after the \end around the cursor (again: the one around that) */
} GoTo;

static void go_to(EditorTab* tab, GoTo where) {
    GtkTextBuffer* b = tab->buffer;
    GtkTextIter cur;
    cursor_iter(b, &cur);
    gsize len = 0;
    gchar* text = buffer_text(b, &len);
    gsize at = byte_of(text, &cur);
    LatexEnvToken tok, other, begin, end;
    gboolean closed = FALSE;
    gboolean found = FALSE;
    gsize dest = 0;
    if (where == GO_MATCH && latex_edit_token_at(text, len, at, FALSE, &tok)) {
        found = latex_edit_partner(text, len, &tok, &other);
        dest = other.start;
    } else if (latex_edit_enclosing(text, len, at, &begin, &end, &closed)) {
        if (where != GO_END) {
            found = TRUE;
            dest = begin.start;
        } else if (closed && end.end == at) {
            /* Already there: the end of the environment around this one */
            found = latex_edit_enclosing(text, len, begin.start, &begin, &end, &closed) && closed;
            dest = end.end;
        } else {
            found = closed;
            dest = end.end;
        }
    }
    if (found) {
        GtkTextIter it;
        iter_at_byte(b, text, dest, &it);
        place_cursor(tab, &it);
    } else {
        editor_mode_message(tab, where == GO_END ? "No closed environment here" : "No environment here");
    }
    g_free(text);
}

/* ------------------------------------------------------------------------- */
/* Prompts                                                                   */
/* ------------------------------------------------------------------------- */

static const gchar* default_env(void) {
    return last_env != NULL ? last_env : "itemize";
}

static const gchar* default_command(void) {
    return last_command != NULL ? last_command : "emph";
}

/* Use the name answered (the default when empty): TRUE when valid */
static gboolean take_answer(EditorTab* tab, const gchar* answer, gboolean env) {
    gchar* name = g_strstrip(g_strdup(answer != NULL ? answer : ""));
    if (name[0] == '\\' && !env) memmove(name, name + 1, strlen(name));
    if (name[0] == '\0') {
        g_free(name);
        name = g_strdup(env ? default_env() : default_command());
    }
    if (!latex_edit_valid_name(name, -1)) {
        editor_mode_message(tab, env ? "Not an environment name" : "Not a command name");
        g_free(name);
        return FALSE;
    }
    if (env) {
        insert_env(tab, name);
        g_free(last_env);
        last_env = name;
    } else {
        wrap_command(tab, name);
        g_free(last_command);
        last_command = name;
    }
    return TRUE;
}

static void on_emacs_answer(EditorTab* tab, const gchar* text, gchar* data) {
    take_answer(tab, text, g_strcmp0(data, "env") == 0);
}

static void emacs_ask(EditorTab* tab, gboolean env) {
    gchar* prompt = env ? g_strdup_printf("Environment type (default %s): ", default_env())
                        : g_strdup_printf("Macro (default %s): ", default_command());
    emacs_prompt(tab, prompt, "", on_emacs_answer, g_strdup(env ? "env" : "command"));
    g_free(prompt);
}

/* Notepad: the name in an AppDialog */
static void dialog_ask(EditorTab* tab, gboolean env) {
    AppData* app = tab->app;
    GtkWidget* dlg = app_dialog_new(env ? _("Insert Environment") : _("Wrap in Command"), app->window, NULL, NULL);
    app_dialog_add_button(APP_DIALOG(dlg), _("_Cancel"), APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), env ? _("_Insert") : _("_Wrap"), APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 380);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    GtkWidget* label = gtk_label_new(env ? _("Environment name (itemize, figure, align*...)")
                                         : _("Command name, without the backslash (emph, textbf, underline...)"));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), env ? default_env() : default_command());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gchar* answer = NULL;
    if (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        answer = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    if (answer != NULL) take_answer(tab, answer, env);
    g_free(answer);
}

/* ------------------------------------------------------------------------- */
/* Keys                                                                      */
/* ------------------------------------------------------------------------- */

static gboolean is_enter(guint keyval, GdkModifierType state) {
    return (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
           (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0;
}

static gboolean is_letter(guint keyval, guint lower) {
    return gdk_keyval_to_lower(keyval) == lower;
}

static gboolean notepad_key(EditorTab* tab, EditorModeState* st, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    if (st->np_chord_k) {
        /* Ctrl+K Ctrl+E: environment; Ctrl+K Ctrl+W: command (other second
         * keys stay the chord's) */
        if (!ctrl || alt || (!is_letter(keyval, GDK_KEY_e) && !is_letter(keyval, GDK_KEY_w))) return FALSE;
        st->np_chord_k = FALSE;
        if (st->np_status_label != NULL) gtk_label_set_text(GTK_LABEL(st->np_status_label), "");
        dialog_ask(tab, is_letter(keyval, GDK_KEY_e));
        return TRUE;
    }
    if (is_enter(keyval, state)) return close_on_enter(tab);
    if (ctrl && !shift && !alt && (is_letter(keyval, GDK_KEY_b) || is_letter(keyval, GDK_KEY_i))) {
        wrap_command(tab, is_letter(keyval, GDK_KEY_b) ? "textbf" : "textit");
        return TRUE;
    }
    if (ctrl && shift && !alt && (keyval == GDK_KEY_backslash || keyval == GDK_KEY_bar)) {
        go_to(tab, GO_MATCH);
        return TRUE;
    }
    return FALSE;
}

static gboolean emacs_key(EditorTab* tab, EditorModeState* st, LatexView* lv, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    if (st->emacs_prefix != EMACS_PREFIX_NONE || st->emacs_mini_mode != EMACS_MINI_NONE) return FALSE;
    LatexPrefix prefix = lv != NULL ? lv->prefix : PREFIX_NONE;
    if (lv != NULL) lv->prefix = PREFIX_NONE;
    if (prefix == PREFIX_C_C) {
        if (ctrl && is_letter(keyval, GDK_KEY_e)) {
            emacs_ask(tab, TRUE);
        } else if (ctrl && is_letter(keyval, GDK_KEY_m)) {
            emacs_ask(tab, FALSE);
        } else if (ctrl && is_letter(keyval, GDK_KEY_f)) {
            lv->prefix = PREFIX_C_C_C_F;
            editor_mode_message(tab, "C-c C-f-");
        } else if (!ctrl && !alt && keyval == GDK_KEY_bracketright) {
            close_env(tab);
        } else if (ctrl && is_letter(keyval, GDK_KEY_g)) {
            editor_mode_message(tab, "Quit");
        } else {
            editor_mode_message(tab, "C-c: this key is not a LaTeX command");
        }
        return TRUE;
    }
    if (prefix == PREFIX_C_C_C_F) {
        const gchar* name = !ctrl                         ? NULL
                            : is_letter(keyval, GDK_KEY_b) ? "textbf"
                            : is_letter(keyval, GDK_KEY_i) ? "textit"
                            : is_letter(keyval, GDK_KEY_e) ? "emph"
                            : is_letter(keyval, GDK_KEY_t) ? "texttt"
                                                           : NULL;
        if (name != NULL) {
            wrap_command(tab, name);
        } else {
            editor_mode_message(tab, ctrl && is_letter(keyval, GDK_KEY_g) ? "Quit" : "C-c C-f: C-b, C-i, C-e or C-t");
        }
        return TRUE;
    }
    if (is_enter(keyval, state)) return close_on_enter(tab);
    if (ctrl && !alt && is_letter(keyval, GDK_KEY_c) && lv != NULL) {
        lv->prefix = PREFIX_C_C;
        editor_mode_message(tab, "C-c-");
        return TRUE;
    }
    if (ctrl && alt && (is_letter(keyval, GDK_KEY_a) || is_letter(keyval, GDK_KEY_e))) {
        go_to(tab, is_letter(keyval, GDK_KEY_a) ? GO_BEGIN : GO_END);
        return TRUE;
    }
    return FALSE;
}

static gboolean is_modifier(guint keyval) {
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

gboolean editor_latex_handle_key(EditorTab* tab, guint keyval, GdkModifierType state) {
    if (is_modifier(keyval) || !is_latex(tab) || tab->mode_state == NULL || tab->buffer == NULL) return FALSE;
    EditorModeState* st = tab->mode_state;
    switch (tab->mode) {
        case EDITOR_MODE_NOTEPAD: return notepad_key(tab, st, keyval, state);
        case EDITOR_MODE_EMACS: return emacs_key(tab, st, view_of(tab), keyval, state);
        case EDITOR_MODE_VIM: return vim_is_inserting(tab) && is_enter(keyval, state) && close_on_enter(tab);
        default: return FALSE;
    }
}

gboolean editor_latex_match_pair(EditorTab* tab, GtkTextIter* pos) {
    if (!is_latex(tab) || pos == NULL) return FALSE;
    gsize len = 0;
    gchar* text = buffer_text(tab->buffer, &len);
    LatexEnvToken tok, other;
    gboolean found = latex_edit_token_at(text, len, byte_of(text, pos), FALSE, &tok) &&
                     latex_edit_partner(text, len, &tok, &other);
    if (found) iter_at_byte(tab->buffer, text, other.start, pos);
    g_free(text);
    return found;
}

gboolean editor_latex_env_command(EditorTab* tab, const gchar* args, gchar** error) {
    if (!is_latex(tab)) {
        if (error != NULL) *error = g_strdup(":Env works in LaTeX files only");
        return FALSE;
    }
    if (!take_answer(tab, args, TRUE)) {
        if (error != NULL) *error = g_strdup("E474: Invalid argument");
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Linked names: typing in a \begin{name} or an \end{name} renames the other */
/* ------------------------------------------------------------------------- */

static void set_mark(GtkTextBuffer* b, GtkTextMark** mark, const GtkTextIter* it, gboolean left) {
    if (*mark == NULL) {
        *mark = gtk_text_buffer_create_mark(b, NULL, it, left);
    } else {
        gtk_text_buffer_move_mark(b, *mark, it);
    }
}

static void range_of(GtkTextBuffer* b, GtkTextMark* s, GtkTextMark* e, GtkTextIter* si, GtkTextIter* ei) {
    gtk_text_buffer_get_iter_at_mark(b, si, s);
    gtk_text_buffer_get_iter_at_mark(b, ei, e);
}

/* The range is still the name of an environment: between { and } */
static gboolean between_braces(const GtkTextIter* s, const GtkTextIter* e) {
    GtkTextIter before = *s;
    return gtk_text_iter_backward_char(&before) && gtk_text_iter_get_char(&before) == '{' &&
           gtk_text_iter_get_char(e) == '}';
}

static void on_cursor_moved(GtkTextBuffer* b, GParamSpec* pspec, GtkWidget* view) {
    (void)pspec;
    LatexView* lv = g_object_get_data(G_OBJECT(view), VIEW_KEY);
    if (lv == NULL || mirroring) return;
    GtkTextIter cur, s, e;
    cursor_iter(b, &cur);
    if (lv->linked) {
        range_of(b, lv->own_start, lv->own_end, &s, &e);
        if (gtk_text_iter_in_range(&cur, &s, &e) || gtk_text_iter_equal(&cur, &e)) return;
        lv->linked = FALSE;
    }
    if (!is_latex(lv->tab)) return;
    /* Cheap test first: the line holds a \begin or an \end */
    s = cur;
    gtk_text_iter_set_line_offset(&s, 0);
    e = cur;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    gchar* line = gtk_text_iter_get_slice(&s, &e);
    gboolean maybe = strstr(line, "\\begin") != NULL || strstr(line, "\\end") != NULL;
    g_free(line);
    if (!maybe) return;
    gsize len = 0;
    gchar* text = buffer_text(b, &len);
    LatexEnvToken tok, other;
    if (latex_edit_token_at(text, len, byte_of(text, &cur), TRUE, &tok) &&
        latex_edit_partner(text, len, &tok, &other)) {
        GtkTextIter it;
        iter_at_byte(b, text, tok.name_start, &it);
        set_mark(b, &lv->own_start, &it, TRUE);
        iter_at_byte(b, text, tok.name_end, &it);
        set_mark(b, &lv->own_end, &it, FALSE);
        iter_at_byte(b, text, other.name_start, &it);
        set_mark(b, &lv->other_start, &it, TRUE);
        iter_at_byte(b, text, other.name_end, &it);
        set_mark(b, &lv->other_end, &it, FALSE);
        lv->linked = TRUE;
    }
    g_free(text);
}

/* After an edit at the place at (the inserted text's end, the deletion
 * point): the partner gets the name typed. at is the caller's iterator: it
 * is made valid again after the partner changed. */
static void mirror(GtkTextBuffer* b, GtkWidget* view, GtkTextIter* at) {
    LatexView* lv = g_object_get_data(G_OBJECT(view), VIEW_KEY);
    if (lv == NULL || !lv->linked || mirroring) return;
    EditorModeState* st = lv->tab->mode_state;
    if (replaying > 0 || (st != NULL && vim_engine_undoing(st->vim))) {
        lv->linked = FALSE;
        return;
    }
    GtkTextIter os, oe, ps, pe;
    range_of(b, lv->own_start, lv->own_end, &os, &oe);
    range_of(b, lv->other_start, lv->other_end, &ps, &pe);
    /* An edit elsewhere, or one that broke either name, ends the link */
    if (!(gtk_text_iter_in_range(at, &os, &oe) || gtk_text_iter_equal(at, &oe)) || !between_braces(&os, &oe) ||
        !between_braces(&ps, &pe) || (gtk_text_iter_compare(&oe, &ps) > 0 && gtk_text_iter_compare(&pe, &os) > 0)) {
        lv->linked = FALSE;
        return;
    }
    gchar* own = gtk_text_iter_get_slice(&os, &oe);
    gchar* other = gtk_text_iter_get_slice(&ps, &pe);
    if (strcmp(own, other) != 0) {
        if (own[0] == '\0' || latex_edit_valid_name(own, -1)) {
            mirroring = TRUE;
            GtkTextMark* keep = gtk_text_buffer_create_mark(b, NULL, at, FALSE);
            gtk_text_buffer_delete(b, &ps, &pe);
            gtk_text_buffer_get_iter_at_mark(b, &ps, lv->other_start);
            gtk_text_buffer_insert(b, &ps, own, -1);
            gtk_text_buffer_get_iter_at_mark(b, at, keep);
            gtk_text_buffer_delete_mark(b, keep);
            mirroring = FALSE;
        } else {
            lv->linked = FALSE;
        }
    }
    g_free(other);
    g_free(own);
}

static void on_inserted(GtkTextBuffer* b, GtkTextIter* location, const gchar* text, gint len, GtkWidget* view) {
    (void)text;
    (void)len;
    mirror(b, view, location);
}

static void on_deleted(GtkTextBuffer* b, GtkTextIter* start, GtkTextIter* end, GtkWidget* view) {
    mirror(b, view, start);
    *end = *start;
}

static void on_replay(GtkTextBuffer* b, gpointer data) {
    (void)b;
    replaying += GPOINTER_TO_INT(data);
}

static void latex_view_free(gpointer data) {
    LatexView* lv = data;
    GtkTextMark* marks[] = {lv->own_start, lv->own_end, lv->other_start, lv->other_end};
    for (gsize i = 0; i < G_N_ELEMENTS(marks); i++) {
        if (marks[i] != NULL && !gtk_text_mark_get_deleted(marks[i])) gtk_text_buffer_delete_mark(lv->buffer, marks[i]);
    }
    g_object_unref(lv->buffer);
    g_free(lv);
}

void editor_latex_attach(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL || tab->buffer == NULL || view_of(tab) != NULL) return;
    LatexView* lv = g_new0(LatexView, 1);
    lv->tab = tab;
    lv->buffer = g_object_ref(tab->buffer);
    g_object_set_data_full(G_OBJECT(tab->view), VIEW_KEY, lv, latex_view_free);
    g_signal_connect_object(tab->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), tab->view, 0);
    /* After the edit (the caller's iterators are made valid again), inside
     * its user action: one undo step for both names */
    g_signal_connect_object(tab->buffer, "insert-text", G_CALLBACK(on_inserted), tab->view, G_CONNECT_AFTER);
    g_signal_connect_object(tab->buffer, "delete-range", G_CALLBACK(on_deleted), tab->view, G_CONNECT_AFTER);
    if (g_object_get_data(G_OBJECT(tab->buffer), VIEW_KEY) == NULL) {
        /* Once per buffer: around GTK's undo and redo (Notepad, Emacs) */
        g_object_set_data(G_OBJECT(tab->buffer), VIEW_KEY, GINT_TO_POINTER(1));
        g_signal_connect(tab->buffer, "undo", G_CALLBACK(on_replay), GINT_TO_POINTER(1));
        g_signal_connect_after(tab->buffer, "undo", G_CALLBACK(on_replay), GINT_TO_POINTER(-1));
        g_signal_connect(tab->buffer, "redo", G_CALLBACK(on_replay), GINT_TO_POINTER(1));
        g_signal_connect_after(tab->buffer, "redo", G_CALLBACK(on_replay), GINT_TO_POINTER(-1));
    }
}
