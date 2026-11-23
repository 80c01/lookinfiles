/* The list of a project's tasks (see task_picker.h) */
#include "tasks/task_picker.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include "widgets/dialog.h"
#include <string.h>

#define TASK_TYPE_CHOICE (task_choice_get_type())
G_DECLARE_FINAL_TYPE(TaskChoice, task_choice, TASK, CHOICE, GObject)

struct _TaskChoice {
    GObject parent;
    TaskDef* def;
};

G_DEFINE_FINAL_TYPE(TaskChoice, task_choice, G_TYPE_OBJECT)

static void task_choice_finalize(GObject* obj) {
    task_def_free(TASK_CHOICE(obj)->def);
    G_OBJECT_CLASS(task_choice_parent_class)->finalize(obj);
}

static void task_choice_class_init(TaskChoiceClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = task_choice_finalize;
}

static void task_choice_init(TaskChoice* c) {
    (void)c;
}

enum { RESPONSE_RUN = 1 };

typedef struct {
    AppData* app;
    gchar* project;
    TaskPickerRun run;
    GtkWidget* dialog;
    GtkWidget* entry;
    GtkWidget* list;
    GListStore* store;
    GtkCustomFilter* filter;
    GtkFilterListModel* filtered;
    GtkSingleSelection* selection;
    gchar* needle; /* filter text, lower case */
} Picker;

static void picker_free(gpointer data) {
    Picker* p = data;
    g_clear_object(&p->store);
    g_free(p->project);
    g_free(p->needle);
    g_free(p);
}

static gboolean filter_func(gpointer item, gpointer data) {
    Picker* p = data;
    if (p->needle == NULL || p->needle[0] == '\0') return TRUE;
    gchar* low = g_utf8_strdown(TASK_CHOICE(item)->def->label, -1);
    gboolean match = strstr(low, p->needle) != NULL;
    g_free(low);
    if (!match && TASK_CHOICE(item)->def->detail != NULL) {
        low = g_utf8_strdown(TASK_CHOICE(item)->def->detail, -1);
        match = strstr(low, p->needle) != NULL;
        g_free(low);
    }
    return match;
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    Picker* p = data;
    g_free(p->needle);
    p->needle = g_utf8_strdown(gtk_editable_get_text(GTK_EDITABLE(entry)), -1);
    gtk_filter_changed(GTK_FILTER(p->filter), GTK_FILTER_CHANGE_DIFFERENT);
    if (g_list_model_get_n_items(G_LIST_MODEL(p->filtered)) > 0) {
        gtk_single_selection_set_selected(p->selection, 0);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), 0, GTK_LIST_SCROLL_NONE, NULL);
    }
}

static void run_selected(Picker* p) {
    TaskChoice* c = gtk_single_selection_get_selected_item(p->selection);
    if (c == NULL) return;
    AppData* app = p->app;
    TaskPickerRun run = p->run;
    TaskDef* def = task_def_copy(c->def);
    gchar* project = g_strdup(p->project);
    app_dialog_destroy(APP_DIALOG(p->dialog)); /* frees p */
    run(app, def, project);
    task_def_free(def);
    g_free(project);
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    if (response == RESPONSE_RUN) {
        run_selected(data);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_activate_row(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Picker* p = data;
    gtk_single_selection_set_selected(p->selection, position);
    run_selected(p);
}

static void on_entry_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    run_selected(data);
}

static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    Picker* p = data;
    /* Escape closes the picker (the search entry would only clear its text) */
    if (keyval == GDK_KEY_Escape) {
        app_dialog_response(APP_DIALOG(p->dialog), APP_RESPONSE_CANCEL);
        return TRUE;
    }
    gint delta = 0;
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: delta = 1; break;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: delta = -1; break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: delta = 10; break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: delta = -10; break;
    default: return FALSE;
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->filtered));
    if (n == 0) return TRUE;
    guint cur = gtk_single_selection_get_selected(p->selection);
    gint next = cur == GTK_INVALID_LIST_POSITION ? 0 : CLAMP((gint)cur + delta, 0, (gint)n - 1);
    gtk_single_selection_set_selected(p->selection, (guint)next);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), (guint)next, GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

static void on_choice_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_list_item_set_child(item, label);
}

static void on_choice_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    const gchar* project = data;
    TaskChoice* c = gtk_list_item_get_item(item);
    /* A named task's command; the folder when it is not the project's */
    const gchar* where = c->def->detail != NULL               ? c->def->detail
                         : g_strcmp0(c->def->dir, project) != 0 ? c->def->dir
                                                                : "";
    gchar* markup = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">%s</span>", c->def->label,
                                            palette_color(PAL_FG_SUBTLE), where);
    gtk_label_set_markup(GTK_LABEL(gtk_list_item_get_child(item)), markup);
    g_free(markup);
}

void task_picker_show(AppData* app, const gchar* project, const GPtrArray* tasks, const TaskDef* chosen,
                      TaskPickerRun run) {
    if (app->window == NULL || app_dialog_is_open(app->window)) return;
    Picker* p = g_new0(Picker, 1);
    p->app = app;
    p->project = g_strdup(project);
    p->run = run;
    p->store = g_list_store_new(TASK_TYPE_CHOICE);
    guint selected = 0;
    for (guint i = 0; i < tasks->len; i++) {
        TaskChoice* c = g_object_new(TASK_TYPE_CHOICE, NULL);
        c->def = task_def_copy(g_ptr_array_index(tasks, i));
        if (task_def_equal(c->def, chosen)) selected = i;
        g_list_store_append(p->store, c);
        g_object_unref(c);
    }

    gchar* base = g_path_get_basename(project);
    gchar* title = g_strdup_printf(_("Run a Task of %s"), base);
    g_free(base);
    p->dialog = app_dialog_new(title, app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Run"), RESPONSE_RUN, NULL);
    g_free(title);
    app_dialog_set_width(APP_DIALOG(p->dialog), 520);
    app_dialog_set_default_response(APP_DIALOG(p->dialog), RESPONSE_RUN);
    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(p->dialog))), area);

    p->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->entry), _("Filter the tasks"));
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_search_changed), p);
    g_signal_connect(p->entry, "activate", G_CALLBACK(on_entry_activate), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), p);
    gtk_widget_add_controller(p->entry, keys);
    gtk_box_append(GTK_BOX(area), p->entry);

    p->filter = gtk_custom_filter_new(filter_func, p, NULL);
    p->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(p->store)), GTK_FILTER(p->filter));
    p->selection = gtk_single_selection_new(G_LIST_MODEL(p->filtered));
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_choice_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_choice_bind), p->project);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate_row), p);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 280);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    gtk_widget_add_css_class(scroll, "dialog-list");
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(area), scroll);

    gchar* where = g_strdup_printf(_("In %s"), project);
    GtkWidget* note = gtk_label_new(where);
    g_free(where);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(note), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_top(note, 4);
    gtk_box_append(GTK_BOX(area), note);

    gtk_single_selection_set_selected(p->selection, selected);
    g_signal_connect(p->dialog, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(p->dialog), "task-picker", p, picker_free);
    app_dialog_present(APP_DIALOG(p->dialog));
    gtk_widget_grab_focus(p->entry);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), selected, GTK_LIST_SCROLL_NONE, NULL);
}

