/* Unit tests for src/workspace/layout_store.c: saved layouts file and terminal pane trees */
#include "workspace/layout_store.h"
#include "workspace/tab_tree.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_term_tree(void) {
    TermLayoutNode* t = term_layout_parse("H:0.5(T,V:0.4(T,T))");
    g_assert_nonnull(t);
    g_assert_cmpuint(term_layout_count(t), ==, 3);
    g_assert_false(t->vertical);
    g_assert_true(t->second->vertical);
    gchar* s = term_layout_to_string(t);
    g_assert_cmpstr(s, ==, "H:0.500(T,V:0.400(T,T))");
    g_free(s);
    term_layout_free(t);

    t = term_layout_parse("T");
    g_assert_cmpuint(term_layout_count(t), ==, 1);
    term_layout_free(t);

    /* "T<n>": a pane with n terminal tabs */
    t = term_layout_parse("V:0.5(T3,T)");
    g_assert_nonnull(t);
    g_assert_cmpuint(t->first->tabs, ==, 3);
    g_assert_cmpuint(term_layout_count(t), ==, 4);
    s = term_layout_to_string(t);
    g_assert_cmpstr(s, ==, "V:0.500(T3,T)");
    g_free(s);
    term_layout_free(t);
    g_assert_null(term_layout_parse("T0"));
    g_assert_null(term_layout_parse("T99"));

    /* Fractions are clamped; invalid or empty text gives NULL */
    t = term_layout_parse("V:2(T,T)");
    g_assert_cmpfloat(t->frac, ==, 0.95);
    term_layout_free(t);
    g_assert_null(term_layout_parse(""));
    g_assert_null(term_layout_parse(NULL));
    g_assert_null(term_layout_parse("H:0.5(T)"));
    g_assert_null(term_layout_parse("H:0.5(T,T)x"));
    g_assert_null(term_layout_parse("X"));
    g_assert_cmpuint(term_layout_count(NULL), ==, 0);
}

static void test_save_load(void) {
    gchar* dir = g_dir_make_tmp("layouts-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "sub", "layouts.ini", NULL);

    GPtrArray* layouts = g_ptr_array_new_with_free_func(saved_layout_free);
    SavedLayout* a = saved_layout_new("Coding [main] = 1");
    a->term_vis = TRUE;
    a->stacked = TRUE;
    a->main_frac = 0.2;
    g_free(a->terminals);
    a->terminals = g_strdup("H:0.300(T,T)");
    layout_store_put(layouts, a);
    SavedLayout* b = saved_layout_new("Reading");
    b->files_vis = FALSE;
    b->editor_dedicated = TRUE;
    layout_store_put(layouts, b);
    g_assert_true(layout_store_save(path, layouts, NULL, NULL)); /* creates the directory */

    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "ExplorerWidth=0.200\n"));
    g_free(text);

    GPtrArray* loaded = layout_store_load(path);
    g_assert_cmpuint(loaded->len, ==, 2);
    SavedLayout* la = g_ptr_array_index(loaded, 0); /* file order kept */
    g_assert_cmpstr(la->name, ==, "Coding [main] = 1");
    g_assert_true(la->term_vis && la->stacked && la->files_vis);
    g_assert_cmpfloat_with_epsilon(la->main_frac, 0.2, 1e-9);
    g_assert_cmpstr(la->terminals, ==, "H:0.300(T,T)");
    SavedLayout* lb = layout_store_find(loaded, "Reading");
    g_assert_false(lb->files_vis);
    g_assert_true(lb->editor_dedicated);
    g_assert_cmpstr(lb->terminals, ==, "");

    /* Same name replaces in place; remove */
    SavedLayout* c = saved_layout_copy(lb);
    c->preview_vis = FALSE;
    layout_store_put(loaded, c);
    g_assert_cmpuint(loaded->len, ==, 2);
    g_assert_false(((SavedLayout*)g_ptr_array_index(loaded, 1))->preview_vis);
    g_assert_true(layout_store_remove(loaded, "Reading"));
    g_assert_false(layout_store_remove(loaded, "Reading"));
    g_assert_cmpuint(loaded->len, ==, 1);

    /* Missing file: empty list */
    GPtrArray* none = layout_store_load("/nonexistent/layouts.ini");
    g_assert_cmpuint(none->len, ==, 0);

    g_ptr_array_unref(none);
    g_ptr_array_unref(loaded);
    g_ptr_array_unref(layouts);
    g_remove(path);
    gchar* sub = g_path_get_dirname(path);
    g_rmdir(sub);
    g_rmdir(dir);
    g_free(sub);
    g_free(path);
    g_free(dir);
}

static void test_default(void) {
    gchar* dir = g_dir_make_tmp("layouts-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "layouts.ini", NULL);
    GPtrArray* named = g_ptr_array_new_with_free_func(saved_layout_free);
    layout_store_put(named, saved_layout_new("Work"));

    /* No file: built-in default; a built-in default is not written */
    SavedLayout* d = layout_store_load_default(path);
    g_assert_false(layout_store_is_customized(d));
    g_assert_true(layout_store_save(path, named, d, NULL));
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_null(strstr(text, "[Default]"));
    g_free(text);

    /* Customized default: saved apart, not listed among the named layouts */
    d->term_vis = TRUE;
    g_free(d->terminals);
    d->terminals = g_strdup("V:0.500(T,T)");
    g_assert_true(layout_store_is_customized(d));
    g_assert_true(layout_store_save(path, named, d, NULL));
    SavedLayout* back = layout_store_load_default(path);
    g_assert_true(back->term_vis);
    g_assert_cmpstr(back->terminals, ==, "V:0.500(T,T)");
    GPtrArray* loaded = layout_store_load(path);
    g_assert_cmpuint(loaded->len, ==, 1);
    g_assert_cmpstr(((SavedLayout*)g_ptr_array_index(loaded, 0))->name, ==, "Work");

    g_ptr_array_unref(loaded);
    saved_layout_free(back);
    saved_layout_free(d);
    g_ptr_array_unref(named);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_summary(void) {
    SavedLayout* l = saved_layout_new("x");
    gchar* s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview");
    g_free(s);
    l->term_vis = TRUE;
    g_free(l->terminals);
    l->terminals = g_strdup("V:0.500(T,T)");
    s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview · Terminal (2 terminals, beside)");
    g_free(s);
    l->editor_dedicated = TRUE;
    s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview · Editor Panel · Terminal (2 terminals, beside)");
    g_free(s);
    g_free(l->editor_splits);
    l->editor_splits = g_strdup("H:0.500([0],[t,t])");
    s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview · Editor Panel (2 panes, 2 terminals) · Terminal (2 terminals, beside)");
    g_free(s);
    saved_layout_free(l);
}

/* The splits of the editor panel are saved, read back and validated */
static void test_editor_splits(void) {
    GKeyFile* kf = g_key_file_new();
    SavedLayout* l = saved_layout_new("Split");
    l->editor_dedicated = TRUE;
    g_free(l->editor_splits);
    l->editor_splits = g_strdup("V:0.300([0,t],[0])");
    g_assert_true(layout_store_is_customized(l));
    saved_layout_write(kf, "G", l, TRUE);
    SavedLayout* back = saved_layout_read(kf, "G", "Split");
    g_assert_cmpstr(back->editor_splits, ==, "V:0.300([0,t],[0])");
    SavedLayout* copy = saved_layout_copy(back);
    g_assert_cmpstr(copy->editor_splits, ==, "V:0.300([0,t],[0])");
    saved_layout_free(copy);
    saved_layout_free(back);

    /* Invalid or missing: no splits (and no key written for none) */
    g_key_file_set_string(kf, "G", "EditorSplits", "H:([0]");
    back = saved_layout_read(kf, "G", "Split");
    g_assert_cmpstr(back->editor_splits, ==, "");
    saved_layout_free(back);
    g_free(l->editor_splits);
    l->editor_splits = g_strdup("");
    saved_layout_write(kf, "H", l, TRUE);
    g_assert_false(g_key_file_has_key(kf, "H", "EditorSplits", NULL));

    saved_layout_free(l);
    g_key_file_free(kf);
}

/* The tagbars are saved with the layout; older layouts keep the current ones */
static void test_tagbars(void) {
    GKeyFile* kf = g_key_file_new();
    SavedLayout* l = saved_layout_new("Tags");
    saved_layout_write(kf, "Old", l, TRUE);
    SavedLayout* back = saved_layout_read(kf, "Old", "Tags");
    g_assert_false(back->tagbars);
    g_assert_false(g_key_file_has_key(kf, "Old", "EditorTagbar", NULL));
    saved_layout_free(back);

    l->tagbars = TRUE;
    g_assert_false(layout_store_is_customized(l)); /* both hidden: as built in */
    l->editor_tagbar = TRUE;
    l->tagbar_width = 310;
    g_assert_true(layout_store_is_customized(l));
    gchar* s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview · Tagbar (editor)");
    g_free(s);
    saved_layout_write(kf, "New", l, TRUE);
    back = saved_layout_read(kf, "New", "Tags");
    g_assert_true(back->tagbars);
    g_assert_true(back->editor_tagbar);
    g_assert_false(back->preview_tagbar);
    g_assert_cmpint(back->tagbar_width, ==, 310);
    SavedLayout* copy = saved_layout_copy(back);
    g_assert_cmpint(copy->tagbar_width, ==, 310);
    saved_layout_free(copy);
    saved_layout_free(back);

    l->preview_tagbar = TRUE;
    s = saved_layout_summary(l);
    g_assert_cmpstr(s, ==, "Explorer · Preview · Tagbar");
    g_free(s);

    /* Default width: no key; a bad width falls back to the default */
    l->tagbar_width = 0;
    saved_layout_write(kf, "Wide", l, TRUE);
    g_assert_false(g_key_file_has_key(kf, "Wide", "TagbarWidth", NULL));
    g_key_file_set_integer(kf, "Wide", "TagbarWidth", -5);
    back = saved_layout_read(kf, "Wide", "Tags");
    g_assert_cmpint(back->tagbar_width, ==, 0);
    g_assert_true(back->preview_tagbar);
    saved_layout_free(back);

    saved_layout_free(l);
    g_key_file_free(kf);
}

static void test_grid(void) {
    static const guint panes[] = { 1, 1, 2, 3, 4, 4, 4 };
    for (guint files = 0; files < G_N_ELEMENTS(panes); files++) {
        gchar* text = layout_store_grid_splits(files, 0, 0);
        TabTreeNode* tree = tab_tree_parse(text);
        g_assert_nonnull(tree);
        g_assert_cmpuint(tab_tree_notebook_count(tree), ==, panes[files]);
        g_assert_cmpuint(tab_tree_terminal_count(tree), ==, 0);
        tab_tree_free(tree);
        g_free(text);
    }

    /* Auto keeps the panels and puts the files in the editor panel */
    SavedLayout* current = saved_layout_new("Current");
    current->term_vis = TRUE;
    g_free(current->terminals);
    current->terminals = g_strdup("H:0.500(T,T)");
    SavedLayout* l = layout_store_auto(current, 3, 0, 0);
    g_assert_cmpstr(l->name, ==, "Auto");
    g_assert_true(l->editor_dedicated && l->term_vis && l->files_vis == current->files_vis);
    g_assert_cmpstr(l->terminals, ==, "H:0.500(T,T)");
    g_assert_cmpstr(l->editor_splits, ==, "H:0.500([0],V:0.500([0],[0]))");
    saved_layout_free(l);
    saved_layout_free(current);
}

static void expect_grid(guint files, gint w, gint h, const gchar* want) {
    gchar* text = layout_store_grid_splits(files, w, h);
    g_assert_cmpstr(text, ==, want);
    TabTreeNode* tree = tab_tree_parse(text);
    g_assert_nonnull(tree);
    tab_tree_free(tree);
    g_free(text);
}

static void test_grid_size(void) {
    /* wide: side by side; tall: one above the other */
    expect_grid(2, 1200, 800, "H:0.500([0],[0])");
    expect_grid(2, 800, 1000, "V:0.500([0],[0])");
    /* too narrow for two side by side, too short for two stacked */
    expect_grid(2, 1000, 200, "H:0.500([0],[0])");
    expect_grid(2, 400, 1000, "V:0.500([0],[0])");
    expect_grid(3, 400, 200, "[0]");
    /* three */
    expect_grid(3, 1200, 800, "H:0.500([0],V:0.500([0],[0]))");
    expect_grid(3, 1000, 1200, "V:0.500([0],H:0.500([0],[0]))");
    expect_grid(3, 1600, 300, "H:0.333([0],H:0.500([0],[0]))");
    expect_grid(3, 400, 900, "V:0.333([0],V:0.500([0],[0]))");
    expect_grid(3, 1000, 200, "H:0.500([0],[0])");
    /* four and more */
    expect_grid(6, 1200, 800, "H:0.500(V:0.500([0],[0]),V:0.500([0],[0]))");
    expect_grid(4, 2000, 300, "H:0.250([0],H:0.333([0],H:0.500([0],[0])))");
    expect_grid(5, 400, 1000, "V:0.250([0],V:0.333([0],V:0.500([0],[0])))");

    /* the editor panel's share of the window */
    SavedLayout* l = saved_layout_new("Area");
    l->files_vis = TRUE;
    l->main_frac = 0.25;
    l->preview_vis = TRUE;
    l->editor_frac = 0.50;
    l->term_vis = TRUE;
    l->stacked = TRUE;
    l->right_frac = 0.60;
    gint w = 0;
    gint h = 0;
    layout_store_editor_area(l, 1600, 1000, &w, &h);
    g_assert_cmpint(w, ==, 600);
    g_assert_cmpint(h, ==, 600);
    l->stacked = FALSE;
    layout_store_editor_area(l, 1600, 1000, &w, &h);
    g_assert_cmpint(w, ==, 360);
    g_assert_cmpint(h, ==, 1000);

    /* a narrow editor panel stacks its two files */
    l->editor_dedicated = TRUE;
    SavedLayout* a = layout_store_auto(l, 2, 1600, 1000);
    g_assert_cmpstr(a->editor_splits, ==, "V:0.500([0],[0])");
    saved_layout_free(a);
    l->term_vis = FALSE;
    a = layout_store_auto(l, 2, 3000, 1000);
    g_assert_cmpstr(a->editor_splits, ==, "H:0.500([0],[0])");
    saved_layout_free(a);
    saved_layout_free(l);
}

static void test_presets(void) {
    GPtrArray* presets = layout_store_presets(NULL);
    g_assert_cmpuint(presets->len, ==, 3);
    const SavedLayout* code = layout_store_find(presets, "Code");
    g_assert_nonnull(code);
    g_assert_true(code->editor_dedicated && code->term_vis && code->stacked);
    const SavedLayout* term = layout_store_find(presets, "Terminal");
    g_assert_nonnull(term);
    TermLayoutNode* tree = term_layout_parse(term->terminals);
    g_assert_cmpuint(term_layout_count(tree), ==, 2);
    term_layout_free(tree);
    const SavedLayout* focus = layout_store_find(presets, "Focus");
    g_assert_nonnull(focus);
    g_assert_true(focus->preview_vis && !focus->files_vis && !focus->term_vis);
    for (guint i = 0; i < presets->len; i++) {
        const SavedLayout* l = g_ptr_array_index(presets, i);
        g_assert_true(layout_store_is_customized(l));
        g_assert_false(l->tagbars); /* presets keep the current tagbars */
    }
    g_ptr_array_unref(presets);

    /* A saved layout of the same name replaces the preset */
    GPtrArray* saved = g_ptr_array_new_with_free_func(saved_layout_free);
    layout_store_put(saved, saved_layout_new("Code"));
    presets = layout_store_presets(saved);
    g_assert_cmpuint(presets->len, ==, 2);
    g_assert_null(layout_store_find(presets, "Code"));
    g_ptr_array_unref(presets);

    /* Presets are never written to the file */
    gchar* dir = g_dir_make_tmp("layouts-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "layouts.ini", NULL);
    GPtrArray* empty = g_ptr_array_new_with_free_func(saved_layout_free);
    g_assert_true(layout_store_save(path, empty, NULL, NULL));
    GPtrArray* loaded = layout_store_load(path);
    g_assert_cmpuint(loaded->len, ==, 0);
    g_ptr_array_unref(loaded);
    g_ptr_array_unref(empty);
    g_ptr_array_unref(saved);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/layout_store/term_tree", test_term_tree);
    g_test_add_func("/layout_store/save_load", test_save_load);
    g_test_add_func("/layout_store/default", test_default);
    g_test_add_func("/layout_store/summary", test_summary);
    g_test_add_func("/layout_store/editor_splits", test_editor_splits);
    g_test_add_func("/layout_store/tagbars", test_tagbars);
    g_test_add_func("/layout_store/presets", test_presets);
    g_test_add_func("/layout_store/grid", test_grid);
    g_test_add_func("/layout_store/grid_size", test_grid_size);
    return g_test_run();
}
