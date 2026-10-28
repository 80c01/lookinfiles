/* Notepad editor mode: find/replace and save-as bars, Windows-style key bindings. */
#include "editor_modes_private.h"
#include "editor.h"
#include <ctype.h>
#include <string.h>

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

/* Build the notepad bars (stored in st) below the editor view */
void notepad_build_ui(EditorTab* tab, EditorModeState* st) {
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
}

gboolean notepad_handle_key(EditorTab* tab, EditorModeState* st,
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
