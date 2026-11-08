/* Notes view dialogs: new or edited note and notebook (title, type, icon),
 * confirmations and notices (see notes_private.h) */
#include "notes.h"
#include "notes_private.h"
#include "core/i18n.h"
#include "note_types.h"
#include "notes_store.h"
#include "notebook_icons.h"
#include "theme/icons.h"
#include "icon_picker.h"
#include "theme/palette.h"
#include "dialog.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Modal Dialog Helpers                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* entry;
} SimpleDialogData;

static void on_simple_dlg_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    SimpleDialogData* d = (SimpleDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static GtkWidget* notes_dialog_body(GtkWidget* dlg) {
    GtkWidget* content = app_dialog_get_content_area(APP_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(content), box);
    return box;
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* entry;
    GtkWidget* rb[NOTE_TYPE_COUNT]; /* format radio buttons, by type */
    GtkWidget* preview_label;
} NoteDialogData;

/* Format choices of the note dialog, in display order */
static const struct {
    NoteType type;
    const gchar* label;
} NOTE_FORMATS[] = {
    {NOTE_TYPE_MARKDOWN, N_("Markdown (.md - headings, lists, tables, images)")},
    {NOTE_TYPE_ORG, N_("Org-mode (.org - Emacs tasks, TODOs, checkboxes, folding)")},
    {NOTE_TYPE_TEXT, N_("Plain Text (.txt - unformatted plain text)")},
    {NOTE_TYPE_CANVAS, N_("Canvas (text boxes and pasted pictures placed freely)")},
};

static NoteType note_dialog_type(NoteDialogData* d) {
    for (gint t = 0; t < NOTE_TYPE_COUNT; t++) {
        if (d->rb[t] != NULL && gtk_check_button_get_active(GTK_CHECK_BUTTON(d->rb[t]))) return (NoteType)t;
    }
    return NOTE_TYPE_MARKDOWN;
}

static void update_note_dialog_preview(NoteDialogData* d) {
    if (d->preview_label == NULL || d->entry == NULL) return;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(d->entry));
    gchar* title = (text != NULL) ? g_strstrip(g_strdup(text)) : NULL;
    const gchar* disp_title = (title != NULL && title[0] != '\0') ? title : _("Untitled");

    NoteType type = note_dialog_type(d);
    const gchar* ext = note_type_ext(type);

    gchar* markup = g_markup_printf_escaped("%s <b>%s%s</b>  <span foreground=\"%s\">(%s)</span>",
                                           _("File Preview:"), disp_title, ext, palette_color(PAL_FG_SUBTLE), note_type_name(type));
    gtk_label_set_markup(GTK_LABEL(d->preview_label), markup);
    g_free(markup);
    g_free(title);
}

static void on_note_dlg_field_changed(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    update_note_dialog_preview((NoteDialogData*)user_data);
}

static void on_note_dlg_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    NoteDialogData* d = (NoteDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

/* notebooks: when non-NULL, a "Notebook" drop-down lists them, starts on
 * *io_nb and returns the choice there (moving the note); focus_notebook
 * gives it the focus instead of the title */
gboolean notes_prompt_note(GtkWindow* parent_win, const gchar* title,
                           const gchar* ok_btn_text,
                           const gchar* init_title, NoteType init_type, gboolean existing,
                           GList* notebooks, NotebookItem** io_nb, gboolean focus_notebook,
                           gchar** out_title, NoteType* out_type) {
    *out_title = NULL;
    *out_type = init_type;

    GtkWidget* dlg = app_dialog_new(title, parent_win,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    ok_btn_text ? ok_btn_text : _("_OK"), APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = notes_dialog_body(dlg);

    GtkWidget* lbl = gtk_label_new(_("Note Title:"));
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget* entry = gtk_entry_new();
    if (init_title != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(entry), init_title);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget* nb_dd = NULL;
    GPtrArray* nb_items = NULL;
    if (notebooks != NULL && io_nb != NULL) {
        GtkWidget* nb_lbl = gtk_label_new(_("Notebook:"));
        gtk_widget_set_halign(nb_lbl, GTK_ALIGN_START);
        gtk_widget_set_margin_top(nb_lbl, 6);
        gtk_box_append(GTK_BOX(box), nb_lbl);

        nb_items = g_ptr_array_new();
        GPtrArray* labels = g_ptr_array_new();
        guint init_idx = 0;
        for (GList* l = notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            if (nb == *io_nb) init_idx = nb_items->len;
            g_ptr_array_add(nb_items, nb);
            g_ptr_array_add(labels, nb->name);
        }
        g_ptr_array_add(labels, NULL);
        nb_dd = gtk_drop_down_new_from_strings((const char* const*)labels->pdata);
        g_ptr_array_unref(labels);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(nb_dd), init_idx);
        gtk_widget_set_tooltip_text(nb_dd, _("Notebook the note belongs to: pick another one to move the note"));
        gtk_widget_set_sensitive(nb_dd, nb_items->len > 1);
        gtk_box_append(GTK_BOX(box), nb_dd);
    }

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 6);
    gtk_widget_set_margin_bottom(sep, 6);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget* tlabel = gtk_label_new(_("Select Note Format & Type:"));
    gtk_widget_set_halign(tlabel, GTK_ALIGN_START);
    gtk_widget_add_css_class(tlabel, "dim-label");
    gtk_box_append(GTK_BOX(box), tlabel);

    GtkWidget* type_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(type_vbox, 8);

    GtkWidget* rb[NOTE_TYPE_COUNT] = {NULL};
    GtkWidget* group = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(NOTE_FORMATS); i++) {
        NoteType t = NOTE_FORMATS[i].type;
        rb[t] = gtk_check_button_new_with_label(_(NOTE_FORMATS[i].label));
        if (group != NULL) gtk_check_button_set_group(GTK_CHECK_BUTTON(rb[t]), GTK_CHECK_BUTTON(group));
        group = rb[t];
        /* An existing note keeps the Canvas format, or never takes it: the
         * contents differ (a Canvas exports to Markdown from its menu) */
        if (existing && (t == NOTE_TYPE_CANVAS) != (init_type == NOTE_TYPE_CANVAS)) {
            gtk_widget_set_sensitive(rb[t], FALSE);
        }
        gtk_box_append(GTK_BOX(type_vbox), rb[t]);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rb[init_type < NOTE_TYPE_COUNT ? init_type : NOTE_TYPE_MARKDOWN]),
                                TRUE);
    gtk_box_append(GTK_BOX(box), type_vbox);

    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep2, 6);
    gtk_widget_set_margin_bottom(sep2, 6);
    gtk_box_append(GTK_BOX(box), sep2);

    GtkWidget* preview_label = gtk_label_new(NULL);
    gtk_widget_set_halign(preview_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), preview_label);

    NoteDialogData d = {.loop = g_main_loop_new(NULL, FALSE), .response = APP_RESPONSE_CANCEL, .entry = entry,
                        .preview_label = preview_label};
    memcpy(d.rb, rb, sizeof(rb));

    update_note_dialog_preview(&d);

    g_signal_connect(entry, "changed", G_CALLBACK(on_note_dlg_field_changed), &d);
    for (gint t = 0; t < NOTE_TYPE_COUNT; t++) {
        g_signal_connect(rb[t], "toggled", G_CALLBACK(on_note_dlg_field_changed), &d);
    }
    g_signal_connect(dlg, "response", G_CALLBACK(on_note_dlg_response), &d);

    app_dialog_present(APP_DIALOG(dlg));
    if (focus_notebook && nb_dd != NULL && gtk_widget_get_sensitive(nb_dd)) {
        gtk_widget_grab_focus(nb_dd);
    } else {
        gtk_widget_grab_focus(entry);
        if (init_title != NULL) {
            gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        }
    }

    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    gboolean ok = FALSE;
    if (d.response == APP_RESPONSE_OK) {
        const gchar* raw = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (raw != NULL) {
            gchar* trimmed = g_strstrip(g_strdup(raw));
            if (trimmed[0] != '\0' && strchr(trimmed, '/') == NULL && strchr(trimmed, '\\') == NULL) {
                *out_title = trimmed;
                *out_type = note_dialog_type(&d);
                if (nb_dd != NULL) {
                    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(nb_dd));
                    if (idx < nb_items->len) *io_nb = (NotebookItem*)g_ptr_array_index(nb_items, idx);
                }
                ok = TRUE;
            } else {
                g_free(trimmed);
            }
        }
    }

    if (nb_items != NULL) g_ptr_array_unref(nb_items);
    app_dialog_destroy(APP_DIALOG(dlg));
    return ok;
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* name_entry;
    const gchar* icon;         /* selected NOTEBOOK_ICONS entry */
    GtkWidget* preview_icon;
    GtkWidget* preview_label;
} NotebookDialogData;

static void update_notebook_preview(NotebookDialogData* d) {
    if (d->preview_label == NULL || d->name_entry == NULL) return;
    const gchar* name_text = gtk_editable_get_text(GTK_EDITABLE(d->name_entry));
    gchar* name_trimmed = (name_text != NULL) ? g_strstrip(g_strdup(name_text)) : NULL;
    const gchar* display_name = (name_trimmed != NULL && name_trimmed[0] != '\0') ? name_trimmed : _("Notebook");
    glyph_image_set(d->preview_icon, d->icon);
    gchar* markup = g_markup_printf_escaped("<b>%s</b>", display_name);
    gtk_label_set_markup(GTK_LABEL(d->preview_label), markup);
    g_free(markup);
    g_free(name_trimmed);
}

static void on_notebook_field_changed(GtkEditable* editable, gpointer user_data) {
    (void)editable;
    update_notebook_preview((NotebookDialogData*)user_data);
}

static void on_notebook_icon_changed(const gchar* icon, gpointer user_data) {
    NotebookDialogData* d = (NotebookDialogData*)user_data;
    d->icon = icon;
    update_notebook_preview(d);
}

static void on_notebook_dlg_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    NotebookDialogData* d = (NotebookDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

gboolean notes_prompt_notebook(GtkWindow* parent_win, const gchar* title,
                               const gchar* ok_btn_text,
                               const gchar* init_name, const gchar* init_icon,
                               gchar** out_name, gchar** out_icon) {
    *out_name = NULL;
    *out_icon = NULL;

    GtkWidget* dlg = app_dialog_new(title, parent_win,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    ok_btn_text ? ok_btn_text : _("_OK"), APP_RESPONSE_OK,
                                    NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = notes_dialog_body(dlg);

    NotebookDialogData d;
    memset(&d, 0, sizeof(d));
    d.loop = g_main_loop_new(NULL, FALSE);
    d.response = APP_RESPONSE_CANCEL;

    /* 1. Notebook Name */
    GtkWidget* name_lbl = gtk_label_new(_("Notebook Name:"));
    gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), name_lbl);

    GtkWidget* name_entry = gtk_entry_new();
    if (init_name != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(name_entry), init_name);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    gtk_box_append(GTK_BOX(box), name_entry);
    d.name_entry = name_entry;

    /* 2. Icon: one toggle button per theme icon, 11 per row */
    GtkWidget* icon_lbl = gtk_label_new(_("Notebook Icon:"));
    gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_top(icon_lbl, 6);
    gtk_box_append(GTK_BOX(box), icon_lbl);

    d.icon = notebook_icon_normalize(init_icon);
    GtkWidget* grid = icon_picker_new(NOTEBOOK_ICONS, d.icon, on_notebook_icon_changed, &d);
    gtk_box_append(GTK_BOX(box), grid);

    /* Live Preview: the row as the explorer will show it */
    GtkWidget* prev_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(prev_box, 8);
    gtk_widget_set_margin_bottom(prev_box, 4);
    gtk_box_append(GTK_BOX(prev_box), gtk_label_new(_("Preview:")));
    d.preview_icon = glyph_image_new(d.icon);
    gtk_box_append(GTK_BOX(prev_box), d.preview_icon);
    GtkWidget* prev_lbl = gtk_label_new("");
    gtk_widget_set_halign(prev_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(prev_box), prev_lbl);
    gtk_box_append(GTK_BOX(box), prev_box);
    d.preview_label = prev_lbl;

    g_signal_connect(name_entry, "changed", G_CALLBACK(on_notebook_field_changed), &d);
    g_signal_connect(dlg, "response", G_CALLBACK(on_notebook_dlg_response), &d);

    update_notebook_preview(&d);

    app_dialog_present(APP_DIALOG(dlg));
    gtk_widget_grab_focus(name_entry);
    if (init_name != NULL) {
        gtk_editable_select_region(GTK_EDITABLE(name_entry), 0, -1);
    }

    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    gboolean success = FALSE;
    if (d.response == APP_RESPONSE_OK) {
        const gchar* raw_name = gtk_editable_get_text(GTK_EDITABLE(name_entry));
        gchar* trimmed_name = (raw_name != NULL) ? g_strstrip(g_strdup(raw_name)) : NULL;

        if (trimmed_name != NULL && trimmed_name[0] != '\0' &&
            strchr(trimmed_name, '/') == NULL && strchr(trimmed_name, '\\') == NULL &&
            g_strcmp0(trimmed_name, ".") != 0 && g_strcmp0(trimmed_name, "..") != 0) {
            *out_name = trimmed_name;
            *out_icon = g_strdup(d.icon);
            success = TRUE;
        } else {
            g_free(trimmed_name);
        }
    }

    app_dialog_destroy(APP_DIALOG(dlg));
    return success;
}

gboolean notes_confirm(AppData* app, const gchar* title, const gchar* message,
                       const gchar* ok_label, gboolean destructive) {
    GtkWidget* dlg = app_dialog_new(title, app ? app->window : NULL,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    NULL);
    GtkWidget* ok_btn = app_dialog_add_button(APP_DIALOG(dlg), ok_label, APP_RESPONSE_OK);
    if (destructive) {
        gtk_widget_add_css_class(ok_btn, "destructive-action");
    } else {
        gtk_widget_add_css_class(ok_btn, "suggested-action");
    }
    app_dialog_set_default_response(APP_DIALOG(dlg), destructive ? APP_RESPONSE_CANCEL : APP_RESPONSE_OK);

    GtkWidget* box = notes_dialog_body(dlg);
    GtkWidget* lbl = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 55);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    SimpleDialogData d = { g_main_loop_new(NULL, FALSE), APP_RESPONSE_CANCEL, NULL };
    g_signal_connect(dlg, "response", G_CALLBACK(on_simple_dlg_response), &d);

    app_dialog_present(APP_DIALOG(dlg));
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);
    app_dialog_destroy(APP_DIALOG(dlg));

    return (d.response == APP_RESPONSE_OK);
}

/* Information message with a single Close button */
void notes_notice(AppData* app, const gchar* title, const gchar* message) {
    GtkWidget* dlg = app_dialog_new(title, app ? app->window : NULL,
                                    _("_Close"), APP_RESPONSE_CLOSE, NULL);
    GtkWidget* box = notes_dialog_body(dlg);
    GtkWidget* lbl = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), lbl);
    g_signal_connect(dlg, "response", G_CALLBACK(app_dialog_destroy), NULL);
    app_dialog_present(APP_DIALOG(dlg));
}
