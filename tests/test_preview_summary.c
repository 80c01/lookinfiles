/* Unit tests for src/preview_summary.c: summaries of remote machines, notebooks,
 * bookmark folders and the notes trash, and their column alignment. */
#include "preview_summary.h"
#include "canvas/canvas_doc.h"

#include <glib/gstdio.h>
#include <string.h>

/* Append a row with a badge; containers can have children */
static ExplorerRow* add_row(ExplorerRow* parent, const gchar* type, const gchar* name,
                            const gchar* path, const gchar* badge) {
    gboolean container = g_strcmp0(type, "notebook") == 0 || g_strcmp0(type, "bm_folder") == 0 ||
                         g_strcmp0(type, "trash") == 0;
    ExplorerRow* row = container ? explorer_row_append_container(parent, type, name, name, path)
                                 : explorer_row_append(parent, type, name, name, path);
    explorer_row_set_badge(row, badge);
    return row;
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
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* nb = add_row(root, "notebook", "General", "/notes/General", "2");
    ExplorerRow* n = add_row(nb, "note", "short", "/notes/General/short.txt", "");
    add_row(nb, "note", "a much longer note title", "/notes/General/x.md",
            "<span foreground=\"#4ec9b0\" size=\"small\">md</span>");
    gchar* s = preview_summary_container(nb);
    g_assert_nonnull(strstr(s, "Notebook: General\n"));
    g_assert_nonnull(strstr(s, "/notes/General"));
    g_assert_nonnull(strstr(s, "Plain Text"));
    g_assert_nonnull(strstr(s, "Markdown"));
    g_assert_cmpint(column_of(s, "• short", "Plain Text"), ==,
                    column_of(s, "• a much longer", "Markdown"));
    g_free(s);

    /* Other row types have no summary */
    g_assert_null(preview_summary_container(n));
    g_assert_null(preview_summary_container(NULL));
    g_object_unref(root);
}

static void test_canvas(void) {
    g_autofree gchar* tmp = g_dir_make_tmp("summary-XXXXXX", NULL);
    g_autofree gchar* dir = g_build_filename(tmp, "Board.canvas", NULL);
    g_assert_true(canvas_doc_create(dir, "Board", NULL));
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* nb = add_row(root, "notebook", "General", tmp, "");
    ExplorerRow* note = add_row(nb, "note", "Board", dir, "canvas");
    gchar* s = preview_summary_container(note);
    g_assert_nonnull(s);
    g_assert_nonnull(strstr(s, "Canvas: Board\n"));
    g_assert_nonnull(strstr(s, "Text boxes:"));
    g_assert_nonnull(strstr(s, "# Board\n"));
    g_free(s);
    CanvasDoc* doc = canvas_doc_load(dir, NULL);
    canvas_doc_add_image(doc, 10, 10, 100, 100, "media/0001.png");
    canvas_doc_save(doc, dir, NULL);
    canvas_doc_free(doc);
    s = preview_summary_container(note);
    g_assert_nonnull(strstr(s, "Pictures:"));
    g_assert_nonnull(strstr(s, "![](media/0001.png)"));
    g_free(s);
    /* Its notebook lists it as a Canvas */
    s = preview_summary_container(nb);
    g_assert_nonnull(strstr(s, "Canvas"));
    g_free(s);
    /* Not a Canvas directory */
    g_assert_null(preview_summary_canvas("x", tmp));
    g_object_unref(root);
    g_autofree gchar* page = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    g_remove(page);
    g_rmdir(dir);
    g_rmdir(tmp);
}

static void test_bookmark_folder_and_trash(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* folder = add_row(root, "bm_folder", "Work", "", "2");
    add_row(folder, "bookmark", "Sources", "/home/me/src", "");
    ExplorerRow* sub = add_row(folder, "bm_folder", "Docs", "", "1");
    add_row(sub, "bookmark", "Manual", "/home/me/doc", "");
    gchar* s = preview_summary_container(folder);
    g_assert_nonnull(strstr(s, "Bookmark Folder: Work\n"));
    g_assert_nonnull(strstr(s, "/home/me/src"));
    g_assert_nonnull(strstr(s, "Docs/"));
    g_assert_nonnull(strstr(s, "folder, 1 item\n"));
    g_assert_null(strstr(s, "Manual")); /* only direct children */
    g_free(s);

    /* Placeholder rows are not items */
    ExplorerRow* empty = add_row(root, "bm_folder", "Empty", "", "");
    add_row(empty, "-", "", "/", "");
    ExplorerRow* outer = add_row(root, "bm_folder", "Outer", "", "");
    ExplorerRow* inner = add_row(outer, "bm_folder", "Inner", "", "");
    add_row(inner, "-", "", "", "");
    s = preview_summary_container(empty);
    g_assert_nonnull(strstr(s, "\n(empty)\n"));
    g_free(s);
    s = preview_summary_container(outer);
    g_assert_nonnull(strstr(s, "folder, 0 items\n"));
    g_free(s);

    ExplorerRow* trash = add_row(root, "trash", "Trash", "/notes/.trash", "");
    add_row(trash, "trash_note", "old idea", "/notes/.trash/old.txt",
            "<span foreground=\"#6e7681\" size=\"small\">from R&amp;D</span>");
    s = preview_summary_container(trash);
    g_assert_nonnull(strstr(s, "Trash\n"));
    g_assert_nonnull(strstr(s, "old idea   from R&D")); /* markup decoded */
    g_free(s);
    g_object_unref(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_summary/remote", test_remote);
    g_test_add_func("/preview_summary/notebook", test_notebook);
    g_test_add_func("/preview_summary/bookmark_folder_and_trash", test_bookmark_folder_and_trash);
    g_test_add_func("/preview_summary/canvas", test_canvas);
    return g_test_run();
}
