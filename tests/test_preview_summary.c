/* Unit tests for src/preview_summary.c: summaries of remote machines, notebooks,
 * bookmark folders and the notes trash, and their column alignment. */
#include "preview_summary.h"

#include <string.h>

static GtkTreeStore* new_store(void) {
    return gtk_tree_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                              G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);
}

static void add_row(GtkTreeStore* store, GtkTreeIter* out, GtkTreeIter* parent,
                    const gchar* type, const gchar* name, const gchar* path, const gchar* badge) {
    gtk_tree_store_append(store, out, parent);
    gtk_tree_store_set(store, out, COL_TYPE, type, COL_NAME, name, COL_LABEL, name,
                       COL_PATH, path, COL_GIT_STATUS, badge, -1);
}

/* Character column where needle starts on the line containing it (-1 if absent) */
static glong column_of(const gchar* text, const gchar* line_part, const gchar* needle) {
    const gchar* line = strstr(text, line_part);
    if (line == NULL) {
        return -1;
    }
    while (line > text && line[-1] != '\n') {
        line--;
    }
    const gchar* hit = strstr(line, needle);
    const gchar* eol = strchr(line, '\n');
    if (hit == NULL || (eol != NULL && hit > eol)) {
        return -1;
    }
    return g_utf8_pointer_to_offset(line, hit);
}

static void test_remote(void) {
    RemoteMachine rm = {0};
    rm.name = "web";
    rm.host = "web.example.com";
    rm.user = "";
    rm.port = 2222;
    rm.remote_path = "/srv";
    rm.identity_file = "";
    rm.from_ssh_config = TRUE;
    gchar* s = preview_summary_remote(&rm);
    g_assert_nonnull(strstr(s, "Remote Machine: web\n"));
    g_assert_nonnull(strstr(s, "(default)"));
    g_assert_nonnull(strstr(s, "Auto-discovered from ~/.ssh/config"));
    /* Values aligned: Host, Port and Remote Path values start in the same column */
    glong col = column_of(s, "Host:", "web.example.com");
    g_assert_cmpint(col, >, 0);
    g_assert_cmpint(column_of(s, "Port:", "2222"), ==, col);
    g_assert_cmpint(column_of(s, "Remote Path:", "/srv"), ==, col);
    /* Shortcut descriptions aligned too */
    g_assert_cmpint(column_of(s, "• t ", "Open SSH"), ==, column_of(s, "• F5", "Re-scan"));
    g_free(s);
    g_assert_null(preview_summary_remote(NULL));
}

static void test_notebook(void) {
    GtkTreeStore* store = new_store();
    GtkTreeIter nb, n;
    add_row(store, &nb, NULL, "notebook", "General", "/notes/General", "2");
    add_row(store, &n, &nb, "note", "short", "/notes/General/short.txt", "");
    add_row(store, &n, &nb, "note", "a much longer note title", "/notes/General/x.md",
            "<span foreground=\"#4ec9b0\" size=\"small\">rich</span>");
    gchar* s = preview_summary_container(GTK_TREE_MODEL(store), &nb);
    g_assert_nonnull(strstr(s, "Notebook: General\n"));
    g_assert_nonnull(strstr(s, "/notes/General"));
    g_assert_nonnull(strstr(s, "Plain Text"));
    g_assert_nonnull(strstr(s, "Rich Format"));
    g_assert_cmpint(column_of(s, "• short", "Plain Text"), ==,
                    column_of(s, "• a much longer", "Rich Format"));
    g_free(s);

    /* Other row types have no summary */
    g_assert_null(preview_summary_container(GTK_TREE_MODEL(store), &n));
    g_object_unref(store);
}

static void test_bookmark_folder_and_trash(void) {
    GtkTreeStore* store = new_store();
    GtkTreeIter folder, b, sub, trash, t, empty;
    add_row(store, &folder, NULL, "bm_folder", "Work", "", "2");
    add_row(store, &b, &folder, "bookmark", "Sources", "/home/me/src", "");
    add_row(store, &sub, &folder, "bm_folder", "Docs", "", "1");
    add_row(store, &b, &sub, "bookmark", "Manual", "/home/me/doc", "");
    gchar* s = preview_summary_container(GTK_TREE_MODEL(store), &folder);
    g_assert_nonnull(strstr(s, "Bookmark Folder: Work\n"));
    g_assert_nonnull(strstr(s, "/home/me/src"));
    g_assert_nonnull(strstr(s, "Docs/"));
    g_assert_nonnull(strstr(s, "folder, 1 item\n"));
    g_assert_null(strstr(s, "Manual")); /* only direct children */
    g_free(s);

    add_row(store, &empty, NULL, "bm_folder", "Empty", "", "");
    s = preview_summary_container(GTK_TREE_MODEL(store), &empty);
    g_assert_nonnull(strstr(s, "(empty folder)"));
    g_free(s);

    add_row(store, &trash, NULL, "trash", "Trash", "/notes/.trash", "");
    add_row(store, &t, &trash, "trash_note", "old idea", "/notes/.trash/old.txt",
            "<span foreground=\"#6e7681\" size=\"small\">from R&amp;D</span>");
    s = preview_summary_container(GTK_TREE_MODEL(store), &trash);
    g_assert_nonnull(strstr(s, "Trash\n"));
    g_assert_nonnull(strstr(s, "old idea   from R&D")); /* markup decoded */
    g_free(s);
    g_object_unref(store);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_summary/remote", test_remote);
    g_test_add_func("/preview_summary/notebook", test_notebook);
    g_test_add_func("/preview_summary/bookmark_folder_and_trash", test_bookmark_folder_and_trash);
    return g_test_run();
}
