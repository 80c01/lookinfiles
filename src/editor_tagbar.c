/* The tagbar of an editor tab (see editor_tagbar.h) */
#include "editor_tagbar.h"
#include "editor.h"
#include "code_outline.h"
#include "icons.h"
#include "panels.h"
#include "settings_store.h"

#define REFRESH_DELAY_MS 600   /* pause in the edits before the outline is read again */
#define CURSOR_DELAY_MS 80     /* cursor moves are followed after this pause */
#define DEFAULT_WIDTH 240
#define MIN_WIDTH 120

/* ------------------------------------------------------------------------- */
/* One row: a group of symbols or an entry                                   */
/* ------------------------------------------------------------------------- */

#define TAGBAR_TYPE_ITEM (tagbar_item_get_type())
G_DECLARE_FINAL_TYPE(TagbarItem, tagbar_item, TAGBAR, ITEM, GObject)

struct _TagbarItem {
    GObject parent;
    gchar* name;
    gchar* detail;        /* or NULL */
    gchar* key;           /* path of names, to keep a row collapsed across refreshes */
    gboolean group;
    CodeSymbolKind kind;  /* of the symbol, or of a group's symbols */
    gint level;           /* Markdown heading level, 0 for a symbol */
    gboolean declaration;
    gint entry;           /* index in the entries, -1 for a group */
    gint line;
    gint column;
    GListStore* children; /* TagbarItem*, or NULL */
};

G_DEFINE_FINAL_TYPE(TagbarItem, tagbar_item, G_TYPE_OBJECT)

static void tagbar_item_finalize(GObject* obj) {
    TagbarItem* it = TAGBAR_ITEM(obj);
    g_free(it->name);
    g_free(it->detail);
    g_free(it->key);
    g_clear_object(&it->children);
    G_OBJECT_CLASS(tagbar_item_parent_class)->finalize(obj);
}

static void tagbar_item_class_init(TagbarItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = tagbar_item_finalize;
}

static void tagbar_item_init(TagbarItem* it) {
    it->entry = -1;
}

static GListStore* store_of(const GPtrArray* nodes, const GPtrArray* entries, const gchar* parent_key) {
    GListStore* store = g_list_store_new(TAGBAR_TYPE_ITEM);
    for (guint i = 0; i < nodes->len; i++) {
        const CodeOutlineNode* node = g_ptr_array_index(nodes, i);
        TagbarItem* it = g_object_new(TAGBAR_TYPE_ITEM, NULL);
        if (node->entry < 0) {
            it->group = TRUE;
            it->kind = node->group;
            it->name = g_strdup(code_outline_group_label(node->group));
        } else {
            const CodeOutlineEntry* e = g_ptr_array_index(entries, node->entry);
            it->name = g_strdup(e->name);
            it->detail = g_strdup(e->detail);
            it->kind = e->kind;
            it->level = e->level;
            it->declaration = e->declaration;
            it->entry = node->entry;
            it->line = e->line;
            it->column = e->column;
        }
        it->key = g_strdup_printf("%s/%s%s", parent_key, it->group ? "=" : "", it->name);
        if (node->children->len > 0) it->children = store_of(node->children, entries, it->key);
        g_list_store_append(store, it);
        g_object_unref(it);
    }
    return store;
}

static GListModel* create_children(gpointer item, gpointer data) {
    (void)data;
    TagbarItem* it = item;
    return it->children != NULL ? G_LIST_MODEL(g_object_ref(it->children)) : NULL;
}

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    EditorTab* tab;          /* NULL once released */
    GtkWidget* paned;
    GtkWidget* panel;
    GtkWidget* title;
    GtkWidget* source;
    GtkWidget* sort_button;
    GtkWidget* stack;        /* "list" or "empty" */
    GtkWidget* empty;
    GtkWidget* list;
    GtkSingleSelection* selection;
    GtkTreeListModel* tree;
    GPtrArray* entries;      /* CodeOutlineEntry* of the rows, or NULL before the first answer */
    CodeOutlineSort sort;
    GHashTable* collapsed;   /* keys of the rows the user collapsed */
    GCancellable* cancellable;
    guint refresh_source;
    guint cursor_source;
    gboolean stale;          /* the buffer changed while the tagbar was hidden */
    gboolean placing;        /* the paned position is being set by the code */
} Tagbar;

static void tagbar_free(Tagbar* tb) {
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_object(&tb->cancellable);
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    g_clear_object(&tb->tree);
    g_clear_pointer(&tb->entries, g_ptr_array_unref);
    g_hash_table_unref(tb->collapsed);
    g_free(tb);
}

static gint tagbar_width(AppData* app) {
    return app->editor_tagbar_width >= MIN_WIDTH ? app->editor_tagbar_width : DEFAULT_WIDTH;
}

static gboolean tagbar_wanted(Tagbar* tb) {
    return tb->tab != NULL && tb->app->editor_tagbar && code_outline_supported(tb->tab);
}

static gboolean list_has_focus(Tagbar* tb) {
    GtkRoot* root = gtk_widget_get_root(tb->list);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    return focus != NULL && (focus == tb->list || gtk_widget_is_ancestor(focus, tb->list));
}

/* ------------------------------------------------------------------------- */
/* Width: one for every tab, kept when the window is resized                 */
/* ------------------------------------------------------------------------- */

static void place_handle(Tagbar* tb) {
    if (!gtk_widget_get_visible(tb->panel)) return;
    gint max = 0;
    g_object_get(tb->paned, "max-position", &max, NULL);
    if (max <= 0) return;
    tb->placing = TRUE;
    gtk_paned_set_position(GTK_PANED(tb->paned), MAX(max - tagbar_width(tb->app), 0));
    tb->placing = FALSE;
}

static void on_max_position(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    place_handle(data);
}

static void on_position(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    Tagbar* tb = data;
    if (tb->placing || !gtk_widget_get_visible(tb->panel) || !gtk_widget_get_mapped(tb->paned)) return;
    gint max = 0;
    gboolean set = FALSE;
    g_object_get(obj, "max-position", &max, "position-set", &set, NULL);
    /* Before the first place_handle(), GTK picks a position of its own */
    if (!set) return;
    gint width = max - gtk_paned_get_position(GTK_PANED(obj));
    if (max > 0 && width >= MIN_WIDTH) tb->app->editor_tagbar_width = width;
}

/* ------------------------------------------------------------------------- */
/* Cursor                                                                    */
/* ------------------------------------------------------------------------- */

static gint cursor_line(EditorTab* tab) {
    GtkTextMark* mark = tab->view_passive && tab->view_insert != NULL ? tab->view_insert
                                                                        : gtk_text_buffer_get_insert(tab->buffer);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, mark);
    return gtk_text_iter_get_line(&it);
}

/* Select the row of the entry the cursor is in (none when it is hidden in
 * a collapsed row) */
static void follow_cursor(Tagbar* tb) {
    if (tb->tab == NULL || tb->tree == NULL || tb->entries == NULL) return;
    gint entry = code_outline_entry_at_line(tb->entries, cursor_line(tb->tab));
    GListModel* rows = G_LIST_MODEL(tb->tree);
    guint n = g_list_model_get_n_items(rows);
    guint found = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; entry >= 0 && i < n && found == GTK_INVALID_LIST_POSITION; i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        TagbarItem* it = gtk_tree_list_row_get_item(row);
        if (it->entry == entry) found = i;
        g_object_unref(it);
        g_object_unref(row);
    }
    if (found == gtk_single_selection_get_selected(tb->selection)) return;
    gtk_single_selection_set_selected(tb->selection, found);
    if (found != GTK_INVALID_LIST_POSITION) {
        gtk_list_view_scroll_to(GTK_LIST_VIEW(tb->list), found, GTK_LIST_SCROLL_NONE, NULL);
    }
}

static gboolean on_cursor_timeout(gpointer data) {
    Tagbar* tb = data;
    tb->cursor_source = 0;
    if (!list_has_focus(tb)) follow_cursor(tb);
    return G_SOURCE_REMOVE;
}

static void on_mark_set(GtkTextBuffer* buffer, const GtkTextIter* where, GtkTextMark* mark, gpointer data) {
    (void)where;
    Tagbar* tb = data;
    if (mark != gtk_text_buffer_get_insert(buffer) || !gtk_widget_get_visible(tb->panel)) return;
    if (tb->cursor_source == 0) tb->cursor_source = g_timeout_add(CURSOR_DELAY_MS, on_cursor_timeout, tb);
}

/* ------------------------------------------------------------------------- */
/* Outline                                                                   */
/* ------------------------------------------------------------------------- */

static void collapse_remembered(Tagbar* tb) {
    GListModel* rows = G_LIST_MODEL(tb->tree);
    for (guint i = 0; i < g_list_model_get_n_items(rows); i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        TagbarItem* it = gtk_tree_list_row_get_item(row);
        if (it->children != NULL && g_hash_table_contains(tb->collapsed, it->key)) {
            gtk_tree_list_row_set_expanded(row, FALSE);
        }
        g_object_unref(it);
        g_object_unref(row);
    }
}

static void rebuild(Tagbar* tb) {
    if (tb->entries == NULL) return;
    GPtrArray* roots = code_outline_tree(tb->entries, tb->sort);
    GListStore* store = store_of(roots, tb->entries, "");
    g_ptr_array_unref(roots);
    g_clear_object(&tb->tree);
    /* The tree takes the store (transfer full) */
    tb->tree = gtk_tree_list_model_new(G_LIST_MODEL(store), FALSE, TRUE, create_children, NULL, NULL);
    collapse_remembered(tb);
    gtk_single_selection_set_model(tb->selection, G_LIST_MODEL(tb->tree));
    gtk_single_selection_set_selected(tb->selection, GTK_INVALID_LIST_POSITION);
    follow_cursor(tb);
}

static void on_outline(AppData* app, EditorTab* tab, GPtrArray* entries, const gchar* source, gpointer data) {
    (void)app;
    (void)tab;
    Tagbar* tb = data;
    g_clear_pointer(&tb->entries, g_ptr_array_unref);
    tb->entries = g_ptr_array_ref(entries);
    gboolean headings = g_strcmp0(source, "Markdown headings") == 0;
    gtk_label_set_text(GTK_LABEL(tb->title), headings ? "Headings" : "Symbols");
    gtk_label_set_text(GTK_LABEL(tb->source), headings ? "" : source);
    gtk_widget_set_visible(tb->sort_button, !headings);
    gtk_label_set_text(GTK_LABEL(tb->empty), headings ? "No headings in this file" : "No symbols in this file");
    gtk_stack_set_visible_child_name(GTK_STACK(tb->stack), entries->len > 0 ? "list" : "empty");
    rebuild(tb);
}

static void refresh_now(Tagbar* tb) {
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    if (tb->tab == NULL) return;
    tb->stale = FALSE;
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_object(&tb->cancellable);
    tb->cancellable = g_cancellable_new();
    /* tb stays alive while its cancellable is not cancelled: nothing to free */
    code_outline_collect(tb->app, tb->tab, tb->cancellable, on_outline, tb, NULL);
}

static gboolean on_refresh_timeout(gpointer data) {
    Tagbar* tb = data;
    tb->refresh_source = 0;
    refresh_now(tb);
    return G_SOURCE_REMOVE;
}

static void update_visibility(Tagbar* tb) {
    gboolean show = tagbar_wanted(tb);
    if (show == gtk_widget_get_visible(tb->panel)) return;
    gtk_widget_set_visible(tb->panel, show);
    if (!show) return;
    place_handle(tb);
    if (tb->entries == NULL || tb->stale) refresh_now(tb);
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    Tagbar* tb = data;
    if (!gtk_widget_get_visible(tb->panel)) {
        /* Saved under another name, the file may now have an outline */
        tb->stale = TRUE;
        update_visibility(tb);
        return;
    }
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    tb->refresh_source = g_timeout_add(REFRESH_DELAY_MS, on_refresh_timeout, tb);
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

/* CSS class of a name (colours in theme.c, so they follow theme changes) */
static const gchar* name_class(const TagbarItem* it) {
    if (it->group || it->level > 0) return "tagbar-strong";
    switch (it->kind) {
    case CODE_SYM_FUNCTION:
    case CODE_SYM_METHOD: return "tagbar-function";
    case CODE_SYM_TYPE:
    case CODE_SYM_CLASS: return "tagbar-type";
    case CODE_SYM_MACRO:
    case CODE_SYM_CONSTANT: return "tagbar-constant";
    case CODE_SYM_VARIABLE:
    case CODE_SYM_FIELD: return "tagbar-variable";
    case CODE_SYM_MODULE:
    case CODE_SYM_TARGET: return "tagbar-keyword";
    }
    return "tagbar-plain";
}

static void go_to_item(Tagbar* tb, TagbarItem* it) {
    EditorTab* tab = tb->tab;
    if (tab == NULL || it->group) return;
    EditorTab* dest = NULL;
    if (tab->path != NULL && tab->path[0] != '\0') {
        dest = code_nav_jump(tb->app, tab, tab->path, it->line, it->column);
    } else {
        GtkTextIter where;
        gtk_text_buffer_get_iter_at_line_index(tab->buffer, &where, it->line, it->column);
        gtk_text_buffer_place_cursor(tab->buffer, &where);
        editor_center_cursor(tab);
        dest = tab;
    }
    if (dest != NULL && dest->view != NULL) gtk_widget_grab_focus(dest->view);
}

/* Enter on a row, or a click: go to the symbol, open or close a group */
static void activate_row(Tagbar* tb, GtkTreeListRow* row) {
    TagbarItem* it = gtk_tree_list_row_get_item(row);
    if (it->group) {
        gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
    } else {
        go_to_item(tb, it);
    }
    g_object_unref(it);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Tagbar* tb = data;
    GtkTreeListRow* row = g_list_model_get_item(G_LIST_MODEL(tb->tree), position);
    if (row == NULL) return;
    activate_row(tb, row);
    g_object_unref(row);
}

static void on_row_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    Tagbar* tb = data;
    GtkWidget* box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkTreeListRow* row = g_object_get_data(G_OBJECT(box), "tagbar-row");
    if (row != NULL) activate_row(tb, row);
}

static void on_row_expanded(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    Tagbar* tb = data;
    GtkTreeListRow* row = GTK_TREE_LIST_ROW(obj);
    TagbarItem* it = gtk_tree_list_row_get_item(row);
    if (it == NULL) return;
    if (gtk_tree_list_row_get_expanded(row)) {
        g_hash_table_remove(tb->collapsed, it->key);
    } else {
        g_hash_table_add(tb->collapsed, g_strdup(it->key));
    }
    g_object_unref(it);
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    GtkWidget* expander = gtk_tree_expander_new();
    gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(expander), TRUE);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* name = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), name);
    GtkWidget* detail = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(detail, TRUE);
    gtk_widget_add_css_class(detail, "tagbar-detail");
    gtk_box_append(GTK_BOX(box), detail);
    g_object_set_data(G_OBJECT(box), "name", name);
    g_object_set_data(G_OBJECT(box), "detail", detail);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "released", G_CALLBACK(on_row_click), data);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(item, expander);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    Tagbar* tb = data;
    GtkTreeListRow* row = gtk_list_item_get_item(item);
    GtkWidget* expander = gtk_list_item_get_child(item);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
    GtkWidget* box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
    g_object_set_data(G_OBJECT(box), "tagbar-row", row);
    if (g_object_get_data(G_OBJECT(row), "tagbar-watched") == NULL) {
        g_object_set_data(G_OBJECT(row), "tagbar-watched", GINT_TO_POINTER(1));
        g_signal_connect(row, "notify::expanded", G_CALLBACK(on_row_expanded), tb);
    }
    TagbarItem* it = gtk_tree_list_row_get_item(row);
    gboolean strong = it->group || (it->level > 0 && it->level <= 2);
    GtkWidget* name = g_object_get_data(G_OBJECT(box), "name");
    const gchar* classes[] = {"tagbar-name", name_class(it), strong ? "tagbar-bold" : NULL, NULL};
    gtk_widget_set_css_classes(name, classes);
    gtk_label_set_text(GTK_LABEL(name), it->name);
    gchar* detail;
    if (it->group) {
        detail = g_strdup_printf("%u", it->children != NULL ? g_list_model_get_n_items(G_LIST_MODEL(it->children)) : 0);
    } else if (it->level > 0) {
        detail = NULL;
    } else {
        const gchar* kind = tb->sort == CODE_OUTLINE_BY_LINE ? code_symbol_kind_name(it->kind) : NULL;
        const gchar* extra = it->declaration ? "declaration" : it->detail;
        detail = kind != NULL && extra != NULL ? g_strdup_printf("%s · %s", kind, extra) : g_strdup(kind != NULL ? kind : extra);
    }
    GtkWidget* detail_label = g_object_get_data(G_OBJECT(box), "detail");
    gtk_label_set_text(GTK_LABEL(detail_label), detail != NULL ? detail : "");
    gtk_widget_set_tooltip_text(box, it->group ? NULL : (it->detail != NULL ? it->detail : it->name));
    g_free(detail);
    g_object_unref(it);
}

static void on_unbind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* expander = gtk_list_item_get_child(item);
    GtkWidget* box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
    g_object_set_data(G_OBJECT(box), "tagbar-row", NULL);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), NULL);
}

/* ------------------------------------------------------------------------- */
/* Header and keys                                                           */
/* ------------------------------------------------------------------------- */

static void update_sort_button(Tagbar* tb) {
    gboolean by_kind = tb->sort == CODE_OUTLINE_BY_KIND;
    icon_button_set_content(tb->sort_button, by_kind ? "view-list-ordered" : "view-list-bullet", NULL);
    gtk_widget_set_tooltip_text(tb->sort_button, by_kind ? "Sort By Line (s)" : "Group By Kind (s)");
}

static void toggle_sort(Tagbar* tb) {
    tb->sort = tb->sort == CODE_OUTLINE_BY_KIND ? CODE_OUTLINE_BY_LINE : CODE_OUTLINE_BY_KIND;
    update_sort_button(tb);
    rebuild(tb);
}

static void on_sort_clicked(GtkButton* button, gpointer data) {
    (void)button;
    toggle_sort(data);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    Tagbar* tb = data;
    if (tb->tab != NULL) editor_tagbar_toggle(tb->app, tb->tab, NULL);
}

/* Escape: back to the view; s: sort; q: hide (Enter goes to the symbol) */
static gboolean on_list_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    Tagbar* tb = data;
    if (tb->tab == NULL || (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0) return FALSE;
    switch (keyval) {
    case GDK_KEY_Escape: gtk_widget_grab_focus(tb->tab->view); return TRUE;
    case GDK_KEY_s:
        if (tb->entries != NULL && gtk_widget_get_visible(tb->sort_button)) toggle_sort(tb);
        return TRUE;
    case GDK_KEY_q: editor_tagbar_toggle(tb->app, tb->tab, NULL); return TRUE;
    default: return FALSE;
    }
}

/* ------------------------------------------------------------------------- */
/* Public                                                                    */
/* ------------------------------------------------------------------------- */

GtkWidget* editor_tagbar_wrap(AppData* app, EditorTab* tab, GtkWidget* content) {
    Tagbar* tb = g_new0(Tagbar, 1);
    tb->app = app;
    tb->tab = tab;
    tb->sort = CODE_OUTLINE_BY_KIND;
    tb->collapsed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    tab->tagbar = tb;

    tb->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(tb->panel, "editor-tagbar");
    gtk_widget_set_size_request(tb->panel, MIN_WIDTH, -1);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(header, "tagbar-header");
    tb->title = gtk_label_new("Symbols");
    gtk_widget_add_css_class(tb->title, "tagbar-title");
    gtk_box_append(GTK_BOX(header), tb->title);
    tb->source = gtk_label_new(NULL);
    gtk_widget_add_css_class(tb->source, "tagbar-detail");
    gtk_label_set_xalign(GTK_LABEL(tb->source), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tb->source), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tb->source, TRUE);
    gtk_box_append(GTK_BOX(header), tb->source);
    tb->sort_button = icon_button_new("view-list-ordered", NULL);
    update_sort_button(tb);
    g_signal_connect(tb->sort_button, "clicked", G_CALLBACK(on_sort_clicked), tb);
    gtk_box_append(GTK_BOX(header), tb->sort_button);
    GtkWidget* close = icon_button_new("window-close", "Hide Tagbar (q)");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), tb);
    gtk_box_append(GTK_BOX(header), close);
    gtk_box_append(GTK_BOX(tb->panel), header);

    tb->selection = gtk_single_selection_new(NULL);
    gtk_single_selection_set_autoselect(tb->selection, FALSE);
    gtk_single_selection_set_can_unselect(tb->selection, TRUE);
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), tb);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), tb);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_unbind), tb);
    tb->list = gtk_list_view_new(GTK_SELECTION_MODEL(tb->selection), factory);
    g_signal_connect(tb->list, "activate", G_CALLBACK(on_activate), tb);
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), tb);
    gtk_widget_add_controller(tb->list, keys);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tb->list);
    tb->empty = gtk_label_new("Reading the symbols…");
    gtk_widget_add_css_class(tb->empty, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(tb->empty), TRUE);
    gtk_widget_set_valign(tb->empty, GTK_ALIGN_START);
    gtk_widget_set_margin_top(tb->empty, 12);
    tb->stack = gtk_stack_new();
    gtk_widget_set_vexpand(tb->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(tb->stack), scroll, "list");
    gtk_stack_add_named(GTK_STACK(tb->stack), tb->empty, "empty");
    gtk_stack_set_visible_child_name(GTK_STACK(tb->stack), "empty");
    gtk_box_append(GTK_BOX(tb->panel), tb->stack);
    gtk_widget_set_visible(tb->panel, FALSE);

    tb->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(tb->paned, TRUE);
    gtk_widget_set_vexpand(tb->paned, TRUE);
    paned_use_thin_handle(tb->paned);
    gtk_paned_set_start_child(GTK_PANED(tb->paned), content);
    gtk_paned_set_resize_start_child(GTK_PANED(tb->paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(tb->paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(tb->paned), tb->panel);
    gtk_paned_set_resize_end_child(GTK_PANED(tb->paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(tb->paned), FALSE);
    g_signal_connect(tb->paned, "notify::max-position", G_CALLBACK(on_max_position), tb);
    g_signal_connect(tb->paned, "notify::position", G_CALLBACK(on_position), tb);
    g_object_set_data_full(G_OBJECT(tb->paned), "tagbar", tb, (GDestroyNotify)tagbar_free);

    g_signal_connect(tab->buffer, "mark-set", G_CALLBACK(on_mark_set), tb);
    g_signal_connect_after(tab->buffer, "changed", G_CALLBACK(on_buffer_changed), tb);
    update_visibility(tb);
    return tb->paned;
}

void editor_tagbar_release(EditorTab* tab) {
    Tagbar* tb = tab != NULL ? tab->tagbar : NULL;
    if (tb == NULL) return;
    if (tab->buffer != NULL) g_signal_handlers_disconnect_by_data(tab->buffer, tb);
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    tb->tab = NULL;
    tab->tagbar = NULL;
}

static void update_tabs(GList* tabs) {
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->tagbar != NULL) update_visibility(t->tagbar);
    }
}

void editor_tagbar_set_shown(AppData* app, gboolean shown) {
    if (app == NULL || app->editor_tagbar == shown) return;
    app->editor_tagbar = shown;
    update_tabs(app->editors);
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws) update_tabs(app->workspaces[i].editors);
    }
}

void editor_tagbar_toggle(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL) return;
    if (!app->editor_tagbar && !code_outline_supported(tab)) {
        if (message != NULL) message(tab, "No tagbar for this file type (code and Markdown files)");
        return;
    }
    editor_tagbar_set_shown(app, !app->editor_tagbar);
    Tagbar* tb = tab->tagbar;
    if (app->editor_tagbar && tb != NULL && gtk_widget_get_visible(tb->panel)) {
        gtk_widget_grab_focus(tb->list);
    } else if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    gchar* path = settings_store_path();
    Settings s;
    settings_store_load(path, &s);
    s.tagbar = app->editor_tagbar;
    GError* error = NULL;
    if (!settings_store_save(path, &s, &error)) {
        g_printerr("[ Tagbar setting not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);
}
