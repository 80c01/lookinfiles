/* Unit tests for src/layout_store.c: saved layouts file and terminal pane trees */
#include "layout_store.h"

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
    g_assert_cmpstr(s, ==, "Explorer · Preview · Terminal (2 panes, beside)");
    g_free(s);
    saved_layout_free(l);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/layout_store/term_tree", test_term_tree);
    g_test_add_func("/layout_store/save_load", test_save_load);
    g_test_add_func("/layout_store/default", test_default);
    g_test_add_func("/layout_store/summary", test_summary);
    return g_test_run();
}
