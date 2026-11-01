/* Explorer tree rows (see explorer_row.h) */
#include "explorer_row.h"

G_DEFINE_FINAL_TYPE(ExplorerRow, explorer_row, G_TYPE_OBJECT)

enum { SIGNAL_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

/* Children outliving their parent (held by a view) must not point to it */
static void detach_children(ExplorerRow* row) {
    if (row->children == NULL) {
        return;
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(row->children));
    for (guint i = 0; i < n; i++) {
        ExplorerRow* child = g_list_model_get_item(G_LIST_MODEL(row->children), i);
        child->parent = NULL;
        g_object_unref(child);
    }
}

static void explorer_row_finalize(GObject* object) {
    ExplorerRow* row = EXPLORER_ROW(object);
    detach_children(row);
    g_clear_object(&row->children);
    g_free(row->type);
    g_free(row->name);
    g_free(row->label);
    g_free(row->icon_name);
    g_free(row->path);
    g_free(row->badge);
    G_OBJECT_CLASS(explorer_row_parent_class)->finalize(object);
}

static void explorer_row_class_init(ExplorerRowClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = explorer_row_finalize;
    signals[SIGNAL_CHANGED] = g_signal_new("changed", G_TYPE_FROM_CLASS(klass),
                                           G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                           G_TYPE_NONE, 0);
}

static void explorer_row_init(ExplorerRow* row) {
    row->type = g_strdup("");
    row->name = g_strdup("");
    row->label = g_strdup("");
    row->icon_name = g_strdup("");
    row->path = g_strdup("");
    row->badge = g_strdup("");
    row->loaded = TRUE;
}

ExplorerRow* explorer_row_new_root(void) {
    ExplorerRow* root = g_object_new(EXPLORER_TYPE_ROW, NULL);
    root->is_root = TRUE;
    root->children = g_list_store_new(EXPLORER_TYPE_ROW);
    return root;
}

static void set_string(gchar** field, const gchar* value) {
    g_free(*field);
    *field = g_strdup(value != NULL ? value : "");
}

static ExplorerRow* append_row(ExplorerRow* parent, gboolean container, const gchar* type,
                               const gchar* name, const gchar* label, const gchar* path) {
    g_return_val_if_fail(EXPLORER_IS_ROW(parent), NULL);
    g_return_val_if_fail(parent->children != NULL, NULL); /* parent must be a container */
    ExplorerRow* row = g_object_new(EXPLORER_TYPE_ROW, NULL);
    set_string(&row->type, type);
    set_string(&row->name, name);
    set_string(&row->label, label);
    set_string(&row->path, path);
    row->parent = parent;
    if (container) {
        /* Before insertion: the view checks once whether a new row can expand */
        row->children = g_list_store_new(EXPLORER_TYPE_ROW);
    }
    g_list_store_append(parent->children, row);
    g_object_unref(row); /* owned by parent->children */
    return row;
}

ExplorerRow* explorer_row_append(ExplorerRow* parent, const gchar* type, const gchar* name,
                                 const gchar* label, const gchar* path) {
    return append_row(parent, FALSE, type, name, label, path);
}

ExplorerRow* explorer_row_append_container(ExplorerRow* parent, const gchar* type,
                                           const gchar* name, const gchar* label,
                                           const gchar* path) {
    return append_row(parent, TRUE, type, name, label, path);
}

void explorer_row_remove_children(ExplorerRow* row) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    if (row->children != NULL) {
        detach_children(row);
        g_list_store_remove_all(row->children);
    }
}

void explorer_row_remove(ExplorerRow* row) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    ExplorerRow* parent = row->parent;
    if (parent == NULL || parent->children == NULL) {
        return;
    }
    guint pos = 0;
    if (g_list_store_find(parent->children, row, &pos)) {
        row->parent = NULL;
        g_list_store_remove(parent->children, pos);
    }
}

guint explorer_row_n_children(ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_ROW(row), 0);
    return row->children != NULL ? g_list_model_get_n_items(G_LIST_MODEL(row->children)) : 0;
}

ExplorerRow* explorer_row_child(ExplorerRow* row, guint i) {
    g_return_val_if_fail(EXPLORER_IS_ROW(row), NULL);
    if (row->children == NULL) {
        return NULL;
    }
    ExplorerRow* child = g_list_model_get_item(G_LIST_MODEL(row->children), i);
    if (child != NULL) {
        g_object_unref(child); /* still owned by row->children */
    }
    return child;
}

ExplorerRow* explorer_row_get_parent(ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_ROW(row), NULL);
    return (row->parent != NULL && !row->parent->is_root) ? row->parent : NULL;
}

gint explorer_row_index(ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_ROW(row), -1);
    guint pos = 0;
    if (row->parent != NULL && row->parent->children != NULL &&
        g_list_store_find(row->parent->children, row, &pos)) {
        return (gint)pos;
    }
    return -1;
}

guint explorer_row_depth(ExplorerRow* row) {
    g_return_val_if_fail(EXPLORER_IS_ROW(row), 0);
    guint depth = 0;
    for (ExplorerRow* p = row; p != NULL && !p->is_root; p = p->parent) {
        depth++;
    }
    return depth;
}

gboolean explorer_row_is_descendant(ExplorerRow* row, ExplorerRow* ancestor) {
    for (ExplorerRow* p = row; p != NULL; p = p->parent) {
        if (p == ancestor) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean explorer_row_is(ExplorerRow* row, const gchar* type) {
    return row != NULL && g_strcmp0(row->type, type) == 0;
}

void explorer_row_set_label(ExplorerRow* row, const gchar* label) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    if (g_strcmp0(row->label, label != NULL ? label : "") != 0) {
        set_string(&row->label, label);
        g_signal_emit(row, signals[SIGNAL_CHANGED], 0);
    }
}

void explorer_row_set_badge(ExplorerRow* row, const gchar* badge) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    if (g_strcmp0(row->badge, badge != NULL ? badge : "") != 0) {
        set_string(&row->badge, badge);
        g_signal_emit(row, signals[SIGNAL_CHANGED], 0);
    }
}

void explorer_row_set_icon_name(ExplorerRow* row, const gchar* icon_name) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    if (g_strcmp0(row->icon_name, icon_name != NULL ? icon_name : "") != 0) {
        set_string(&row->icon_name, icon_name);
        g_signal_emit(row, signals[SIGNAL_CHANGED], 0);
    }
}

void explorer_row_set_loaded(ExplorerRow* row, gboolean loaded) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    row->loaded = loaded;
}

void explorer_row_set_bm_id(ExplorerRow* row, guint id) {
    g_return_if_fail(EXPLORER_IS_ROW(row));
    row->bm_id = id;
}

ExplorerRow* explorer_row_find(ExplorerRow* from, ExplorerRowMatch match, gpointer data) {
    g_return_val_if_fail(EXPLORER_IS_ROW(from), NULL);
    guint n = explorer_row_n_children(from);
    for (guint i = 0; i < n; i++) {
        ExplorerRow* child = explorer_row_child(from, i);
        if (match(child, data)) {
            return child;
        }
        ExplorerRow* found = explorer_row_find(child, match, data);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static gboolean match_path(ExplorerRow* row, gpointer data) {
    return g_strcmp0(row->path, (const gchar*)data) == 0;
}

ExplorerRow* explorer_row_find_by_path(ExplorerRow* from, const gchar* path) {
    if (path == NULL) {
        return NULL;
    }
    return explorer_row_find(from, match_path, (gpointer)path);
}
