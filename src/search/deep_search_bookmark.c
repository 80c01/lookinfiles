/* Saved Deep Search bookmarks (see deep_search_bookmark.h) */
#include "search/deep_search_bookmark.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "explorer/bookmark_dialog.h"
#include "explorer/bookmarks.h"
#include "search/deep_search_store.h"
#include "search/deep_search_tab.h"
#include "widgets/dialog.h"

typedef struct {
    GtkWidget* hint;
    gchar* shown_folder; /* bookmark_display_location() of saved folder, or NULL */
} SaveHint;

static void update_hint(GtkCheckButton* check, gpointer data) {
    SaveHint* h = data;
    if (gtk_check_button_get_active(check) || h->shown_folder == NULL) {
        gtk_label_set_text(GTK_LABEL(h->hint),
                           _("Searches the explorer's current folder. Only the criteria are saved; "
                             "opening the bookmark runs the search."));
    } else {
        /* TRANSLATORS: %s is a folder path */
        gchar* text = g_strdup_printf(_("Searches %s. Only the criteria are saved; "
                                        "opening the bookmark runs the search."),
                                      h->shown_folder);
        gtk_label_set_text(GTK_LABEL(h->hint), text);
        g_free(text);
    }
}

static GtkWidget* field_label(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

void deep_search_bookmark_save(AppData* app) {
    if (!bookmarks_loaded(app)) return;
    SavedDeepSearch saved;
    if (!deep_search_tab_read_saved(app, &saved)) return;

    gchar* initial = saved_deep_search_default_name(&saved);
    guint def_folder = 0;
    gchar** folders = bookmarks_folder_names(app, &def_folder);

    GtkWidget* dlg = app_dialog_new(_("Save Search as Bookmark"), app->window, NULL, NULL);
    app_dialog_add_button(APP_DIALOG(dlg), _("_Cancel"), APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), _("_Save"), APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 480);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    gtk_box_append(GTK_BOX(box), field_label(_("Name")));
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), initial);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    gtk_box_append(GTK_BOX(box), field_label(_("Folder")));
    GtkWidget* folder_dd = gtk_drop_down_new_from_strings((const gchar* const*)folders);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(folder_dd), def_folder);
    gtk_box_append(GTK_BOX(box), folder_dd);

    GtkWidget* cur_check = gtk_check_button_new_with_label(_("Search the explorer's current folder when opened"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cur_check), saved.use_current_folder);
    if (saved.folder == NULL) gtk_widget_set_sensitive(cur_check, FALSE);
    gtk_box_append(GTK_BOX(box), cur_check);

    GtkWidget* hint = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 60);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);

    SaveHint sh = {hint, saved.folder != NULL ? bookmark_display_location(saved.folder) : NULL};
    update_hint(GTK_CHECK_BUTTON(cur_check), &sh);
    g_signal_connect(cur_check, "toggled", G_CALLBACK(update_hint), &sh);

    gchar* name = NULL;
    guint folder_idx = def_folder;
    gboolean use_current = saved.use_current_folder;
    if (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        if (name[0] == '\0') {
            g_free(name);
            name = g_strdup(initial);
        }
        folder_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(folder_dd));
        use_current = gtk_check_button_get_active(GTK_CHECK_BUTTON(cur_check));
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    g_free(sh.shown_folder);
    g_strfreev(folders);
    g_free(initial);

    if (name != NULL) {
        saved.use_current_folder = use_current;
        if (use_current) g_clear_pointer(&saved.folder, g_free);
        gchar* id = deep_search_store_new_id();
        gchar* path = deep_search_store_path(id);
        gchar* target = deep_search_target_make(id);
        GError* err = NULL;
        if (path != NULL && target != NULL && deep_search_store_save(&saved, path, &err)) {
            bookmarks_add_fixed_at(app, name, target, folder_idx);
            /* TRANSLATORS: %s is the bookmark name */
            gchar* notice = g_strdup_printf(_("Saved search “%s” to bookmarks"), name);
            app_notice(app, notice, FALSE);
            g_free(notice);
        } else {
            gchar* msg = g_strdup_printf(_("Could not save the search:\n%s"),
                                         err != NULL ? err->message : _("unknown error"));
            bookmark_message(app, _("Save Search"), msg, _("_Close"), NULL);
            g_free(msg);
        }
        g_clear_error(&err);
        g_free(target);
        g_free(path);
        g_free(id);
        g_free(name);
    }
    saved_deep_search_clear(&saved);
}

void deep_search_bookmark_open(AppData* app) {
    if (app == NULL) return;
    gchar* target = app->deep_search_open_target;
    app->deep_search_open_target = NULL;
    gchar* path = deep_search_store_path(deep_search_target_id(target));
    SavedDeepSearch* saved = deep_search_store_load(path);
    if (saved == NULL) {
        bookmark_message(app, _("Open Search"), _("Could not open this saved search."), _("_Close"), NULL);
    } else {
        deep_search_tab_run_saved(app, saved);
        saved_deep_search_free(saved);
    }
    g_free(path);
    g_free(target);
}
