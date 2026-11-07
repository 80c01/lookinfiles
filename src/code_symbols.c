/* Symbols of the current file in an AppDialog (see code_symbols.h) */
#include "code_symbols.h"
#include "palette.h"
#include "code_outline.h"
#include "code_scan.h"
#include "dialog.h"
#include <string.h>

#define RESPONSE_GO 1

/* ------------------------------------------------------------------------- */
/* One symbol                                                                */
/* ------------------------------------------------------------------------- */

#define CODE_TYPE_SYMBOL_ITEM (code_symbol_item_get_type())
G_DECLARE_FINAL_TYPE(CodeSymbolItem, code_symbol_item, CODE, SYMBOL_ITEM, GObject)

struct _CodeSymbolItem {
    GObject parent;
    gchar* name;
    gchar* detail;     /* signature / type / container, or NULL */
    const gchar* kind; /* "function", "heading"... (static) */
    gint line;
    gint column;
};

G_DEFINE_FINAL_TYPE(CodeSymbolItem, code_symbol_item, G_TYPE_OBJECT)

static void code_symbol_item_finalize(GObject* obj) {
    CodeSymbolItem* it = CODE_SYMBOL_ITEM(obj);
    g_free(it->name);
    g_free(it->detail);
    G_OBJECT_CLASS(code_symbol_item_parent_class)->finalize(obj);
}

static void code_symbol_item_class_init(CodeSymbolItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = code_symbol_item_finalize;
}

static void code_symbol_item_init(CodeSymbolItem* it) {
    (void)it;
}

static void add_item(GListStore* store, const gchar* name, const gchar* detail, const gchar* kind, gint line,
                     gint column) {
    CodeSymbolItem* it = g_object_new(CODE_TYPE_SYMBOL_ITEM, NULL);
    it->name = g_strdup(name);
    it->detail = detail != NULL && detail[0] != '\0' ? g_strdup(detail) : NULL;
    it->kind = kind;
    it->line = line;
    it->column = column;
    g_list_store_append(store, it);
    g_object_unref(it);
}

/* ------------------------------------------------------------------------- */
/* Dialog                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    EditorTab* tab;
    GtkWidget* dialog;
    GtkWidget* entry;
    GtkWidget* list;
    GtkCustomFilter* filter;
    GtkFilterListModel* filtered;
    GtkSingleSelection* selection;
    gchar* needle;     /* filter text, lower case */
} Picker;

static void picker_free(Picker* p) {
    g_free(p->needle);
    g_free(p);
}

static gboolean filter_func(gpointer item, gpointer data) {
    Picker* p = data;
    if (p->needle == NULL || p->needle[0] == '\0') return TRUE;
    CodeSymbolItem* it = item;
    gchar* low = g_utf8_strdown(it->name, -1);
    gboolean match = strstr(low, p->needle) != NULL;
    g_free(low);
    return match;
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    Picker* p = data;
    g_free(p->needle);
    p->needle = g_utf8_strdown(gtk_editable_get_text(GTK_EDITABLE(entry)), -1);
    gtk_filter_changed(GTK_FILTER(p->filter), GTK_FILTER_CHANGE_DIFFERENT);
    if (g_list_model_get_n_items(G_LIST_MODEL(p->filtered)) > 0) gtk_single_selection_set_selected(p->selection, 0);
}

static void go_to_selected(Picker* p) {
    CodeSymbolItem* it = gtk_single_selection_get_selected_item(p->selection);
    if (it == NULL) return;
    gchar* path = g_strdup(p->tab->path);
    gint line = it->line, col = it->column;
    AppData* app = p->app;
    EditorTab* tab = p->tab;
    app_dialog_destroy(APP_DIALOG(p->dialog)); /* frees p */
    code_nav_jump(app, tab, path, line, col);
    g_free(path);
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    Picker* p = data;
    if (response == RESPONSE_GO) {
        go_to_selected(p);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_activate_row(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Picker* p = data;
    gtk_single_selection_set_selected(p->selection, position);
    go_to_selected(p);
}

static void on_entry_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    go_to_selected(data);
}

/* Up / Down in the entry move in the list */
static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    Picker* p = data;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->filtered));
    if (n == 0 || (keyval != GDK_KEY_Down && keyval != GDK_KEY_Up)) return FALSE;
    guint cur = gtk_single_selection_get_selected(p->selection);
    if (cur == GTK_INVALID_LIST_POSITION) cur = 0;
    else if (keyval == GDK_KEY_Down && cur + 1 < n) cur++;
    else if (keyval == GDK_KEY_Up && cur > 0) cur--;
    gtk_single_selection_set_selected(p->selection, cur);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), cur, GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_list_item_set_child(item, label);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    CodeSymbolItem* it = gtk_list_item_get_item(item);
    gchar* markup = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">%s%s%s · %d</span>", it->name,
                                            palette_color(PAL_FG_SUBTLE),
                                            it->kind, it->detail != NULL ? " · " : "",
                                            it->detail != NULL ? it->detail : "", it->line + 1);
    gtk_label_set_markup(GTK_LABEL(gtk_list_item_get_child(item)), markup);
    g_free(markup);
}

/* The dialog over the symbols of store (taken) */
static void show_picker(AppData* app, EditorTab* tab, GListStore* store, const gchar* source) {
    Picker* p = g_new0(Picker, 1);
    p->app = app;
    p->tab = tab;
    gchar* title = g_strdup_printf("Symbols of %s", tab->name);
    p->dialog = app_dialog_new(title, app->window, "_Cancel", APP_RESPONSE_CANCEL, "_Go To", RESPONSE_GO, NULL);
    g_free(title);
    app_dialog_set_width(APP_DIALOG(p->dialog), 560);
    app_dialog_set_default_response(APP_DIALOG(p->dialog), RESPONSE_GO);
    /* Same margins as the other dialogs */
    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(p->dialog))), area);

    p->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->entry), "Filter the symbols by name");
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_search_changed), p);
    g_signal_connect(p->entry, "activate", G_CALLBACK(on_entry_activate), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), p);
    gtk_widget_add_controller(p->entry, keys);
    gtk_box_append(GTK_BOX(area), p->entry);

    p->filter = gtk_custom_filter_new(filter_func, p, NULL);
    /* The filter model takes a reference of its own (transfer full) */
    p->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(store)), GTK_FILTER(p->filter));
    p->selection = gtk_single_selection_new(G_LIST_MODEL(p->filtered));
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate_row), p);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 320);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    gtk_widget_add_css_class(scroll, "dialog-list");
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(area), scroll);

    gchar* hint = g_strdup_printf("%u symbols (%s)", g_list_model_get_n_items(G_LIST_MODEL(store)), source);
    GtkWidget* note = gtk_label_new(hint);
    g_free(hint);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_set_margin_top(note, 4);
    gtk_box_append(GTK_BOX(area), note);

    /* The symbol at or before the cursor is selected first */
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint cursor_line = gtk_text_iter_get_line(&cur);
    guint best = 0;
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(store)); i++) {
        CodeSymbolItem* it = g_list_model_get_item(G_LIST_MODEL(store), i);
        if (it->line <= cursor_line) best = i;
        g_object_unref(it);
    }
    gtk_single_selection_set_selected(p->selection, best);
    g_object_unref(store);

    g_signal_connect(p->dialog, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(p->dialog), "picker", p, (GDestroyNotify)picker_free);
    app_dialog_present(APP_DIALOG(p->dialog));
    gtk_widget_grab_focus(p->entry);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), best, GTK_LIST_SCROLL_NONE, NULL);
}

static void on_outline(AppData* app, EditorTab* tab, GPtrArray* entries, const gchar* source, gpointer data) {
    CodeNavMessageFunc message = (CodeNavMessageFunc)data;
    if (app_dialog_is_open(app->window)) return;
    if (entries->len == 0) {
        if (message != NULL) {
            gboolean scanned = code_nav_is_code(tab) && !code_scan_supported(code_lang_from_name(tab->path));
            message(tab, scanned ? "No symbol scanner for this language" : "No symbols found in this file");
        }
        return;
    }
    GListStore* store = g_list_store_new(CODE_TYPE_SYMBOL_ITEM);
    for (guint i = 0; i < entries->len; i++) {
        CodeOutlineEntry* e = g_ptr_array_index(entries, i);
        gchar* detail = e->declaration                               ? g_strdup("declaration")
                        : e->detail != NULL && e->container != NULL ? g_strdup_printf("%s · in %s", e->detail, e->container)
                        : e->container != NULL                      ? g_strdup_printf("in %s", e->container)
                                                                    : g_strdup(e->detail);
        add_item(store, e->name, detail, e->level > 0 ? "heading" : code_symbol_kind_name(e->kind), e->line, e->column);
        g_free(detail);
    }
    show_picker(app, tab, store, source);
}

void code_symbols_show(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || app->window == NULL) return;
    if (!code_outline_supported(tab)) {
        if (message != NULL) message(tab, "No code navigation for this file type");
        return;
    }
    /* A function pointer through gpointer: the usual GLib callback idiom */
    code_outline_collect(app, tab, NULL, on_outline, (gpointer)message, NULL);
}
