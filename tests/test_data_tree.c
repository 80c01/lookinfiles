/* Unit tests for src/formats/data_tree.c (data documents as a tree). */
#include "formats/data_tree.h"

#include <string.h>

/* server: { host, port, tls: { on } }, tags: [ "a" ], empty: {} */
static DataTree* tree_new(void) {
    DataTree* tree = data_tree_new();
    DataTreeNode* root = data_tree_root(tree);
    DataTreeNode* server = data_tree_add(root, DATA_TREE_OBJECT, "server", "$.server");
    data_tree_set_value(data_tree_add(server, DATA_TREE_VALUE, "host", "$.server.host"), "\"x\"", -1,
                        PREVIEW_STYLE_STRING);
    data_tree_set_value(data_tree_add(server, DATA_TREE_VALUE, "port", "$.server.port"), "80", -1,
                        PREVIEW_STYLE_NUMBER);
    DataTreeNode* tls = data_tree_add(server, DATA_TREE_OBJECT, "tls", "$.server.tls");
    data_tree_set_value(data_tree_add(tls, DATA_TREE_VALUE, "on", "$.server.tls.on"), "true", -1,
                        PREVIEW_STYLE_KEYWORD);
    DataTreeNode* tags = data_tree_add(root, DATA_TREE_ARRAY, "tags", "$.tags");
    data_tree_set_value(data_tree_add(tags, DATA_TREE_VALUE, "0", "$.tags[0]"), "\"a\"", -1, PREVIEW_STYLE_STRING);
    data_tree_add(root, DATA_TREE_OBJECT, "empty", "$.empty");
    return tree;
}

static void test_print(void) {
    g_autoptr(DataTree) tree = tree_new();
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_cmpint(data_tree_level(tree, NULL), ==, PREVIEW_FOLD_OPEN);
    g_assert_false(data_tree_print(tree, doc, NULL, PREVIEW_FOLD_OPEN));
    g_assert_cmpstr(doc->text->str, ==,
                    "▾ server  3 keys\n"
                    "    host  \"x\"\n"
                    "    port  80\n"
                    "  ▾ tls   1 key\n"
                    "      on  true\n"
                    "▾ tags    1 item\n"
                    "    0     \"a\"\n"
                    "  empty   {}\n");
    g_assert_cmpuint(doc->folds->len, ==, 3); /* the empty object does not fold */
    const PreviewFoldPoint* server = preview_doc_fold_find(doc, "$.server");
    g_assert_cmpint(server->start, ==, 0);
    const gchar* tags = strstr(doc->text->str, "\n▾ tags");
    g_assert_cmpint(server->end, ==, (gint)g_utf8_strlen(doc->text->str, tags - doc->text->str));
    g_assert_false(server->folded);
    g_assert_cmpstr(preview_doc_fold_at(doc, 60)->id, ==, "$.server.tls");
    g_assert_cmpstr(preview_doc_path_at(doc, 20), ==, "$.server.host");
    guint headings = 0;
    for (guint i = 0; i < doc->spans->len; i++) {
        headings += g_array_index(doc->spans, PreviewSpan, i).style == PREVIEW_STYLE_OUTLINE1;
    }
    g_assert_cmpuint(headings, ==, 3);
}

static void test_folded(void) {
    g_autoptr(DataTree) tree = tree_new();
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    data_tree_print(tree, doc, NULL, 1);
    g_assert_cmpstr(doc->text->str, ==,
                    "▸ server  3 keys\n"
                    "▸ tags    1 item\n"
                    "  empty   {}\n");
    g_assert_true(preview_doc_fold_find(doc, "$.server")->folded);
    g_assert_null(preview_doc_fold_find(doc, "$.server.tls"));

    /* Opened by hand against the level */
    g_autoptr(PreviewFold) fold = preview_fold_new(1);
    preview_fold_toggle(fold, "$.tags");
    g_autoptr(PreviewDoc) open = preview_doc_new();
    data_tree_print(tree, open, fold, data_tree_level(tree, fold));
    g_assert_nonnull(strstr(open->text->str, "▾ tags    1 item\n    0     \"a\"\n"));
}

static void test_values(void) {
    g_autoptr(DataTree) tree = data_tree_new();
    DataTreeNode* root = data_tree_root(tree);
    data_tree_set_value(data_tree_add(root, DATA_TREE_VALUE, NULL, "$"), "a\nb\tc", -1, PREVIEW_STYLE_STRING);
    DataTreeNode* list = data_tree_add(root, DATA_TREE_ARRAY, "list", "$.list");
    data_tree_add(list, DATA_TREE_VALUE, "0", "$.list[0]");
    data_tree_set_cut(list);
    data_tree_set_cut(root);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    data_tree_print(tree, doc, NULL, PREVIEW_FOLD_OPEN);
    g_assert_cmpstr(doc->text->str, ==, "a↵b c\n▾ list  1 item …\n    0   \n…\n");
}

static void test_long(void) {
    g_autoptr(DataTree) tree = data_tree_new();
    DataTreeNode* root = data_tree_root(tree);
    for (guint i = 0; i < DATA_TREE_MAX_LINES; i++) {
        gchar* key = g_strdup_printf("%u", i);
        DataTreeNode* item = data_tree_add(root, DATA_TREE_ARRAY, key, key);
        data_tree_add(item, DATA_TREE_VALUE, "0", NULL);
        g_free(key);
    }
    g_assert_cmpint(data_tree_level(tree, NULL), ==, 1);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(data_tree_print(tree, doc, NULL, PREVIEW_FOLD_OPEN));
    g_assert_nonnull(strstr(doc->text->str, "stopped after"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/data_tree/print", test_print);
    g_test_add_func("/data_tree/folded", test_folded);
    g_test_add_func("/data_tree/values", test_values);
    g_test_add_func("/data_tree/long", test_long);
    return g_test_run();
}
