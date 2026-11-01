/* Unit tests for src/explorer_row.c: the explorer row tree (children, parent
 * links, lookup, "changed" notifications). */
#include "explorer_row.h"

static void on_changed(ExplorerRow* row, gpointer data) {
    (void)row;
    (*(gint*)data)++;
}

static void test_append_and_parents(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* dir = explorer_row_append_container(root, "dir", "src", "📁 src", "/w/src");
    ExplorerRow* file = explorer_row_append(dir, "file", "main.c", "📄 main.c", "/w/src/main.c");
    ExplorerRow* top = explorer_row_append(root, "file", "README", NULL, NULL);

    g_assert_cmpuint(explorer_row_n_children(root), ==, 2);
    g_assert_true(explorer_row_child(root, 0) == dir);
    g_assert_null(explorer_row_child(root, 2));
    g_assert_cmpstr(top->label, ==, "");  /* NULL stored as "" */
    g_assert_cmpstr(top->badge, ==, "");
    g_assert_true(top->loaded);

    /* Top-level rows have no visible parent */
    g_assert_null(explorer_row_get_parent(dir));
    g_assert_true(explorer_row_get_parent(file) == dir);
    g_assert_cmpuint(explorer_row_depth(dir), ==, 1);
    g_assert_cmpuint(explorer_row_depth(file), ==, 2);
    g_assert_cmpint(explorer_row_index(top), ==, 1);
    g_assert_true(explorer_row_is_descendant(file, dir));
    g_assert_false(explorer_row_is_descendant(dir, file));

    /* Only containers can be expanded (have a children list) */
    g_assert_nonnull(dir->children);
    g_assert_null(file->children);
    g_object_unref(root);
}

static void test_remove(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* dir = explorer_row_append_container(root, "dir", "a", "a", "/a");
    ExplorerRow* child = explorer_row_append(dir, "file", "b", "b", "/a/b");
    g_object_ref(child); /* held by a view after removal */

    explorer_row_remove_children(dir);
    g_assert_cmpuint(explorer_row_n_children(dir), ==, 0);
    g_assert_nonnull(dir->children); /* still expandable */
    g_assert_null(child->parent);    /* detached, no dangling parent */
    g_assert_cmpint(explorer_row_index(child), ==, -1);
    g_object_unref(child);

    ExplorerRow* kept = g_object_ref(explorer_row_append(dir, "file", "c", "c", "/a/c"));
    g_object_ref(dir);
    explorer_row_remove(dir);
    g_assert_cmpuint(explorer_row_n_children(root), ==, 0);
    g_object_unref(dir); /* finalized: its children must not point to it */
    g_assert_null(kept->parent);
    g_object_unref(kept);
    g_object_unref(root);
}

static gboolean match_name(ExplorerRow* row, gpointer data) {
    return g_strcmp0(row->name, (const gchar*)data) == 0;
}

static void test_find(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* a = explorer_row_append_container(root, "dir", "a", "a", "/a");
    ExplorerRow* deep = explorer_row_append(a, "file", "x", "x", "/a/x");
    explorer_row_append(root, "file", "b", "b", "/b");

    g_assert_true(explorer_row_find_by_path(root, "/a/x") == deep);
    g_assert_null(explorer_row_find_by_path(root, "/nope"));
    g_assert_null(explorer_row_find_by_path(root, NULL));
    g_assert_true(explorer_row_find(root, match_name, "x") == deep);
    g_assert_null(explorer_row_find(a, match_name, "a")); /* `from` excluded */
    g_object_unref(root);
}

static void test_changed_signal(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* row = explorer_row_append(root, "file", "f", "f", "/f");
    gint count = 0;
    g_signal_connect(row, "changed", G_CALLBACK(on_changed), &count);

    explorer_row_set_label(row, "📄 f");
    explorer_row_set_badge(row, "<b>M</b>");
    g_assert_cmpint(count, ==, 2);
    explorer_row_set_label(row, "📄 f");  /* unchanged: no notification */
    explorer_row_set_badge(row, NULL);    /* NULL = "" */
    g_assert_cmpint(count, ==, 3);
    g_assert_cmpstr(row->badge, ==, "");
    g_assert_cmpstr(row->icon_name, ==, "");
    explorer_row_set_icon_name(row, "f.c");
    explorer_row_set_icon_name(row, "f.c"); /* unchanged */
    g_assert_cmpint(count, ==, 4);
    explorer_row_set_icon_name(row, NULL);
    g_assert_cmpstr(row->icon_name, ==, "");
    g_assert_cmpint(count, ==, 5);

    explorer_row_set_loaded(row, FALSE);
    explorer_row_set_bm_id(row, 7);
    g_assert_false(row->loaded);
    g_assert_cmpuint(row->bm_id, ==, 7);
    g_assert_cmpint(count, ==, 5);
    g_assert_true(explorer_row_is(row, "file"));
    g_assert_false(explorer_row_is(NULL, "file"));
    g_object_unref(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/explorer_row/append_and_parents", test_append_and_parents);
    g_test_add_func("/explorer_row/remove", test_remove);
    g_test_add_func("/explorer_row/find", test_find);
    g_test_add_func("/explorer_row/changed_signal", test_changed_signal);
    return g_test_run();
}
