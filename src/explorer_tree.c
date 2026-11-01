/* Explorer tree view (see explorer_tree.h) */
#include "explorer_tree.h"
#include "icons.h"

#include <string.h>

struct _ExplorerTree {
    GObject parent_instance;
    ExplorerRow* root;
    GtkTreeListModel* model;      /* owned by selection */
    GtkSingleSelection* selection; /* referenced (the view has its own reference) */
    GtkWidget* view;              /* GtkListView, referenced */
    ExplorerRow* last_selected;   /* referenced: restores the selection on collapse */
    ExplorerRow* toggling;        /* row expanded/collapsed by explorer_tree_set_expanded() */
    GdkFrameClock* scroll_clock;  /* referenced while a swapped-in scroll position waits for layout */
    gulong scroll_handler;
    gdouble pending_scroll;
};

G_DEFINE_FINAL_TYPE(ExplorerTree, explorer_tree, G_TYPE_OBJECT)

enum { SIGNAL_SELECTION_CHANGED, SIGNAL_ROW_TOGGLED, SIGNAL_ROW_ACTIVATED, N_SIGNALS };
static guint signals[N_SIGNALS];

static const gchar* ROW_KEY = "explorer-row";

/* ------------------------------------------------------------------------- */
/* Row widgets                                                               */
/* ------------------------------------------------------------------------- */

/* CSS class colouring the name for a git status badge (theme.c), NULL for
 * the default colour. A class, not markup, so selected rows can turn white. */
static const gchar* const GIT_CLASSES[] = {
    "git-modified", "git-added", "git-deleted", "git-conflict", "git-renamed"
};

static const gchar* git_status_class(const gchar* badge) {
    if (badge == NULL || badge[0] == '\0') return NULL;
    if (strstr(badge, ">M<") != NULL) return "git-modified";
    if (strstr(badge, ">U<") != NULL) return "git-added"; /* untracked */
    if (strstr(badge, ">A<") != NULL) return "git-added";
    if (strstr(badge, ">D<") != NULL) return "git-deleted";
    if (strstr(badge, ">!<") != NULL) return "git-conflict";
    if (strstr(badge, ">R<") != NULL) return "git-renamed";
    return NULL;
}

/* Name (coloured by git status) and badge. Bookmarks and remote machines show
 * their target / address right after the name in the same label, so the name
 * keeps priority and the target uses all the remaining width; other badges
 * (git status, counts, note type) are short and drawn at the right. */
static void render_row(GtkWidget* expander, ExplorerRow* row) {
    GtkWidget* icon = g_object_get_data(G_OBJECT(expander), "type-icon");
    GtkWidget* name = g_object_get_data(G_OBJECT(expander), "name-label");
    GtkWidget* badge = g_object_get_data(G_OBJECT(expander), "badge-label");
    gboolean has_badge = row->badge[0] != '\0';
    gboolean inline_badge = explorer_row_is(row, "bookmark") || explorer_row_is(row, "remote");

    /* Files show their VS Code file-type icon (their label is the bare
     * name); folders have none, as in VS Code; other rows have an emoji */
    gboolean is_file = explorer_row_is(row, "file");
    gtk_widget_set_visible(icon, is_file);
    if (is_file) {
        file_type_image_set(icon, row->name, FALSE);
    }

    gchar* name_esc = g_markup_escape_text(row->label, -1);
    if (inline_badge && has_badge) {
        gchar* markup = g_strdup_printf("%s  %s", name_esc, row->badge); /* badge is markup */
        gtk_label_set_markup(GTK_LABEL(name), markup);
        g_free(markup);
    } else {
        gtk_label_set_markup(GTK_LABEL(name), name_esc);
    }
    g_free(name_esc);

    const gchar* git_class = inline_badge ? NULL : git_status_class(row->badge);
    for (gsize i = 0; i < G_N_ELEMENTS(GIT_CLASSES); i++) {
        if (g_strcmp0(GIT_CLASSES[i], git_class) != 0) {
            gtk_widget_remove_css_class(name, GIT_CLASSES[i]);
        }
    }
    if (git_class != NULL) {
        gtk_widget_add_css_class(name, git_class);
    }

    gboolean show_badge = has_badge && !inline_badge;
    gtk_label_set_markup(GTK_LABEL(badge), show_badge ? row->badge : "");
    gtk_widget_set_visible(badge, show_badge);
}

static void on_row_changed(ExplorerRow* row, gpointer data) {
    render_row(GTK_WIDGET(data), row);
}

static void on_factory_setup(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    (void)data;
    GtkWidget* expander = gtk_tree_expander_new();
    /* Leaves keep the room of the expander arrow: names line up at each level */
    gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(expander), TRUE);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget* icon = file_type_image_new();
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget* name = gtk_label_new(NULL);
    gtk_widget_add_css_class(name, "row-name");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(name, TRUE); /* the name takes the free width */
    gtk_box_append(GTK_BOX(box), name);

    /* Short secondary text only (git badge, count, note type): capped and
     * ellipsized so it never squeezes the name */
    GtkWidget* badge = gtk_label_new(NULL);
    gtk_widget_add_css_class(badge, "row-badge");
    gtk_label_set_xalign(GTK_LABEL(badge), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(badge), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(badge), 8);
    gtk_box_append(GTK_BOX(box), badge);

    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    g_object_set_data(G_OBJECT(expander), "type-icon", icon);
    g_object_set_data(G_OBJECT(expander), "name-label", name);
    g_object_set_data(G_OBJECT(expander), "badge-label", badge);
    gtk_list_item_set_child(item, expander);
}

static void after_toggle(ExplorerTree* tree, ExplorerRow* row, gboolean expanded);

static GtkTreeListRow* list_row_for(ExplorerTree* tree, ExplorerRow* row);

/* The user clicked an expander. GTK also notifies "expanded" when it drops
 * rows (tree cleared, parent collapsed): those rows are no longer shown and
 * are ignored, they were not collapsed by the user. */
static void on_list_row_expanded(GtkTreeListRow* list_row, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    ExplorerTree* tree = EXPLORER_TREE(data);
    ExplorerRow* row = gtk_tree_list_row_get_item(list_row);
    if (row != NULL && row != tree->toggling) {
        GtkTreeListRow* shown = list_row_for(tree, row);
        if (shown == list_row) {
            after_toggle(tree, row, gtk_tree_list_row_get_expanded(list_row));
        }
        g_clear_object(&shown);
    }
    g_clear_object(&row);
}

static void on_factory_bind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    GtkTreeListRow* list_row = gtk_list_item_get_item(item);
    GtkWidget* expander = gtk_list_item_get_child(item);
    ExplorerRow* row = gtk_tree_list_row_get_item(list_row);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), list_row);
    g_object_set_data(G_OBJECT(expander), ROW_KEY, row);
    render_row(expander, row);
    g_signal_connect(row, "changed", G_CALLBACK(on_row_changed), expander);
    g_signal_connect(list_row, "notify::expanded", G_CALLBACK(on_list_row_expanded), data);
    g_object_unref(row); /* kept alive by list_row */
}

static void on_factory_unbind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    GtkTreeListRow* list_row = gtk_list_item_get_item(item);
    GtkWidget* expander = gtk_list_item_get_child(item);
    ExplorerRow* row = g_object_get_data(G_OBJECT(expander), ROW_KEY);
    if (row != NULL) {
        g_signal_handlers_disconnect_by_func(row, on_row_changed, expander);
    }
    if (list_row != NULL) {
        g_signal_handlers_disconnect_by_func(list_row, on_list_row_expanded, data);
    }
    g_object_set_data(G_OBJECT(expander), ROW_KEY, NULL);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), NULL);
}

/* ------------------------------------------------------------------------- */
/* Model                                                                     */
/* ------------------------------------------------------------------------- */

static GListModel* create_children(gpointer item, gpointer data) {
    (void)data;
    ExplorerRow* row = EXPLORER_ROW(item);
    return row->children != NULL ? G_LIST_MODEL(g_object_ref(row->children)) : NULL;
}

/* GtkTreeListRow of a row, NULL when it is hidden (new reference) */
static GtkTreeListRow* list_row_for(ExplorerTree* tree, ExplorerRow* row) {
    if (row == NULL || row->is_root || row->parent == NULL) {
        return NULL;
    }
    gint index = explorer_row_index(row);
    if (index < 0) {
        return NULL;
    }
    if (row->parent == tree->root) {
        return gtk_tree_list_model_get_child_row(tree->model, (guint)index);
    }
    GtkTreeListRow* parent = list_row_for(tree, row->parent);
    if (parent == NULL) {
        return NULL;
    }
    GtkTreeListRow* list_row = gtk_tree_list_row_get_child_row(parent, (guint)index);
    g_object_unref(parent);
    return list_row;
}

static void on_selected_item_changed(GtkSingleSelection* selection, GParamSpec* pspec, gpointer data) {
    (void)selection;
    (void)pspec;
    ExplorerTree* tree = EXPLORER_TREE(data);
    ExplorerRow* row = explorer_tree_get_selected(tree);
    if (row != NULL) {
        g_set_object(&tree->last_selected, row);
    }
    g_signal_emit(tree, signals[SIGNAL_SELECTION_CHANGED], 0);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    ExplorerTree* tree = EXPLORER_TREE(data);
    ExplorerRow* row = explorer_tree_visible_row(tree, position);
    if (row != NULL) {
        g_signal_emit(tree, signals[SIGNAL_ROW_ACTIVATED], 0, row);
    }
}

/* Collapsing a row drops its descendants from the list: if the selection was
 * one of them, select the collapsed row instead (like GtkTreeView did) */
static void after_toggle(ExplorerTree* tree, ExplorerRow* row, gboolean expanded) {
    if (!expanded && explorer_tree_get_selected(tree) == NULL && tree->last_selected != NULL &&
        tree->last_selected != row && explorer_row_is_descendant(tree->last_selected, row)) {
        explorer_tree_select(tree, row);
    }
    g_signal_emit(tree, signals[SIGNAL_ROW_TOGGLED], 0, row, expanded);
}

static void cancel_scroll_restore(ExplorerTree* tree) {
    if (tree->scroll_clock != NULL) {
        g_signal_handler_disconnect(tree->scroll_clock, tree->scroll_handler);
        tree->scroll_handler = 0;
        g_clear_object(&tree->scroll_clock);
    }
}

/* First layout after a swap: the rows of the new model are measured, so the
 * saved scroll offset now maps to the same rows; setting it queues another
 * layout pass, which GDK runs before painting this frame */
static void on_layout_restore_scroll(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    ExplorerTree* tree = EXPLORER_TREE(data);
    cancel_scroll_restore(tree);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tree->view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, tree->pending_scroll);
    }
}

static void explorer_tree_dispose(GObject* object) {
    ExplorerTree* tree = EXPLORER_TREE(object);
    cancel_scroll_restore(tree);
    g_clear_object(&tree->last_selected);
    g_clear_object(&tree->view);
    if (tree->selection != NULL) {
        g_signal_handlers_disconnect_by_func(tree->selection, on_selected_item_changed, tree);
        g_clear_object(&tree->selection);
    }
    g_clear_object(&tree->root);
    G_OBJECT_CLASS(explorer_tree_parent_class)->dispose(object);
}

static void explorer_tree_class_init(ExplorerTreeClass* klass) {
    G_OBJECT_CLASS(klass)->dispose = explorer_tree_dispose;
    signals[SIGNAL_SELECTION_CHANGED] =
        g_signal_new("selection-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_ROW_TOGGLED] =
        g_signal_new("row-toggled", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL, G_TYPE_NONE, 2, EXPLORER_TYPE_ROW, G_TYPE_BOOLEAN);
    signals[SIGNAL_ROW_ACTIVATED] =
        g_signal_new("row-activated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL, G_TYPE_NONE, 1, EXPLORER_TYPE_ROW);
}

/* Empty rows, model and selection (the content a state holds) */
static void content_init(ExplorerTree* tree) {
    tree->root = explorer_row_new_root();
    tree->model = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(tree->root->children)),
                                          FALSE, FALSE, create_children, NULL, NULL);
    tree->selection = gtk_single_selection_new(G_LIST_MODEL(tree->model));
    gtk_single_selection_set_autoselect(tree->selection, FALSE);
    gtk_single_selection_set_can_unselect(tree->selection, TRUE);
    gtk_single_selection_set_selected(tree->selection, GTK_INVALID_LIST_POSITION);
    tree->last_selected = NULL;
}

static void explorer_tree_init(ExplorerTree* tree) {
    content_init(tree);
    g_signal_connect(tree->selection, "notify::selected-item",
                     G_CALLBACK(on_selected_item_changed), tree);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_factory_setup), tree);
    g_signal_connect(factory, "bind", G_CALLBACK(on_factory_bind), tree);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_factory_unbind), tree);

    tree->view = g_object_ref_sink(gtk_list_view_new(
        GTK_SELECTION_MODEL(g_object_ref(tree->selection)), factory));
    g_signal_connect(tree->view, "activate", G_CALLBACK(on_activate), tree);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

ExplorerTree* explorer_tree_new(void) {
    return g_object_new(EXPLORER_TYPE_TREE, NULL);
}

GtkWidget* explorer_tree_get_widget(ExplorerTree* tree) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    return tree->view;
}

struct _ExplorerTreeState {
    ExplorerRow* root;
    GtkTreeListModel* model;
    GtkSingleSelection* selection;
    ExplorerRow* last_selected;
    gdouble scroll;
};

ExplorerTreeState* explorer_tree_swap_state(ExplorerTree* tree, ExplorerTreeState* state) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tree->view));

    ExplorerTreeState* out = g_new0(ExplorerTreeState, 1);
    out->root = tree->root;
    out->model = tree->model;
    out->selection = tree->selection;
    out->last_selected = tree->last_selected;
    out->scroll = (vadj != NULL) ? gtk_adjustment_get_value(vadj) : 0.0;
    g_signal_handlers_disconnect_by_func(tree->selection, on_selected_item_changed, tree);

    if (state != NULL) {
        tree->root = state->root;
        tree->model = state->model;
        tree->selection = state->selection;
        tree->last_selected = state->last_selected;
    } else {
        content_init(tree);
    }
    g_signal_connect(tree->selection, "notify::selected-item",
                     G_CALLBACK(on_selected_item_changed), tree);
    gtk_list_view_set_model(GTK_LIST_VIEW(tree->view), GTK_SELECTION_MODEL(tree->selection));

    cancel_scroll_restore(tree);
    tree->pending_scroll = (state != NULL) ? state->scroll : 0.0;
    GdkFrameClock* clock = gtk_widget_get_frame_clock(tree->view);
    if (clock != NULL) {
        /* Connected after the window's own layout handler: runs once the
         * rows are allocated */
        tree->scroll_clock = g_object_ref(clock);
        tree->scroll_handler = g_signal_connect(clock, "layout",
                                                G_CALLBACK(on_layout_restore_scroll), tree);
    } else if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, tree->pending_scroll);
    }
    g_free(state);
    return out;
}

void explorer_tree_state_free(ExplorerTreeState* state) {
    if (state == NULL) {
        return;
    }
    g_clear_object(&state->last_selected);
    g_clear_object(&state->selection); /* owns the model */
    g_clear_object(&state->root);
    g_free(state);
}

ExplorerRow* explorer_tree_get_root(ExplorerTree* tree) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    return tree->root;
}

void explorer_tree_clear(ExplorerTree* tree) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    g_clear_object(&tree->last_selected);
    explorer_row_remove_children(tree->root);
}

ExplorerRow* explorer_tree_get_selected(ExplorerTree* tree) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    GtkTreeListRow* list_row = gtk_single_selection_get_selected_item(tree->selection);
    if (list_row == NULL) {
        return NULL;
    }
    ExplorerRow* row = gtk_tree_list_row_get_item(list_row);
    if (row != NULL) {
        g_object_unref(row); /* kept alive by the tree */
    }
    return row;
}

gboolean explorer_tree_select(ExplorerTree* tree, ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), FALSE);
    gint pos = explorer_tree_position(tree, row);
    if (pos < 0) {
        return FALSE;
    }
    if (gtk_single_selection_get_selected(tree->selection) == (guint)pos) {
        /* Selecting the same row again changes nothing: notify listeners (the
         * preview) explicitly so the action still refreshes them */
        g_signal_emit(tree, signals[SIGNAL_SELECTION_CHANGED], 0);
    } else {
        gtk_single_selection_set_selected(tree->selection, (guint)pos);
    }
    /* Move the keyboard focus row along when the list has the focus */
    gboolean focused = (gtk_widget_get_state_flags(tree->view) & GTK_STATE_FLAG_FOCUS_WITHIN) != 0;
    gtk_list_view_scroll_to(GTK_LIST_VIEW(tree->view), (guint)pos,
                            focused ? GTK_LIST_SCROLL_FOCUS : GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

void explorer_tree_unselect_all(ExplorerTree* tree) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    gtk_single_selection_set_selected(tree->selection, GTK_INVALID_LIST_POSITION);
}

gboolean explorer_tree_is_expanded(ExplorerTree* tree, ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), FALSE);
    GtkTreeListRow* list_row = list_row_for(tree, row);
    gboolean expanded = list_row != NULL && gtk_tree_list_row_get_expanded(list_row);
    g_clear_object(&list_row);
    return expanded;
}

void explorer_tree_set_expanded(ExplorerTree* tree, ExplorerRow* row, gboolean expanded) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    if (row == NULL || row->children == NULL) {
        return;
    }
    GtkTreeListRow* list_row = list_row_for(tree, row);
    if (list_row == NULL || gtk_tree_list_row_get_expanded(list_row) == expanded) {
        g_clear_object(&list_row);
        return;
    }
    ExplorerRow* outer = tree->toggling;
    tree->toggling = row;
    gtk_tree_list_row_set_expanded(list_row, expanded);
    tree->toggling = outer;
    g_object_unref(list_row);
    after_toggle(tree, row, expanded);
}

void explorer_tree_toggle_expanded(ExplorerTree* tree, ExplorerRow* row) {
    explorer_tree_set_expanded(tree, row, !explorer_tree_is_expanded(tree, row));
}

void explorer_tree_expand_to(ExplorerTree* tree, ExplorerRow* row) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    ExplorerRow* parent = (row != NULL) ? explorer_row_get_parent(row) : NULL;
    if (parent != NULL) {
        explorer_tree_expand_to(tree, parent);
        explorer_tree_set_expanded(tree, parent, TRUE);
    }
}

static void expand_all_below(ExplorerTree* tree, ExplorerRow* parent) {
    for (guint i = 0; i < explorer_row_n_children(parent); i++) {
        ExplorerRow* child = explorer_row_child(parent, i);
        if (child->children != NULL) {
            explorer_tree_set_expanded(tree, child, TRUE);
            expand_all_below(tree, child);
        }
    }
}

void explorer_tree_expand_all(ExplorerTree* tree) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    expand_all_below(tree, tree->root);
}

void explorer_tree_collapse_all(ExplorerTree* tree) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    for (guint i = 0; i < explorer_row_n_children(tree->root); i++) {
        explorer_tree_set_expanded(tree, explorer_row_child(tree->root, i), FALSE);
    }
}

guint explorer_tree_n_visible(ExplorerTree* tree) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), 0);
    return g_list_model_get_n_items(G_LIST_MODEL(tree->model));
}

ExplorerRow* explorer_tree_visible_row(ExplorerTree* tree, guint position) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    GtkTreeListRow* list_row = gtk_tree_list_model_get_row(tree->model, position);
    if (list_row == NULL) {
        return NULL;
    }
    ExplorerRow* row = gtk_tree_list_row_get_item(list_row);
    g_object_unref(list_row);
    if (row != NULL) {
        g_object_unref(row); /* kept alive by the tree */
    }
    return row;
}

gint explorer_tree_position(ExplorerTree* tree, ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), -1);
    GtkTreeListRow* list_row = list_row_for(tree, row);
    if (list_row == NULL) {
        return -1;
    }
    gint pos = (gint)gtk_tree_list_row_get_position(list_row);
    g_object_unref(list_row);
    return pos;
}

/* Height of one row: rows all have the same height, so the list height
 * divided by the row count */
static gdouble row_height(ExplorerTree* tree, GtkAdjustment* vadj) {
    guint n = explorer_tree_n_visible(tree);
    gdouble upper = gtk_adjustment_get_upper(vadj);
    return (n > 0 && upper > 0) ? upper / n : 0;
}

void explorer_tree_scroll_to(ExplorerTree* tree, ExplorerRow* row, gdouble align) {
    g_return_if_fail(EXPLORER_IS_TREE(tree));
    gint pos = explorer_tree_position(tree, row);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tree->view));
    gdouble h = (vadj != NULL) ? row_height(tree, vadj) : 0;
    if (pos < 0 || h <= 0) {
        return;
    }
    gdouble page = gtk_adjustment_get_page_size(vadj);
    gdouble value = pos * h - align * (page - h);
    gdouble max = gtk_adjustment_get_upper(vadj) - page;
    gtk_adjustment_set_value(vadj, CLAMP(value, 0, MAX(max, 0)));
}

gint explorer_tree_page_rows(ExplorerTree* tree) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), 1);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tree->view));
    gdouble h = (vadj != NULL) ? row_height(tree, vadj) : 0;
    if (h <= 0) {
        return 10;
    }
    return MAX(1, (gint)(gtk_adjustment_get_page_size(vadj) / h));
}

ExplorerRow* explorer_tree_row_at(ExplorerTree* tree, gdouble x, gdouble y) {
    g_return_val_if_fail(EXPLORER_IS_TREE(tree), NULL);
    GtkWidget* w = gtk_widget_pick(tree->view, x, y, GTK_PICK_DEFAULT);
    for (; w != NULL && w != tree->view; w = gtk_widget_get_parent(w)) {
        ExplorerRow* row = g_object_get_data(G_OBJECT(w), ROW_KEY);
        if (row != NULL) {
            return row;
        }
        /* The row widget around the expander: look at its child */
        GtkWidget* child = gtk_widget_get_first_child(w);
        if (child != NULL && g_object_get_data(G_OBJECT(child), ROW_KEY) != NULL &&
            gtk_widget_get_parent(w) == tree->view) {
            return g_object_get_data(G_OBJECT(child), ROW_KEY);
        }
    }
    return NULL;
}
