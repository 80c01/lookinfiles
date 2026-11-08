/* Notepad editor mode: find/replace, go to line and save-as bars, Windows-style key bindings. */
#include "editor/editor_modes_private.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "editor/editor.h"
#include "code_nav.h"
#include "code_help.h"
#include "code_refs.h"
#include "code_symbols.h"
#include "editor/editor_tagbar.h"
#include "core/app_events.h"
#include "theme/text_tags.h"
#include <ctype.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Find and replace                                                          */
/* ------------------------------------------------------------------------- */

/* While the find bar is open, matches are tagged (palette roles editor_match
 * and editor_match_current) rather than selected: the selection of an
 * unfocused view is too faint to see. Closing the bar selects the match. */
#define NP_MATCH_TAG "np-search-match"
#define NP_CURRENT_TAG "np-search-current"
#define NP_MAX_TAGGED 10000
#define NP_FLAGS (GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE)

static EditorModeState* np_state(EditorTab* tab) {
    return tab != NULL ? tab->mode_state : NULL;
}

static gboolean np_find_open(EditorModeState* st) {
    return st->np_find_bar != NULL && gtk_widget_get_visible(st->np_find_bar);
}

static const gchar* np_query(EditorModeState* st) {
    return gtk_editable_get_text(GTK_EDITABLE(st->np_find_entry));
}

static void np_status(EditorModeState* st, const gchar* msg) {
    gtk_label_set_text(GTK_LABEL(st->np_status_label), msg != NULL ? msg : "");
}

static void np_not_found(EditorModeState* st) {
    gchar* msg = g_strdup_printf(_("Cannot find \"%s\""), np_query(st));
    np_status(st, msg);
    g_free(msg);
}

static GtkTextTag* np_tag(GtkTextBuffer* b, const gchar* name, const gchar* bg) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(b);
    GtkTextTag* t = text_tag_ensure(b, name, "background", bg, NULL);
    /* Above the syntax highlighting */
    gtk_text_tag_set_priority(t, gtk_text_tag_table_get_size(table) - 1);
    return t;
}

static void np_clear_tags(EditorTab* tab) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(tab->buffer);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    const gchar* names[] = {NP_MATCH_TAG, NP_CURRENT_TAG};
    for (gsize i = 0; i < G_N_ELEMENTS(names); i++) {
        GtkTextTag* t = gtk_text_tag_table_lookup(table, names[i]);
        if (t != NULL) gtk_text_buffer_remove_tag(tab->buffer, t, &s, &e);
    }
}

/* Tag every match of the query and the current one */
static void np_highlight(EditorTab* tab, EditorModeState* st) {
    np_clear_tags(tab);
    const gchar* q = np_query(st);
    if (q[0] == '\0') return;
    GtkTextTag* all = np_tag(tab->buffer, NP_MATCH_TAG, "@editor_match");
    GtkTextIter it, s, e;
    gtk_text_buffer_get_start_iter(tab->buffer, &it);
    for (gint n = 0; n < NP_MAX_TAGGED && gtk_text_iter_forward_search(&it, q, NP_FLAGS, &s, &e, NULL); n++) {
        gtk_text_buffer_apply_tag(tab->buffer, all, &s, &e);
        it = e;
    }
    if (st->np_ms >= 0) {
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &s, st->np_ms);
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &e, st->np_me);
        gtk_text_buffer_apply_tag(tab->buffer, np_tag(tab->buffer, NP_CURRENT_TAG, "@editor_match_current"), &s, &e);
    }
}

/* Show the match found: tagged while the bar is open (the cursor at its
 * start), selected otherwise */
static void np_show_match(EditorTab* tab, EditorModeState* st, GtkTextIter* s, GtkTextIter* e) {
    if (np_find_open(st)) {
        st->np_ms = gtk_text_iter_get_offset(s);
        st->np_me = gtk_text_iter_get_offset(e);
        gtk_text_buffer_place_cursor(tab->buffer, s);
        np_highlight(tab, st);
    } else {
        gtk_text_buffer_select_range(tab->buffer, e, s);
    }
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), s, 0.1, FALSE, 0.0, 0.0);
}

/* Search the query from `from` (a match may start there going forward, end
 * there going backward), around the end of the text */
static gboolean np_search_from(EditorTab* tab, EditorModeState* st, GtkTextIter* from, gboolean forward) {
    const gchar* q = np_query(st);
    if (q[0] == '\0') return FALSE;
    GtkTextIter s, e, edge;
    gboolean found;
    if (forward) {
        found = gtk_text_iter_forward_search(from, q, NP_FLAGS, &s, &e, NULL);
        if (!found) {
            gtk_text_buffer_get_start_iter(tab->buffer, &edge);
            found = gtk_text_iter_forward_search(&edge, q, NP_FLAGS, &s, &e, NULL);
        }
    } else {
        found = gtk_text_iter_backward_search(from, q, NP_FLAGS, &s, &e, NULL);
        if (!found) {
            gtk_text_buffer_get_end_iter(tab->buffer, &edge);
            found = gtk_text_iter_backward_search(&edge, q, NP_FLAGS, &s, &e, NULL);
        }
    }
    if (!found) {
        st->np_ms = st->np_me = -1;
        if (np_find_open(st)) np_highlight(tab, st);
        np_not_found(st);
        return FALSE;
    }
    np_show_match(tab, st, &s, &e);
    np_status(st, "");
    return TRUE;
}

/* Find Next / Find Previous (F3 / Shift+F3): from the cursor, past the
 * current match or selection */
static gboolean np_find_again(EditorTab* tab, EditorModeState* st, gboolean forward) {
    GtkTextIter s, e, from;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &s, &e);
    from = forward ? e : s;
    if (forward && !has_sel && st->np_ms >= 0 && gtk_text_iter_get_offset(&from) == st->np_ms) {
        gtk_text_iter_forward_char(&from);
    }
    return np_search_from(tab, st, &from, forward);
}

/* Typing in the find entry: first match from where the search started */
static void on_np_find_changed(GtkEditable* entry, gpointer user_data) {
    (void)entry;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    if (st == NULL || st->np_setting || !np_find_open(st)) return;
    GtkTextIter origin;
    gtk_text_buffer_get_iter_at_offset(tab->buffer, &origin, st->np_origin);
    if (np_query(st)[0] == '\0') {
        st->np_ms = st->np_me = -1;
        np_clear_tags(tab);
        gtk_text_buffer_place_cursor(tab->buffer, &origin);
        np_status(st, "");
        return;
    }
    if (!np_search_from(tab, st, &origin, TRUE)) {
        gtk_text_buffer_place_cursor(tab->buffer, &origin);
    }
}

/* The text changed under the bar (typing in the view): tags again */
static void on_np_buffer_changed(GtkTextBuffer* buffer, gpointer user_data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    if (st == NULL || st->np_replacing || !np_find_open(st)) return;
    st->np_ms = st->np_me = -1;
    np_highlight(tab, st);
}

static void on_np_find_next_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    np_find_again(tab, np_state(tab), TRUE);
}

static void on_np_find_prev_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    np_find_again(tab, np_state(tab), FALSE);
}

/* The match to replace: the current one, else a selection equal to the query */
static gboolean np_replace_target(EditorTab* tab, EditorModeState* st, GtkTextIter* s, GtkTextIter* e) {
    if (st->np_ms >= 0) {
        gtk_text_buffer_get_iter_at_offset(tab->buffer, s, st->np_ms);
        gtk_text_buffer_get_iter_at_offset(tab->buffer, e, st->np_me);
    } else if (!gtk_text_buffer_get_selection_bounds(tab->buffer, s, e)) {
        return FALSE;
    }
    GtkTextIter ms, me;
    return gtk_text_iter_forward_search(s, np_query(st), NP_FLAGS, &ms, &me, e) &&
           gtk_text_iter_equal(&ms, s) && gtk_text_iter_equal(&me, e);
}

static void on_np_replace_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    if (np_query(st)[0] == '\0') return;
    GtkTextIter s, e;
    if (!np_replace_target(tab, st, &s, &e)) {
        /* Nothing to replace yet: find it first, as Notepad does */
        np_find_again(tab, st, TRUE);
        return;
    }
    const gchar* r = gtk_editable_get_text(GTK_EDITABLE(st->np_replace_entry));
    st->np_replacing = TRUE;
    gtk_text_buffer_begin_user_action(tab->buffer);
    gtk_text_buffer_delete(tab->buffer, &s, &e);
    gtk_text_buffer_insert(tab->buffer, &s, r, -1);
    gtk_text_buffer_end_user_action(tab->buffer);
    st->np_replacing = FALSE;
    st->np_ms = st->np_me = -1;
    gtk_text_buffer_place_cursor(tab->buffer, &s);
    if (np_search_from(tab, st, &s, TRUE)) {
        np_status(st, _("Occurrence replaced"));
    }
}

static void on_np_replace_all_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    const gchar* q = np_query(st);
    if (q[0] == '\0') return;
    const gchar* r = gtk_editable_get_text(GTK_EDITABLE(st->np_replace_entry));
    gint count = 0;
    GtkTextIter it, s, e;
    st->np_replacing = TRUE;
    gtk_text_buffer_begin_user_action(tab->buffer);
    gtk_text_buffer_get_start_iter(tab->buffer, &it);
    while (gtk_text_iter_forward_search(&it, q, NP_FLAGS, &s, &e, NULL)) {
        gtk_text_buffer_delete(tab->buffer, &s, &e);
        gtk_text_buffer_insert(tab->buffer, &s, r, -1); /* s moves past the new text */
        it = s;
        count++;
    }
    gtk_text_buffer_end_user_action(tab->buffer);
    st->np_replacing = FALSE;
    st->np_ms = st->np_me = -1;
    np_highlight(tab, st);
    if (count == 0) {
        np_not_found(st);
    } else {
        gchar* msg = g_strdup_printf(ngettext("Replaced %d occurrence", "Replaced %d occurrences", count), count);
        np_status(st, msg);
        g_free(msg);
    }
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
    EditorModeState* st = np_state(tab);
    if (st == NULL) return;
    GtkTextIter s, e;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(tab->buffer, &s, &e);
    st->np_origin = gtk_text_iter_get_offset(&s);
    st->np_ms = st->np_me = -1;
    /* A selection on one line becomes the text to find */
    if (has_sel && gtk_text_iter_get_line(&s) == gtk_text_iter_get_line(&e) &&
        gtk_text_iter_get_offset(&e) - gtk_text_iter_get_offset(&s) <= 200) {
        gchar* sel = gtk_text_buffer_get_text(tab->buffer, &s, &e, FALSE);
        st->np_setting = TRUE;
        gtk_editable_set_text(GTK_EDITABLE(st->np_find_entry), sel);
        st->np_setting = FALSE;
        g_free(sel);
    }
    gtk_widget_set_visible(st->np_goto_bar, FALSE);
    gtk_widget_set_visible(st->np_find_bar, TRUE);
    gtk_widget_set_visible(st->np_replace_box, is_replace);
    np_highlight(tab, st);
    np_status(st, "");
    /* The focus goes to the text to find, selected: typing replaces it */
    gtk_widget_grab_focus(st->np_find_entry);
    gtk_editable_select_region(GTK_EDITABLE(st->np_find_entry), 0, -1);
}

/* Close the find bar; the current match becomes the selection */
static void np_close_find(EditorTab* tab, EditorModeState* st) {
    if (!np_find_open(st)) return;
    gtk_widget_set_visible(st->np_find_bar, FALSE);
    np_clear_tags(tab);
    if (st->np_ms >= 0) {
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &s, st->np_ms);
        gtk_text_buffer_get_iter_at_offset(tab->buffer, &e, st->np_me);
        gtk_text_buffer_select_range(tab->buffer, &e, &s);
    }
    st->np_ms = st->np_me = -1;
}

void notepad_hide_find(EditorTab* tab) {
    EditorModeState* st = np_state(tab);
    if (st == NULL) return;
    np_close_find(tab, st);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

/* Keys of the find and replace entries: Enter (Shift: previous), F3 /
 * Shift+F3, Escape */
static gboolean on_np_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data) {
    (void)keycode;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    GtkWidget* entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean mods = (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0;
    gboolean enter = keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter;
    if (mods) return FALSE;
    if (keyval == GDK_KEY_Escape) {
        notepad_hide_find(tab);
        return TRUE;
    }
    if (keyval == GDK_KEY_F3 || (enter && (shift || entry == st->np_find_entry))) {
        np_find_again(tab, st, !shift);
        return TRUE;
    }
    if (enter) { /* the replace entry */
        on_np_replace_clicked(NULL, tab);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Go to line and Save As bars                                               */
/* ------------------------------------------------------------------------- */

static void notepad_show_goto(EditorTab* tab) {
    EditorModeState* st = np_state(tab);
    if (st == NULL) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    gchar* line = g_strdup_printf("%d", gtk_text_iter_get_line(&it) + 1);
    gtk_editable_set_text(GTK_EDITABLE(st->np_goto_entry), line);
    g_free(line);
    np_close_find(tab, st);
    gtk_widget_set_visible(st->np_goto_bar, TRUE);
    gtk_widget_grab_focus(st->np_goto_entry);
    gtk_editable_select_region(GTK_EDITABLE(st->np_goto_entry), 0, -1);
}

static void notepad_hide_goto(EditorTab* tab) {
    EditorModeState* st = np_state(tab);
    if (st == NULL) return;
    gtk_widget_set_visible(st->np_goto_bar, FALSE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

static void on_np_goto_confirm(GtkWidget* w, gpointer user_data) {
    (void)w;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = np_state(tab);
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(st->np_goto_entry));
    gchar* end = NULL;
    gint64 line = g_ascii_strtoll(text, &end, 10);
    while (end != NULL && g_ascii_isspace(*end)) end++;
    if (end == text || (end != NULL && *end != '\0') || line < 1) {
        np_status(st, _("Type a line number"));
        return;
    }
    gint total = gtk_text_buffer_get_line_count(tab->buffer);
    if (line > total) {
        gchar* msg = g_strdup_printf(ngettext("The file has %d line", "The file has %d lines", total), total);
        np_status(st, msg);
        g_free(msg);
        return;
    }
    notepad_hide_goto(tab);
    editor_mode_goto_line(tab, (gint)line);
    np_status(st, "");
}

static gboolean on_np_bar_entry_escape(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                                       GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    EditorTab* tab = (EditorTab*)user_data;
    if (keyval != GDK_KEY_Escape) return FALSE;
    notepad_hide_goto(tab);
    notepad_hide_save_as(tab);
    return TRUE;
}

static void on_np_save_as_confirm(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorTab* tab = (EditorTab*)user_data;
    EditorModeState* st = tab->mode_state;
    const gchar* path = gtk_editable_get_text(GTK_EDITABLE(st->np_save_entry));
    if (path && path[0] != '\0') {
        editor_save_tab_as(tab->app, tab, path);
    }
    notepad_hide_save_as(tab);
}

void notepad_show_save_as(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    gtk_editable_set_text(GTK_EDITABLE(st->np_save_entry), tab->name ? tab->name : "untitled.txt");
    gtk_widget_set_visible(st->np_save_bar, TRUE);
    gtk_widget_grab_focus(st->np_save_entry);
}

void notepad_hide_save_as(EditorTab* tab) {
    if (tab == NULL || tab->mode_state == NULL) return;
    EditorModeState* st = tab->mode_state;
    gtk_widget_set_visible(st->np_save_bar, FALSE);
    if (tab->view) gtk_widget_grab_focus(tab->view);
}

void notepad_reset(EditorTab* tab) {
    EditorModeState* st = np_state(tab);
    if (st == NULL || st->np_find_bar == NULL) return;
    np_close_find(tab, st);
    gtk_widget_set_visible(st->np_goto_bar, FALSE);
    gtk_widget_set_visible(st->np_save_bar, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Bars                                                                      */
/* ------------------------------------------------------------------------- */

static void np_add_entry_keys(GtkWidget* entry, GCallback cb, EditorTab* tab) {
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", cb, tab);
    gtk_widget_add_controller(entry, keys);
}

static GtkWidget* np_button(const gchar* label, const gchar* tooltip) {
    GtkWidget* btn = gtk_button_new_with_label(label);
    if (tooltip != NULL) gtk_widget_set_tooltip_text(btn, tooltip);
    return btn;
}

/* Build the notepad bars (stored in st) below the editor view */
void notepad_build_ui(EditorTab* tab, EditorModeState* st) {
    st->np_ms = st->np_me = -1;
    GtkWidget* np_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Notepad Find & Replace bar */
    GtkWidget* find_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(find_bar, "notepad-bar");
    gtk_widget_set_visible(find_bar, FALSE);

    /* Row 1: Find */
    GtkWidget* row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_f = gtk_label_new(_("Find:"));
    st->np_find_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_find_entry, TRUE);
    GtkWidget* btn_fn = np_button(_("Find Next"), _("Find Next (Enter / F3)"));
    GtkWidget* btn_fp = np_button(_("Find Prev"), _("Find Previous (Shift+Enter / Shift+F3)"));
    GtkWidget* btn_fc = icon_button_new("window-close", _("Close Find Bar (Esc)"));

    g_signal_connect(btn_fn, "clicked", G_CALLBACK(on_np_find_next_clicked), tab);
    g_signal_connect(btn_fp, "clicked", G_CALLBACK(on_np_find_prev_clicked), tab);
    g_signal_connect_swapped(btn_fc, "clicked", G_CALLBACK(notepad_hide_find), tab);
    g_signal_connect(st->np_find_entry, "changed", G_CALLBACK(on_np_find_changed), tab);
    np_add_entry_keys(st->np_find_entry, G_CALLBACK(on_np_entry_key), tab);

    gtk_box_append(GTK_BOX(row1), lbl_f);
    gtk_box_append(GTK_BOX(row1), st->np_find_entry);
    gtk_box_append(GTK_BOX(row1), btn_fn);
    gtk_box_append(GTK_BOX(row1), btn_fp);
    gtk_box_append(GTK_BOX(row1), btn_fc);
    gtk_box_append(GTK_BOX(find_bar), row1);

    /* Row 2: Replace */
    GtkWidget* row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_r = gtk_label_new(_("Replace:"));
    st->np_replace_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_replace_entry, TRUE);
    GtkWidget* btn_rep = np_button(_("Replace"), _("Replace the match and find the next one (Enter)"));
    GtkWidget* btn_rep_all = np_button(_("Replace All"), _("Replace every match"));

    g_signal_connect(btn_rep, "clicked", G_CALLBACK(on_np_replace_clicked), tab);
    g_signal_connect(btn_rep_all, "clicked", G_CALLBACK(on_np_replace_all_clicked), tab);
    np_add_entry_keys(st->np_replace_entry, G_CALLBACK(on_np_entry_key), tab);

    gtk_box_append(GTK_BOX(row2), lbl_r);
    gtk_box_append(GTK_BOX(row2), st->np_replace_entry);
    gtk_box_append(GTK_BOX(row2), btn_rep);
    gtk_box_append(GTK_BOX(row2), btn_rep_all);

    /* Formatting quick buttons in notepad bar */
    GtkWidget* btn_al = np_button(_("Left"), _("Align left (Ctrl+L)"));
    g_object_set_data((GObject*)btn_al, "align-type", (gpointer)"left");
    g_signal_connect(btn_al, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);
    GtkWidget* btn_ac = np_button(_("Center"), _("Center (Ctrl+E)"));
    g_object_set_data((GObject*)btn_ac, "align-type", (gpointer)"center");
    g_signal_connect(btn_ac, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);
    GtkWidget* btn_ar = np_button(_("Right"), _("Align right (Ctrl+R)"));
    g_object_set_data((GObject*)btn_ar, "align-type", (gpointer)"right");
    g_signal_connect(btn_ar, "clicked", G_CALLBACK(on_np_align_btn_clicked), tab);

    gtk_box_append(GTK_BOX(row2), btn_al);
    gtk_box_append(GTK_BOX(row2), btn_ac);
    gtk_box_append(GTK_BOX(row2), btn_ar);

    st->np_replace_box = row2;
    gtk_box_append(GTK_BOX(find_bar), row2);
    st->np_find_bar = find_bar;
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_np_buffer_changed), tab);

    /* Go to line bar */
    GtkWidget* goto_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(goto_bar, "notepad-bar");
    gtk_widget_set_visible(goto_bar, FALSE);
    GtkWidget* lbl_g = gtk_label_new(_("Go to line:"));
    st->np_goto_entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(st->np_goto_entry), 8);
    gtk_entry_set_input_purpose(GTK_ENTRY(st->np_goto_entry), GTK_INPUT_PURPOSE_DIGITS);
    GtkWidget* btn_go = np_button(_("Go To"), _("Go to the line (Enter)"));
    GtkWidget* btn_gc = icon_button_new("window-close", _("Close (Esc)"));
    g_signal_connect(btn_go, "clicked", G_CALLBACK(on_np_goto_confirm), tab);
    g_signal_connect_swapped(btn_gc, "clicked", G_CALLBACK(notepad_hide_goto), tab);
    g_signal_connect(st->np_goto_entry, "activate", G_CALLBACK(on_np_goto_confirm), tab);
    np_add_entry_keys(st->np_goto_entry, G_CALLBACK(on_np_bar_entry_escape), tab);
    gtk_box_append(GTK_BOX(goto_bar), lbl_g);
    gtk_box_append(GTK_BOX(goto_bar), st->np_goto_entry);
    gtk_box_append(GTK_BOX(goto_bar), btn_go);
    gtk_box_append(GTK_BOX(goto_bar), btn_gc);
    st->np_goto_bar = goto_bar;

    /* Save As Bar */
    GtkWidget* save_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(save_bar, "notepad-bar");
    gtk_widget_set_visible(save_bar, FALSE);
    GtkWidget* lbl_sa = gtk_label_new(_("Save As:"));
    st->np_save_entry = gtk_entry_new();
    gtk_widget_set_hexpand(st->np_save_entry, TRUE);
    GtkWidget* btn_sa_save = gtk_button_new_with_label(_("Save"));
    GtkWidget* btn_sa_canc = gtk_button_new_with_label(_("Cancel"));

    g_signal_connect(btn_sa_save, "clicked", G_CALLBACK(on_np_save_as_confirm), tab);
    g_signal_connect_swapped(btn_sa_canc, "clicked", G_CALLBACK(notepad_hide_save_as), tab);
    g_signal_connect(st->np_save_entry, "activate", G_CALLBACK(on_np_save_as_confirm), tab);
    np_add_entry_keys(st->np_save_entry, G_CALLBACK(on_np_bar_entry_escape), tab);

    gtk_box_append(GTK_BOX(save_bar), lbl_sa);
    gtk_box_append(GTK_BOX(save_bar), st->np_save_entry);
    gtk_box_append(GTK_BOX(save_bar), btn_sa_save);
    gtk_box_append(GTK_BOX(save_bar), btn_sa_canc);
    st->np_save_bar = save_bar;

    /* Notepad Status Bar */
    GtkWidget* np_status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(np_status, "notepad-statusbar");
    st->np_file_label = gtk_label_new("");
    editor_bar_label_shrinkable(st->np_file_label, PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(st->np_file_label), 0.0f);
    st->np_status_label = gtk_label_new(_("Ready"));
    editor_bar_label_shrinkable(st->np_status_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->np_status_label), 0.0f);
    gtk_widget_set_hexpand(st->np_status_label, TRUE);

    st->np_info_label = gtk_label_new("Ln 1, Col 1   |   100%   |   Unix (LF)   |   UTF-8");
    editor_bar_label_shrinkable(st->np_info_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->np_info_label), 1.0f);

    gtk_box_append(GTK_BOX(np_status), st->np_file_label);
    gtk_box_append(GTK_BOX(np_status), st->np_status_label);
    gtk_box_append(GTK_BOX(np_status), st->np_info_label);

    gtk_box_append(GTK_BOX(np_box), st->np_find_bar);
    gtk_box_append(GTK_BOX(np_box), st->np_goto_bar);
    gtk_box_append(GTK_BOX(np_box), st->np_save_bar);
    gtk_box_append(GTK_BOX(np_box), np_status);
    st->np_box = np_box;
}

static gboolean is_modifier_keyval(guint keyval) {
    return keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R || keyval == GDK_KEY_Shift_L ||
           keyval == GDK_KEY_Shift_R || keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
           keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R || keyval == GDK_KEY_ISO_Level3_Shift;
}

gboolean notepad_handle_key(EditorTab* tab, EditorModeState* st,
                                   guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    if (st->np_chord_k && !is_modifier_keyval(keyval)) {
        /* Ctrl+K Ctrl+I: help card; Ctrl+K Ctrl+O: tagbar */
        st->np_chord_k = FALSE;
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "");
        if (ctrl && (keyval == GDK_KEY_i || keyval == GDK_KEY_I)) {
            code_help_show(tab->app, tab);
        } else if (ctrl && (keyval == GDK_KEY_o || keyval == GDK_KEY_O)) {
            editor_tagbar_toggle(tab->app, tab, editor_mode_message);
        } else {
            gtk_label_set_text(GTK_LABEL(st->np_status_label), "Ctrl+K: no command for this key");
        }
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        notepad_show_find(tab, FALSE);
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        notepad_show_find(tab, TRUE);
        return TRUE;
    }
    if (keyval == GDK_KEY_F6 && !ctrl) {
        /* F6 / Shift+F6: next / previous panel */
        app_emit(tab->app, shift ? APP_EVENT_FOCUS_PREV_PANEL : APP_EVENT_FOCUS_NEXT_PANEL);
        return TRUE;
    }
    if (keyval == GDK_KEY_F3 && !ctrl) {
        /* F3 / Shift+F3: next / previous match; nothing to find yet opens the bar */
        if (np_query(st)[0] == '\0') {
            notepad_show_find(tab, FALSE);
        } else {
            np_find_again(tab, st, !shift);
        }
        return TRUE;
    }
    if (ctrl && !shift && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        if (tab->path != NULL && tab->path[0] != '\0') {
            editor_save_current(tab->app);
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
        notepad_show_goto(tab);
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
    /* Code navigation (VS Code keys) */
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    if (ctrl && !shift && !alt && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        st->np_chord_k = TRUE;
        gtk_label_set_text(GTK_LABEL(st->np_status_label), "Ctrl+K was pressed: waiting for the second key");
        return TRUE;
    }
    if (ctrl && shift && !alt && (keyval == GDK_KEY_o || keyval == GDK_KEY_O)) {
        code_symbols_show(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (ctrl && shift && !alt && keyval == GDK_KEY_space) {
        code_help_signature(tab->app, tab);
        return TRUE;
    }
    if (keyval == GDK_KEY_F12 && shift && !ctrl && !alt) {
        code_refs_find(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (keyval == GDK_KEY_F12 && !shift && !alt) {
        code_nav_goto_definition(tab->app, tab, ctrl, editor_mode_message);
        return TRUE;
    }
    if (ctrl && alt && !shift && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
        code_nav_back(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (ctrl && shift && !alt && (keyval == GDK_KEY_underscore || keyval == GDK_KEY_minus)) {
        code_nav_forward(tab->app, tab, editor_mode_message);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape) {
        notepad_hide_find(tab);
        notepad_hide_goto(tab);
        notepad_hide_save_as(tab);
        return TRUE;
    }
    return FALSE;
}
