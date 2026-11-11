/* A tagbar beside some content (see outline_pane.h) */
#include "widgets/outline_pane.h"
#include "theme/icons.h"
#include "core/i18n.h"
#include "widgets/panels.h"

#include <string.h>

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
    NavPrefix prefix;        /* Vim g / z / Ctrl+W, Emacs C-x typed in the list */
    gint recenter_step;      /* Emacs C-l: center, top, bottom in turn */
    GtkWidget* search_bar;   /* at the bottom, shown while searching */
    GtkWidget* search_prompt;
    GtkWidget* search_entry;
    GtkWidget* search_echo;
    NavStyle search_style;   /* of the open search */
    gboolean search_backward;
    gboolean search_setting; /* the entry text is being set by the code */
    gchar* search_term;      /* last text searched, or NULL */
    OutlineItem* search_origin; /* selected when the search started (referenced), or NULL */
};

static void pane_free(OutlinePane* p) {
    if (p->collapse_idle != 0) g_source_remove(p->collapse_idle);
    g_clear_object(&p->tree);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    g_hash_table_unref(p->collapsed);
    g_free(p->search_term);
    g_clear_object(&p->search_origin);
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
    /* G_MAXINT until the paned is allocated */
    if (max <= 0 || max == G_MAXINT) return 0;
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

/* ------------------------------------------------------------------------- */
/* Moving: the navigation keys of the mode                                   */
/* ------------------------------------------------------------------------- */

/* Select the row at pos and scroll to it; with focus, the keyboard focus
 * goes to it too (not while typing in the search bar) */
static void select_pos(OutlinePane* p, guint pos, gboolean focus) {
    gtk_single_selection_set_selected(p->selection, pos);
    if (pos == GTK_INVALID_LIST_POSITION) return;
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), pos, focus ? GTK_LIST_SCROLL_FOCUS : GTK_LIST_SCROLL_NONE, NULL);
}

/* The item of the selected row (referenced), or NULL */
static OutlineItem* selected_item(OutlinePane* p) {
    GtkTreeListRow* row = gtk_single_selection_get_selected_item(p->selection);
    return row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
}

/* Position of the row showing item, or GTK_INVALID_LIST_POSITION */
static guint row_of(OutlinePane* p, gpointer item) {
    if (p->tree == NULL || item == NULL) return GTK_INVALID_LIST_POSITION;
    GListModel* rows = G_LIST_MODEL(p->tree);
    guint n = g_list_model_get_n_items(rows);
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow* row = g_list_model_get_item(rows, i);
        gpointer it = gtk_tree_list_row_get_item(row);
        g_object_unref(row);
        if (it != NULL) g_object_unref(it);
        if (it == item) return i;
    }
    return GTK_INVALID_LIST_POSITION;
}

/* Rows shown at once, from the scroll adjustment */
static guint page_rows(OutlinePane* p, guint n) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(p->list));
    gdouble upper = adj != NULL ? gtk_adjustment_get_upper(adj) : 0.0;
    if (upper <= 0.0 || n == 0) return 10;
    guint rows = (guint)(gtk_adjustment_get_page_size(adj) * n / upper);
    return rows > 1 ? rows - 1 : 1; /* one row of the page stays in view */
}

/* Scroll so that the selected row is at where of the view (0 top, 0.5
 * middle, 1 bottom: Vim zt / zz / zb, Emacs C-l) */
static void scroll_selected(OutlinePane* p, gdouble where) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->selection));
    guint pos = gtk_single_selection_get_selected(p->selection);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(p->list));
    if (adj == NULL || n == 0 || pos >= n) return;
    gdouble row = gtk_adjustment_get_upper(adj) / n;
    gdouble page = gtk_adjustment_get_page_size(adj);
    gtk_adjustment_set_value(adj, pos * row - (page - row) * where);
}

/* Next / previous row, first / last, a page down / up; forward opens the
 * row (or steps into it), backward closes it (or goes to its parent) */
static void move(OutlinePane* p, NavAction action) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->selection));
    guint cur = gtk_single_selection_get_selected(p->selection);
    gboolean none = cur == GTK_INVALID_LIST_POSITION || cur >= n;
    if (n == 0) return;
    switch (action) {
    case NAV_DOWN: select_pos(p, none ? 0 : MIN(cur + 1, n - 1), TRUE); break;
    case NAV_UP: select_pos(p, none || cur == 0 ? 0 : cur - 1, TRUE); break;
    case NAV_FIRST: select_pos(p, 0, TRUE); break;
    case NAV_LAST: select_pos(p, n - 1, TRUE); break;
    case NAV_PAGE_DOWN: select_pos(p, none ? 0 : MIN(cur + page_rows(p, n), n - 1), TRUE); break;
    case NAV_PAGE_UP: {
        guint rows = page_rows(p, n);
        select_pos(p, none || cur < rows ? 0 : cur - rows, TRUE);
        break;
    }
    case NAV_FORWARD:
    case NAV_BACKWARD: {
        if (none) break;
        GtkTreeListRow* row = g_list_model_get_item(G_LIST_MODEL(p->tree), cur);
        gboolean expandable = gtk_tree_list_row_is_expandable(row);
        gboolean expanded = gtk_tree_list_row_get_expanded(row);
        if (action == NAV_FORWARD) {
            if (expandable && !expanded) {
                set_row_expanded(p, row, TRUE);
            } else if (expanded && cur + 1 < n) {
                select_pos(p, cur + 1, TRUE);
            }
        } else if (expandable && expanded) {
            set_row_expanded(p, row, FALSE);
        } else {
            GtkTreeListRow* parent = gtk_tree_list_row_get_parent(row);
            if (parent != NULL) {
                select_pos(p, gtk_tree_list_row_get_position(parent), TRUE);
                g_object_unref(parent);
            }
        }
        g_object_unref(row);
        queue_collapse_button(p);
        break;
    }
    case NAV_CENTER: scroll_selected(p, 0.5); break;
    case NAV_TOP: scroll_selected(p, 0.0); break;
    case NAV_BOTTOM: scroll_selected(p, 1.0); break;
    case NAV_RECENTER: {
        static const gdouble where[] = {0.5, 0.0, 1.0};
        scroll_selected(p, where[p->recenter_step]);
        p->recenter_step = (p->recenter_step + 1) % 3;
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------------- */
/* Searching the names                                                       */
/* ------------------------------------------------------------------------- */

typedef enum { FOUND, WRAPPED, FAILED } SearchResult;

/* Every item in the order of the rows, collapsed ones included, with the
 * index of its parent (-1 at the top) */
static void flatten(GListModel* model, gint parent, GPtrArray* items, GArray* parents) {
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        OutlineItem* it = g_list_model_get_item(model, i);
        g_ptr_array_add(items, it);
        g_array_append_val(parents, parent);
        gint index = (gint)items->len - 1;
        if (it->children != NULL) flatten(G_LIST_MODEL(it->children), index, items, parents);
    }
}

static gboolean item_matches(const OutlineItem* it, const gchar* folded) {
    if (it == NULL || it->group || it->name == NULL || folded[0] == '\0') return FALSE;
    gchar* name = g_utf8_casefold(it->name, -1);
    gboolean match = strstr(name, folded) != NULL;
    g_free(name);
    return match;
}

/* Open the rows above items[index] and select it */
static void reveal(OutlinePane* p, GPtrArray* items, GArray* parents, guint index, gboolean focus) {
    GSList* chain = NULL;
    for (gint i = g_array_index(parents, gint, index); i >= 0; i = g_array_index(parents, gint, i)) {
        chain = g_slist_prepend(chain, g_ptr_array_index(items, i));
    }
    for (GSList* l = chain; l != NULL; l = l->next) {
        guint pos = row_of(p, l->data);
        if (pos == GTK_INVALID_LIST_POSITION) break;
        GtkTreeListRow* row = g_list_model_get_item(G_LIST_MODEL(p->tree), pos);
        if (!gtk_tree_list_row_get_expanded(row)) set_row_expanded(p, row, TRUE);
        g_object_unref(row);
    }
    g_slist_free(chain);
    queue_collapse_button(p);
    select_pos(p, row_of(p, g_ptr_array_index(items, index)), focus);
}

/* The next name holding term in direction (+1 / -1), after the row
 * selected (from_origin: the one selected when the search started),
 * wrapping around */
static SearchResult search_find(OutlinePane* p, const gchar* term, gint direction, gboolean from_origin,
                                gboolean focus) {
    if (p->tree == NULL || term == NULL || term[0] == '\0') return FAILED;
    GPtrArray* items = g_ptr_array_new_with_free_func(g_object_unref);
    GArray* parents = g_array_new(FALSE, FALSE, sizeof(gint));
    flatten(gtk_tree_list_model_get_model(p->tree), -1, items, parents);
    OutlineItem* from = from_origin ? (p->search_origin != NULL ? g_object_ref(p->search_origin) : NULL)
                                    : selected_item(p);
    gint n = (gint)items->len;
    gint start = -1;
    for (gint i = 0; from != NULL && i < n && start < 0; i++) {
        if (g_ptr_array_index(items, i) == (gpointer)from) start = i;
    }
    if (from != NULL) g_object_unref(from);
    gchar* folded = g_utf8_casefold(term, -1);
    SearchResult result = FAILED;
    for (gint k = 1; k <= n && result == FAILED; k++) {
        gint i = start < 0 ? (direction > 0 ? k - 1 : n - k) : ((start + direction * k) % n + n) % n;
        if (!item_matches(g_ptr_array_index(items, i), folded)) continue;
        gboolean wrapped = start >= 0 && (direction > 0 ? i <= start : i >= start);
        result = wrapped ? WRAPPED : FOUND;
        reveal(p, items, parents, (guint)i, focus);
    }
    g_free(folded);
    g_array_unref(parents);
    g_ptr_array_unref(items);
    return result;
}

static const gchar* const BAR_CLASSES[] = {"notepad-bar", "emacs-minibuffer", "vim-commandline"};
static const gchar* const PROMPT_CLASSES[] = {"emacs-minibuffer-prompt", "vim-commandline-prompt"};
static const gchar* const ENTRY_CLASSES[] = {"emacs-minibuffer-entry", "vim-commandline-entry", "flat"};
static const gchar* const ECHO_CLASSES[] = {"vim-message-error", "dim-label"};

static void remove_classes(GtkWidget* w, const gchar* const* classes, gsize n) {
    for (gsize i = 0; i < n; i++) gtk_widget_remove_css_class(w, classes[i]);
}

static void set_echo(OutlinePane* p, const gchar* text, const gchar* css_class) {
    remove_classes(p->search_echo, ECHO_CLASSES, G_N_ELEMENTS(ECHO_CLASSES));
    if (css_class != NULL) gtk_widget_add_css_class(p->search_echo, css_class);
    gtk_label_set_text(GTK_LABEL(p->search_echo), text != NULL ? text : "");
    gtk_widget_set_visible(p->search_echo, text != NULL && text[0] != '\0');
}

/* Prompt and message the way the mode's search does (search.c) */
static void search_report(OutlinePane* p, SearchResult result) {
    const gchar* term = gtk_editable_get_text(GTK_EDITABLE(p->search_entry));
    gboolean empty = term[0] == '\0';
    gchar* msg = NULL;
    switch (p->search_style) {
    case NAV_STYLE_EMACS: {
        gboolean failing = !empty && result == FAILED;
        gboolean wrapped = !empty && result == WRAPPED;
        gchar* prompt = g_strdup_printf("%s%sI-search%s: ", failing ? "Failing " : "",
                                        wrapped ? "Wrapped " : "", p->search_backward ? " backward" : "");
        gtk_label_set_text(GTK_LABEL(p->search_prompt), prompt);
        g_free(prompt);
        set_echo(p, NULL, NULL);
        break;
    }
    case NAV_STYLE_VIM:
        gtk_label_set_text(GTK_LABEL(p->search_prompt), "/");
        if (!empty && result == FAILED) {
            msg = g_strdup_printf("E486: Pattern not found: %s", term);
            set_echo(p, msg, "vim-message-error");
        } else if (!empty && result == WRAPPED) {
            set_echo(p, p->search_backward ? "search hit TOP, continuing at BOTTOM"
                                           : "search hit BOTTOM, continuing at TOP",
                     "vim-message-error");
        } else {
            set_echo(p, NULL, NULL);
        }
        break;
    default:
        gtk_label_set_text(GTK_LABEL(p->search_prompt), _("Find:"));
        if (!empty && result == FAILED) msg = g_strdup_printf(_("Cannot find \"%s\""), term);
        set_echo(p, msg, "dim-label");
        break;
    }
    g_free(msg);
}

static void search_open(OutlinePane* p, NavStyle style, gboolean backward) {
    p->search_style = style;
    p->search_backward = backward;
    g_clear_object(&p->search_origin);
    p->search_origin = selected_item(p);
    remove_classes(p->search_bar, BAR_CLASSES, G_N_ELEMENTS(BAR_CLASSES));
    remove_classes(p->search_prompt, PROMPT_CLASSES, G_N_ELEMENTS(PROMPT_CLASSES));
    remove_classes(p->search_entry, ENTRY_CLASSES, G_N_ELEMENTS(ENTRY_CLASSES));
    gtk_widget_set_margin_end(p->search_prompt, style == NAV_STYLE_NOTEPAD ? 6 : 0);
    switch (style) {
    case NAV_STYLE_EMACS:
        gtk_widget_add_css_class(p->search_bar, "emacs-minibuffer");
        gtk_widget_add_css_class(p->search_prompt, "emacs-minibuffer-prompt");
        gtk_widget_add_css_class(p->search_entry, "emacs-minibuffer-entry");
        gtk_widget_add_css_class(p->search_entry, "flat");
        break;
    case NAV_STYLE_VIM:
        gtk_widget_add_css_class(p->search_bar, "vim-commandline");
        gtk_widget_add_css_class(p->search_prompt, "vim-commandline-prompt");
        gtk_widget_add_css_class(p->search_entry, "vim-commandline-entry");
        gtk_widget_add_css_class(p->search_entry, "flat");
        break;
    default:
        gtk_widget_add_css_class(p->search_bar, "notepad-bar");
        break;
    }
    p->search_setting = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(p->search_entry), "");
    p->search_setting = FALSE;
    search_report(p, FOUND);
    gtk_widget_set_visible(p->search_bar, TRUE);
    gtk_widget_grab_focus(p->search_entry);
}

/* Close the bar, the focus back on the selected row (restore: on the one
 * selected when the search started) */
static void search_close(OutlinePane* p, gboolean restore) {
    if (!gtk_widget_get_visible(p->search_bar)) return;
    if (restore) select_pos(p, row_of(p, p->search_origin), FALSE);
    g_clear_object(&p->search_origin);
    gtk_widget_set_visible(p->search_bar, FALSE);
    gtk_widget_grab_focus(p->list);
    guint pos = gtk_single_selection_get_selected(p->selection);
    if (pos != GTK_INVALID_LIST_POSITION) select_pos(p, pos, TRUE);
}

/* As you type: the selected name while it still holds the text, else the
 * next one from where the search started */
static void on_search_changed(GtkEditable* entry, gpointer data) {
    OutlinePane* p = data;
    if (p->search_setting || p->detached) return;
    const gchar* term = gtk_editable_get_text(entry);
    if (term[0] == '\0') {
        select_pos(p, row_of(p, p->search_origin), FALSE);
        search_report(p, FOUND);
        return;
    }
    g_free(p->search_term);
    p->search_term = g_strdup(term);
    OutlineItem* cur = selected_item(p);
    gchar* folded = g_utf8_casefold(term, -1);
    gboolean keep = cur != NULL && cur != p->search_origin && item_matches(cur, folded);
    g_free(folded);
    if (cur != NULL) g_object_unref(cur);
    search_report(p, keep ? FOUND : search_find(p, term, p->search_backward ? -1 : 1, TRUE, FALSE));
}

/* Keys of the mode in the bar (nav_keys_search_lookup()): next / previous
 * match (Emacs C-s / C-r, Notepad Enter / F3 / Shift+F3; on an empty bar,
 * the last text), Enter / Escape close it on the match, C-g goes back */
static gboolean on_search_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                              gpointer data) {
    (void)ctrl;
    (void)keycode;
    OutlinePane* p = data;
    if (p->detached) return FALSE;
    NavSearchKey key = nav_keys_search_lookup(p->search_style, keyval, state);
    switch (key) {
    case NAV_SEARCH_KEY_NEXT:
    case NAV_SEARCH_KEY_PREV: {
        p->search_backward = key == NAV_SEARCH_KEY_PREV;
        const gchar* term = gtk_editable_get_text(GTK_EDITABLE(p->search_entry));
        if (term[0] == '\0') {
            /* on_search_changed() searches it */
            if (p->search_term != NULL) gtk_editable_set_text(GTK_EDITABLE(p->search_entry), p->search_term);
            gtk_editable_set_position(GTK_EDITABLE(p->search_entry), -1);
        } else {
            search_report(p, search_find(p, term, p->search_backward ? -1 : 1, FALSE, FALSE));
        }
        return TRUE;
    }
    case NAV_SEARCH_KEY_ACCEPT: search_close(p, FALSE); return TRUE;
    case NAV_SEARCH_KEY_CANCEL: search_close(p, TRUE); return TRUE;
    default: return FALSE;
    }
}

/* Vim n / N, Notepad F3 / Shift+F3 in the list: the last text again (Vim N
 * the other way) */
static void search_again(OutlinePane* p, NavStyle style, gboolean next) {
    gint direction = next ? 1 : -1;
    if (style == NAV_STYLE_VIM && p->search_backward) direction = -direction;
    if (search_find(p, p->search_term, direction, FALSE, TRUE) == FAILED) gtk_widget_error_bell(p->list);
}

/* ------------------------------------------------------------------------- */
/* Keys of the list                                                          */
/* ------------------------------------------------------------------------- */

/* A prefix typed before the focus left does not carry over */
static void on_list_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    OutlinePane* p = data;
    p->prefix = NAV_PREFIX_NONE;
    p->recenter_step = 0;
}

/* The navigation keys of the owner's mode (moving, searching, panel keys);
 * Escape: back to the content; s: sort; c: collapse / expand all; q: hide
 * (Enter goes to the entry) */
static gboolean on_list_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    OutlinePane* p = data;
    if (p->detached) return FALSE;
    NavStyle style = p->cb.nav_style != NULL ? p->cb.nav_style(p->data) : NAV_STYLE_NOTEPAD;
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(style, p->prefix, keyval, state, &next);
    p->prefix = next;
    if (action != NAV_RECENTER && action != NAV_PENDING) p->recenter_step = 0;
    switch (action) {
    case NAV_NONE:
        break;
    case NAV_DOWN: case NAV_UP: case NAV_FORWARD: case NAV_BACKWARD:
    case NAV_FIRST: case NAV_LAST: case NAV_PAGE_DOWN: case NAV_PAGE_UP:
    case NAV_CENTER: case NAV_TOP: case NAV_BOTTOM: case NAV_RECENTER:
        move(p, action);
        return TRUE;
    case NAV_SEARCH:
    case NAV_SEARCH_BACKWARD:
        search_open(p, style, action == NAV_SEARCH_BACKWARD);
        return TRUE;
    case NAV_SEARCH_NEXT:
    case NAV_SEARCH_PREV:
        search_again(p, style, action == NAV_SEARCH_NEXT);
        return TRUE;
    case NAV_PANEL_NEXT: case NAV_PANEL_PREV:
    case NAV_TAB_NEXT: case NAV_TAB_PREV: case NAV_TAB_CLOSE:
        if (p->cb.panel_action != NULL) p->cb.panel_action(p->data, action);
        return TRUE;
    default:
        return TRUE; /* prefixes, and keys with no use here */
    }
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
    /* Before the list's own keys: the mode decides what the arrows do */
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), p);
    gtk_widget_add_controller(p->list, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_list_focus_enter), p);
    gtk_widget_add_controller(p->list, focus);
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

    /* Search bar: prompt and text, the message below */
    p->search_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    p->search_prompt = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(search_row), p->search_prompt);
    p->search_entry = gtk_entry_new();
    gtk_widget_set_hexpand(p->search_entry, TRUE);
    g_signal_connect(p->search_entry, "changed", G_CALLBACK(on_search_changed), p);
    GtkEventController* search_keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(search_keys, GTK_PHASE_CAPTURE);
    g_signal_connect(search_keys, "key-pressed", G_CALLBACK(on_search_key), p);
    gtk_widget_add_controller(p->search_entry, search_keys);
    gtk_box_append(GTK_BOX(search_row), p->search_entry);
    gtk_box_append(GTK_BOX(p->search_bar), search_row);
    p->search_echo = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(p->search_echo), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(p->search_echo), PANGO_ELLIPSIZE_END);
    gtk_widget_set_visible(p->search_echo, FALSE);
    gtk_box_append(GTK_BOX(p->search_bar), p->search_echo);
    gtk_widget_set_visible(p->search_bar, FALSE);
    gtk_box_append(GTK_BOX(p->panel), p->search_bar);
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
