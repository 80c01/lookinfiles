/* Unit tests for src/workspace/session_store.c: session and recent places. */
#include "workspace/session_store.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_session_round_trip(void) {
    gchar* dir = g_dir_make_tmp("session_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "session.ini", NULL);

    g_assert_null(session_load(path)); /* no file yet */

    Session* s = session_new();
    const gchar* expanded[] = { "/w/src", "/w/odd;name", NULL };
    const gchar* tabs[] = { "/w/src/a.c", "/w/README.md", NULL };
    session_add_workspace(s, "/w", expanded, "/w/src/a.c", TRUE, 2, tabs)->view = 2;
    session_add_workspace(s, "/home/u", NULL, NULL, FALSE, 0, NULL);
    s->current = 1;
    GError* err = NULL;
    g_assert_true(session_save(s, path, &err));
    g_assert_no_error(err);
    session_free(s);

    s = session_load(path);
    g_assert_nonnull(s);
    g_assert_cmpuint(s->workspaces->len, ==, 2);
    g_assert_cmpint(s->current, ==, 1);
    SessionWorkspace* w = g_ptr_array_index(s->workspaces, 0);
    g_assert_cmpstr(w->dir, ==, "/w");
    g_assert_cmpuint(g_strv_length(w->expanded), ==, 2);
    g_assert_cmpstr(w->expanded[1], ==, "/w/odd;name"); /* separators survive */
    g_assert_cmpstr(w->selected, ==, "/w/src/a.c");
    g_assert_true(w->show_hidden);
    g_assert_cmpint(w->sort_mode, ==, 2);
    g_assert_cmpint(w->view, ==, 2);
    g_assert_cmpuint(g_strv_length(w->tabs), ==, 2);
    g_assert_cmpstr(w->tabs[0], ==, "/w/src/a.c");
    w = g_ptr_array_index(s->workspaces, 1);
    g_assert_cmpint(w->view, ==, 0); /* the files */
    g_assert_null(w->selected);
    g_assert_cmpuint(g_strv_length(w->expanded), ==, 0);
    g_assert_cmpuint(g_strv_length(w->tabs), ==, 0);
    session_free(s);

    /* The current index stays in range */
    g_assert_true(g_file_set_contents(path, "[Session]\nworkspaces=1\ncurrent=9\n[Workspace 1]\ndir=/x\n", -1, NULL));
    s = session_load(path);
    g_assert_nonnull(s);
    g_assert_cmpint(s->current, ==, 0);
    session_free(s);

    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_session_layout(void) {
    gchar* dir = g_dir_make_tmp("session_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "session.ini", NULL);

    Session* s = session_new();
    const gchar* tabs[] = { "/w/a.c", "/w/b.c", NULL };
    SessionWorkspace* w = session_add_workspace(s, "/w", NULL, NULL, FALSE, 0, tabs);
    w->preview_tabs = g_strdup("[1*]");
    w->editor_tabs = g_strdup("H:0.400([0*!],[0])");
    w->panels = saved_layout_new("Session");
    w->panels->files_vis = FALSE;
    w->panels->term_vis = TRUE;
    w->panels->main_frac = 0.3;
    g_free(w->panels->terminals);
    w->panels->terminals = g_strdup("H:0.500(T,T2)");
    session_add_workspace(s, "/old", NULL, NULL, FALSE, 0, tabs); /* nothing more saved */
    g_assert_true(session_save(s, path, NULL));
    session_free(s);

    s = session_load(path);
    g_assert_nonnull(s);
    w = g_ptr_array_index(s->workspaces, 0);
    g_assert_cmpstr(w->preview_tabs, ==, "[1*]");
    g_assert_cmpstr(w->editor_tabs, ==, "H:0.400([0*!],[0])");
    g_assert_nonnull(w->panels);
    g_assert_false(w->panels->files_vis);
    g_assert_true(w->panels->preview_vis);
    g_assert_true(w->panels->term_vis);
    g_assert_cmpfloat_with_epsilon(w->panels->main_frac, 0.3, 1e-9);
    g_assert_cmpstr(w->panels->terminals, ==, "H:0.500(T,T2)");
    w = g_ptr_array_index(s->workspaces, 1);
    g_assert_null(w->preview_tabs);
    g_assert_null(w->editor_tabs);
    g_assert_null(w->panels);
    g_assert_cmpuint(g_strv_length(w->tabs), ==, 2);
    session_free(s);

    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_snapshot_target(void) {
    gchar* t = session_target_make("0123abcd");
    g_assert_cmpstr(t, ==, "lookinfiles-session://0123abcd");
    g_assert_cmpstr(session_target_id(t), ==, "0123abcd");
    g_free(t);
    g_assert_null(session_target_make("../etc"));
    g_assert_null(session_target_make("ABCD"));
    g_assert_null(session_target_make(""));
    g_assert_null(session_target_id(NULL));
    g_assert_null(session_target_id("https://example.org"));
    g_assert_null(session_target_id("lookinfiles-session://"));
    g_assert_null(session_target_id("lookinfiles-session://../../x"));
    g_assert_null(session_target_id("lookinfiles-session://ab/cd"));

    gchar* id = session_snapshot_new_id();
    gchar* made = session_target_make(id); /* a generated id is always valid */
    g_assert_nonnull(made);
    g_assert_cmpstr(session_target_id(made), ==, id);
    g_free(made);
    gchar* path = session_snapshot_path(id);
    g_assert_nonnull(path);
    g_assert_true(g_str_has_suffix(path, ".ini"));
    g_assert_nonnull(strstr(path, "/sessions/"));
    g_assert_null(session_snapshot_path("a/b"));
    session_snapshot_remove("a/b"); /* ignored */
    session_snapshot_remove(id);    /* missing file is fine */
    g_free(path);
    g_free(id);
}

static void test_recent(void) {
    RecentList* r = recent_list_new();
    recent_list_add(r->folders, "/a");
    recent_list_add(r->folders, "/b");
    recent_list_add(r->folders, "/a"); /* moves first, no duplicate */
    g_assert_cmpuint(r->folders->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(r->folders, 0), ==, "/a");
    g_assert_cmpstr(g_ptr_array_index(r->folders, 1), ==, "/b");
    for (gint i = 0; i < RECENT_MAX + 5; i++) {
        gchar* p = g_strdup_printf("/f%d", i);
        recent_list_add(r->files, p);
        g_free(p);
    }
    g_assert_cmpuint(r->files->len, ==, RECENT_MAX);

    gchar* dir = g_dir_make_tmp("recent_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "recent.ini", NULL);
    g_assert_true(recent_list_save(r, path, NULL));
    recent_list_free(r);
    r = recent_list_load(path);
    g_assert_cmpuint(r->folders->len, ==, 2);
    g_assert_cmpuint(r->files->len, ==, RECENT_MAX);
    gchar* newest = g_strdup_printf("/f%d", RECENT_MAX + 4);
    g_assert_cmpstr(g_ptr_array_index(r->files, 0), ==, newest);
    g_free(newest);
    recent_list_free(r);

    r = recent_list_load("/nonexistent/recent.ini");
    g_assert_cmpuint(r->folders->len, ==, 0);
    recent_list_free(r);
    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/session_store/session_round_trip", test_session_round_trip);
    g_test_add_func("/session_store/session_layout", test_session_layout);
    g_test_add_func("/session_store/snapshot_target", test_snapshot_target);
    g_test_add_func("/session_store/recent", test_recent);
    return g_test_run();
}
