/* A tagbar beside some content (see outline_pane.h) */
#include "widgets/outline_pane.h"
#include "theme/icons.h"
#include "core/i18n.h"
#include "widgets/panels.h"

#define DEFAULT_WIDTH 180
#define MIN_WIDTH 120

/* ------------------------------------------------------------------------- */
/* One row: a group of symbols or an entry                                   */
/* ------------------------------------------------------------------------- */

#define OUTLINE_TYPE_ITEM (outline_item_get_type())
G_DECLARE_FINAL_TYPE(OutlineItem, outline_item, OUTLINE, ITEM, GObject)

struct _OutlineItem {
    GObject parent;
    gchar* name;
    gchar* detail;        /* or NULL */
    gchar* key;           /* path of names, to keep a row collapsed across refreshes */
    gboolean group;
    CodeSymbolKind kind;  /* of the symbol, or of a group's symbols */
    gint level;           /* heading level, 0 for a symbol */
    gboolean declaration;
    gint entry;           /* index in the entries, -1 for a group */
    GListStore* children; /* OutlineItem*, or NULL */
};

G_DEFINE_FINAL_TYPE(OutlineItem, outline_item, G_TYPE_OBJECT)

static void outline_item_finalize(GObject* obj) {
    OutlineItem* it = OUTLINE_ITEM(obj);
    g_free(it->name);
    g_free(it->detail);
    g_free(it->key);
    g_clear_object(&it->children);
    G_OBJECT_CLASS(outline_item_parent_class)->finalize(obj);
}

static void outline_item_class_init(OutlineItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = outline_item_finalize;
}

static void outline_item_init(OutlineItem* it) {
    it->entry = -1;
}

static GListStore* store_of(const GPtrArray* nodes, const GPtrArray* entries, const gchar* parent_key) {
    GListStore* store = g_list_store_new(OUTLINE_TYPE_ITEM);
    for (guint i = 0; i < nodes->len; i++) {
        const CodeOutlineNode* node = g_ptr_array_index(nodes, i);
        OutlineItem* it = g_object_new(OUTLINE_TYPE_ITEM, NULL);
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
    OutlineItem* it = item;
    return it->children != NULL ? G_LIST_MODEL(g_object_ref(it->children)) : NULL;
}

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

struct OutlinePane {
    OutlinePaneCallbacks cb;
    gpointer data;           /* NULL callbacks once detached */
    gboolean detached;
    gint* width;
    GtkWidget* paned;
    GtkWidget* panel;
    GtkWidget* title;
    GtkWidget* sort_button;
    GtkWidget* collapse_button;
    gboolean collapse_shown; /* the collapse button shows "Collapse All" */
    guint collapse_idle;     /* pending update of the collapse button */
    GtkWidget* stack;        /* "list" or "empty" */
    GtkWidget* empty;
    GtkWidget* list;
    GtkSingleSelection* selection;
    GtkTreeListModel* tree;
    GPtrArray* entries;      /* CodeOutlineEntry* of the rows, or NULL before the first outline */
    gint jumped;             /* entry last gone to, -1: kept selected while the cursor is on its line */
    CodeOutlineSort sort;
    GHashTable* collapsed;   /* keys of the rows the user collapsed */
    gboolean placing;        /* the paned position is being set by the code */
};

static void pane_free(OutlinePane* p) {
    if (p->collapse_idle != 0) g_source_remove(p->collapse_idle);
    g_clear_object(&p->tree);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    g_hash_table_unref(p->collapsed);
    g_free(p);
}

static gint pane_width(OutlinePane* p) {
    return *p->width >= MIN_WIDTH ? *p->width : DEFAULT_WIDTH;
}

/* ------------------------------------------------------------------------- */
/* Width: kept when the window is resized                                    */
/* ------------------------------------------------------------------------- */

/* The panel cannot shrink, so GTK takes its minimum width off max-position:
 * the far end of the paned is max-position + that minimum */
static gint paned_end(OutlinePane* p) {
    gint max = 0;
    g_object_get(p->paned, "max-position", &max, NULL);
    if (max <= 0) return 0;
    gint min = 0;
    gtk_widget_measure(p->panel, GTK_ORIENTATION_HORIZONTAL, -1, &min, NULL, NULL, NULL);
    return max + min;
}

static void place_handle(OutlinePane* p) {
    if (!gtk_widget_get_visible(p->panel)) return;
    gint end = paned_end(p);
    if (end <= 0) return;
    p->placing = TRUE;
    gtk_paned_set_position(GTK_PANED(p->paned), MAX(end - pane_width(p), 0));
    p->placing = FALSE;
}

static void on_max_position(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    place_handle(data);
}

static void on_position(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    OutlinePane* p = data;
    if (p->placing || !gtk_widget_get_visible(p->panel) || !gtk_widget_get_mapped(p->paned)) return;
    gboolean set = FALSE;
    g_object_get(obj, "position-set", &set, NULL);
    /* Before the first place_handle(), GTK picks a position of its own */
    if (!set) return;
    gint end = paned_end(p);
    gint width = end - gtk_paned_get_position(GTK_PANED(obj));
    if (end > 0 && width >= MIN_WIDTH) *p->width = width;
}

/* ------------------------------------------------------------------------- */
/* Outline                                                                   */
/* ------------------------------------------------------------------------- */

/* Select the row of the entry the cursor is in (none when it is hidden in
 * a collapsed row) */
void outline_pane_follow_cursor(OutlinePane* p) {
    if (p == NULL || p->detached || p->tree == NULL || p->entries == NULL) return;
    gint entry = code_outline_entry_at_line(p->entries, p->cb.cursor_line(p->data));
    /* Several entries on one line: the one gone to, not the last one */
    if (entry >= 0 && p->jumped >= 0 && (guint)p->jumped < p->entries->len) {
        const CodeOutlineEntry* at = g_ptr_array_index(p->entries, entry);
        const CodeOutlineEntry* gone = g_ptr_array_index(p->entries, p->jumped);
        if (gone->line == at->line) entry = p->jumped;
    }
    GListModel* rows = G_LIST_MODEL(p->tree);
    guint n = g_list_model_get_n_items(rows);
    guint found = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; entry >= 0 && i < n && found == GTK_INVALID_LIST_POSITION; i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        OutlineItem* it = gtk_tree_list_row_get_item(row);
        if (it->entry == entry) found = i;
        g_object_unref(it);
        g_object_unref(row);
    }
    if (found == gtk_single_selection_get_selected(p->selection)) return;
    gtk_single_selection_set_selected(p->selection, found);
    if (found != GTK_INVALID_LIST_POSITION) {
        gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), found, GTK_LIST_SCROLL_NONE, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Collapse / expand all                                                     */
/* ------------------------------------------------------------------------- */

/* Whether a row can be expanded; whether one is expanded */
static void scan_expanders(OutlinePane* p, gboolean* any_expandable, gboolean* any_expanded) {
    *any_expandable = FALSE;
    *any_expanded = FALSE;
    if (p->tree == NULL) return;
    GListModel* rows = G_LIST_MODEL(p->tree);
    guint n = g_list_model_get_n_items(rows);
    for (guint i = 0; i < n && !*any_expanded; i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        if (gtk_tree_list_row_is_expandable(row)) {
            *any_expandable = TRUE;
            if (gtk_tree_list_row_get_expanded(row)) *any_expanded = TRUE;
        }
        g_object_unref(row);
    }
}

/* The button shows what a click does: pan-up "Collapse All" while a row is
 * expanded, else pan-down "Expand All" (like the explorer toolbars) */
static void update_collapse_button(OutlinePane* p) {
    gboolean expandable, expanded;
    scan_expanders(p, &expandable, &expanded);
    gtk_widget_set_sensitive(p->collapse_button, expandable);
    if (p->collapse_shown == expanded && gtk_widget_get_tooltip_text(p->collapse_button) != NULL) return;
    p->collapse_shown = expanded;
    icon_button_set_content(p->collapse_button, expanded ? "pan-up" : "pan-down", NULL);
    gtk_widget_set_tooltip_text(p->collapse_button, expanded ? _("Collapse All (c)") : _("Expand All (c)"));
}

static gboolean collapse_button_idle(gpointer data) {
    OutlinePane* p = data;
    p->collapse_idle = 0;
    update_collapse_button(p);
    return G_SOURCE_REMOVE;
}

static void queue_collapse_button(OutlinePane* p) {
    if (p->collapse_idle == 0) p->collapse_idle = g_idle_add(collapse_button_idle, p);
}

/* Expand or collapse a row and remember it (on_row_expanded() only watches
 * the rows shown so far) */
static void set_row_expanded(OutlinePane* p, GtkTreeListRow* row, gboolean expanded) {
    OutlineItem* it = gtk_tree_list_row_get_item(row);
    if (it == NULL) return;
    if (expanded) {
        g_hash_table_remove(p->collapsed, it->key);
    } else {
        g_hash_table_add(p->collapsed, g_strdup(it->key));
    }
    g_object_unref(it);
    gtk_tree_list_row_set_expanded(row, expanded);
}

/* Collapse every row if one is expanded, else expand them all. Collapsing
 * runs from the end (a collapsed row only hides the rows after it) and
 * expanding from the start (the new children come next), so every level is
 * reached. */
static void toggle_collapse_all(OutlinePane* p) {
    gboolean expandable, expanded;
    scan_expanders(p, &expandable, &expanded);
    if (!expandable) return;
    GListModel* rows = G_LIST_MODEL(p->tree);
    if (expanded) {
        for (guint i = g_list_model_get_n_items(rows); i > 0; i--) {
            GtkTreeListRow* row = g_list_model_get_item(rows, i - 1);
            if (row == NULL) continue;
            if (gtk_tree_list_row_get_expanded(row)) set_row_expanded(p, row, FALSE);
            g_object_unref(row);
        }
    } else {
        for (guint i = 0; i < g_list_model_get_n_items(rows); i++) {
            GtkTreeListRow* row = g_list_model_get_item(rows, i);
            if (gtk_tree_list_row_is_expandable(row)) set_row_expanded(p, row, TRUE);
            g_object_unref(row);
        }
    }
    update_collapse_button(p);
}

static void collapse_remembered(OutlinePane* p) {
    GListModel* rows = G_LIST_MODEL(p->tree);
    for (guint i = 0; i < g_list_model_get_n_items(rows); i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        OutlineItem* it = gtk_tree_list_row_get_item(row);
        if (it->children != NULL && g_hash_table_contains(p->collapsed, it->key)) {
            gtk_tree_list_row_set_expanded(row, FALSE);
        }
        g_object_unref(it);
        g_object_unref(row);
    }
}

static void rebuild(OutlinePane* p) {
    if (p->entries == NULL) return;
    GPtrArray* roots = code_outline_tree(p->entries, p->sort);
    GListStore* store = store_of(roots, p->entries, "");
    g_ptr_array_unref(roots);
    g_clear_object(&p->tree);
    /* The tree takes the store (transfer full) */
    p->tree = gtk_tree_list_model_new(G_LIST_MODEL(store), FALSE, TRUE, create_children, NULL, NULL);
    collapse_remembered(p);
    gtk_single_selection_set_model(p->selection, G_LIST_MODEL(p->tree));
    gtk_single_selection_set_selected(p->selection, GTK_INVALID_LIST_POSITION);
    outline_pane_follow_cursor(p);
    update_collapse_button(p);
}

void outline_pane_set_entries(OutlinePane* p, GPtrArray* entries, gboolean headings, const gchar* source) {
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    p->entries = g_ptr_array_ref(entries);
    p->jumped = -1;
    gtk_label_set_text(GTK_LABEL(p->title), headings ? _("Headings") : _("Symbols"));
    /* The scanner name is a tooltip so the header stays narrow */
    gtk_widget_set_tooltip_text(p->title, headings ? NULL : source);
    gtk_widget_set_visible(p->sort_button, !headings);
    gtk_label_set_text(GTK_LABEL(p->empty), headings ? _("No headings in this file") : _("No symbols in this file"));
    gtk_stack_set_visible_child_name(GTK_STACK(p->stack), entries->len > 0 ? "list" : "empty");
    rebuild(p);
}

gboolean outline_pane_has_entries(OutlinePane* p) {
    return p->entries != NULL;
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

/* CSS class of a name (colours in theme.c, so they follow theme changes) */
static const gchar* name_class(const OutlineItem* it) {
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

/* Enter on a row, or a click: go to the entry, open or close a group */
static void activate_row(OutlinePane* p, GtkTreeListRow* row) {
    OutlineItem* it = gtk_tree_list_row_get_item(row);
    if (it->group) {
        gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
    } else if (!p->detached && p->entries != NULL && it->entry >= 0 && (guint)it->entry < p->entries->len) {
        p->jumped = it->entry;
        p->cb.go_to(p->data, g_ptr_array_index(p->entries, it->entry));
    }
    g_object_unref(it);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    OutlinePane* p = data;
    if (p->tree == NULL) return;
    GtkTreeListRow* row = g_list_model_get_item(G_LIST_MODEL(p->tree), position);
    if (row == NULL) return;
    activate_row(p, row);
    g_object_unref(row);
}

static void on_row_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    OutlinePane* p = data;
    GtkWidget* box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkTreeListRow* row = g_object_get_data(G_OBJECT(box), "outline-row");
    if (row != NULL) activate_row(p, row);
}

static void on_row_expanded(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    OutlinePane* p = data;
    GtkTreeListRow* row = GTK_TREE_LIST_ROW(obj);
    OutlineItem* it = gtk_tree_list_row_get_item(row);
    if (it == NULL) return;
    if (gtk_tree_list_row_get_expanded(row)) {
        g_hash_table_remove(p->collapsed, it->key);
    } else {
        g_hash_table_add(p->collapsed, g_strdup(it->key));
    }
    g_object_unref(it);
    queue_collapse_button(p);
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
    OutlinePane* p = data;
    GtkTreeListRow* row = gtk_list_item_get_item(item);
    GtkWidget* expander = gtk_list_item_get_child(item);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
    GtkWidget* box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
    g_object_set_data(G_OBJECT(box), "outline-row", row);
    if (g_object_get_data(G_OBJECT(row), "outline-watched") == NULL) {
        g_object_set_data(G_OBJECT(row), "outline-watched", GINT_TO_POINTER(1));
        g_signal_connect(row, "notify::expanded", G_CALLBACK(on_row_expanded), p);
    }
    OutlineItem* it = gtk_tree_list_row_get_item(row);
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
        const gchar* kind = p->sort == CODE_OUTLINE_BY_LINE ? code_symbol_kind_name(it->kind) : NULL;
        const gchar* extra = it->declaration ? _("declaration") : it->detail;
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
    g_object_set_data(G_OBJECT(box), "outline-row", NULL);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), NULL);
}

/* ------------------------------------------------------------------------- */
/* Header and keys                                                           */
/* ------------------------------------------------------------------------- */

static void update_sort_button(OutlinePane* p) {
    gboolean by_kind = p->sort == CODE_OUTLINE_BY_KIND;
    icon_button_set_content(p->sort_button, by_kind ? "view-list-ordered" : "view-list-bullet", NULL);
    gtk_widget_set_tooltip_text(p->sort_button, by_kind ? _("Sort By Line (s)") : _("Group By Kind (s)"));
}

static void toggle_sort(OutlinePane* p) {
    p->sort = p->sort == CODE_OUTLINE_BY_KIND ? CODE_OUTLINE_BY_LINE : CODE_OUTLINE_BY_KIND;
    update_sort_button(p);
    rebuild(p);
}

static void on_sort_clicked(GtkButton* button, gpointer data) {
    (void)button;
    toggle_sort(data);
}

static void on_collapse_clicked(GtkButton* button, gpointer data) {
    (void)button;
    toggle_collapse_all(data);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    OutlinePane* p = data;
    if (!p->detached) p->cb.close(p->data);
}

/* The owner's keys first (panel keys); Escape: back to the content; s: sort; c: collapse / expand all; q: hide
 * (Enter goes to the entry) */
static gboolean on_list_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    OutlinePane* p = data;
    if (p->detached) return FALSE;
    if (p->cb.key != NULL && p->cb.key(p->data, keyval, state)) return TRUE;
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0) return FALSE;
    switch (keyval) {
    case GDK_KEY_Escape: p->cb.escape(p->data); return TRUE;
    case GDK_KEY_s:
        if (p->entries != NULL && gtk_widget_get_visible(p->sort_button)) toggle_sort(p);
        return TRUE;
    case GDK_KEY_c: toggle_collapse_all(p); return TRUE;
    case GDK_KEY_q: p->cb.close(p->data); return TRUE;
    default: return FALSE;
    }
}

/* ------------------------------------------------------------------------- */
/* Public                                                                    */
/* ------------------------------------------------------------------------- */

OutlinePane* outline_pane_new(GtkWidget* content, gint* width, const gchar* loading,
                              const OutlinePaneCallbacks* callbacks, gpointer data) {
    OutlinePane* p = g_new0(OutlinePane, 1);
    p->cb = *callbacks;
    p->data = data;
    p->jumped = -1;
    p->width = width;
    p->sort = CODE_OUTLINE_BY_KIND;
    p->collapsed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    p->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(p->panel, "tagbar");
    gtk_widget_set_size_request(p->panel, MIN_WIDTH, -1);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(header, "tagbar-header");
    p->title = gtk_label_new(_("Symbols"));
    gtk_widget_add_css_class(p->title, "tagbar-title");
    gtk_label_set_xalign(GTK_LABEL(p->title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(p->title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(p->title, TRUE);
    gtk_box_append(GTK_BOX(header), p->title);
    p->sort_button = icon_button_new("view-list-ordered", NULL);
    update_sort_button(p);
    g_signal_connect(p->sort_button, "clicked", G_CALLBACK(on_sort_clicked), p);
    gtk_box_append(GTK_BOX(header), p->sort_button);
    p->collapse_button = icon_button_new("pan-up", NULL);
    update_collapse_button(p);
    g_signal_connect(p->collapse_button, "clicked", G_CALLBACK(on_collapse_clicked), p);
    gtk_box_append(GTK_BOX(header), p->collapse_button);
    GtkWidget* close = icon_button_new("window-close", _("Hide Tagbar (q)"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), p);
    gtk_box_append(GTK_BOX(header), close);
    gtk_box_append(GTK_BOX(p->panel), header);

    p->selection = gtk_single_selection_new(NULL);
    gtk_single_selection_set_autoselect(p->selection, FALSE);
    gtk_single_selection_set_can_unselect(p->selection, TRUE);
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), p);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), p);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_unbind), p);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), p);
    gtk_widget_add_controller(p->list, keys);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    p->empty = gtk_label_new(loading);
    gtk_widget_add_css_class(p->empty, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(p->empty), TRUE);
    gtk_widget_set_valign(p->empty, GTK_ALIGN_START);
    gtk_widget_set_margin_top(p->empty, 12);
    p->stack = gtk_stack_new();
    gtk_widget_set_vexpand(p->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(p->stack), scroll, "list");
    gtk_stack_add_named(GTK_STACK(p->stack), p->empty, "empty");
    gtk_stack_set_visible_child_name(GTK_STACK(p->stack), "empty");
    gtk_box_append(GTK_BOX(p->panel), p->stack);
    gtk_widget_set_visible(p->panel, FALSE);

    p->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(p->paned, TRUE);
    gtk_widget_set_vexpand(p->paned, TRUE);
    paned_use_thin_handle(p->paned);
    gtk_paned_set_start_child(GTK_PANED(p->paned), content);
    gtk_paned_set_resize_start_child(GTK_PANED(p->paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(p->paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(p->paned), p->panel);
    gtk_paned_set_resize_end_child(GTK_PANED(p->paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(p->paned), FALSE);
    g_signal_connect(p->paned, "notify::max-position", G_CALLBACK(on_max_position), p);
    g_signal_connect(p->paned, "notify::position", G_CALLBACK(on_position), p);
    g_object_set_data_full(G_OBJECT(p->paned), "outline-pane", p, (GDestroyNotify)pane_free);
    return p;
}

GtkWidget* outline_pane_widget(OutlinePane* p) {
    return p->paned;
}

void outline_pane_detach(OutlinePane* p) {
    if (p == NULL) return;
    p->detached = TRUE;
    p->data = NULL;
}

void outline_pane_set_shown(OutlinePane* p, gboolean shown) {
    if (shown == gtk_widget_get_visible(p->panel)) return;
    gtk_widget_set_visible(p->panel, shown);
    if (shown) place_handle(p);
}

void outline_pane_place(OutlinePane* p) {
    if (p != NULL && !p->detached) place_handle(p);
}

gboolean outline_pane_shown(OutlinePane* p) {
    return gtk_widget_get_visible(p->panel);
}

void outline_pane_focus(OutlinePane* p) {
    gtk_widget_grab_focus(p->list);
}

GtkWidget* outline_pane_list(OutlinePane* p) {
    return p->list;
}

gboolean outline_pane_has_focus(OutlinePane* p) {
    GtkRoot* root = gtk_widget_get_root(p->list);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    return focus != NULL && (focus == p->list || gtk_widget_is_ancestor(focus, p->list));
}
