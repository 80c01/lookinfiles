/* Unit tests for src/preview/preview_summary.c: summaries of remote machines, notebooks,
 * bookmark folders and the notes trash, and their column alignment. */
#include "preview/preview_summary.h"
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

static void test_local(void) {
    gchar* s = preview_summary_local("box", "ann", "/home/ann", "Debian GNU/Linux 13", TRUE);
    g_assert_nonnull(strstr(s, "Local Machine: box\n"));
    g_assert_nonnull(strstr(s, "Browsed in this workspace"));
    g_assert_nonnull(strstr(s, "Debian GNU/Linux 13"));
    glong col = column_of(s, "User:", "ann");
    g_assert_cmpint(col, >, 0);
    g_assert_cmpint(column_of(s, "Home Folder:", "/home/ann"), ==, col);
    g_free(s);
    /* No system line when unknown; another machine browsed */
    s = preview_summary_local("", NULL, NULL, NULL, FALSE);
    g_assert_nonnull(strstr(s, "Local Machine: Unknown\n"));
    g_assert_nonnull(strstr(s, "a remote machine is browsed here"));
    g_assert_null(strstr(s, "System:"));
    g_free(s);
}

static void test_disk(void) {
    DiskVolume v = {0};
    v.name = "USB KEY";
    v.path = "/media/ann/USB KEY";
    v.device = "/dev/sdb1";
    v.kind = DISK_VOLUME_REMOVABLE;
    v.can_eject = TRUE;
    gchar* s = preview_summary_disk(&v);
    g_assert_nonnull(strstr(s, "Disk: USB KEY\n"));
    g_assert_nonnull(strstr(s, "Removable disk"));
    g_assert_cmpint(column_of(s, "Device:", "/dev/sdb1"), ==, column_of(s, "Kind:", "Removable"));
    g_free(s);
    v.path = NULL;
    v.device = NULL;
    v.can_eject = FALSE;
    s = preview_summary_disk(&v);
    g_assert_nonnull(strstr(s, "Not mounted"));
    g_assert_null(strstr(s, "Mount Folder:"));
    g_assert_null(strstr(s, "Device:"));
    g_free(s);
    g_assert_null(preview_summary_disk(NULL));
}

static void test_remote_disk(void) {
    RemoteDisk d = {0};
    d.target = "/mnt/data";
    d.name = "data";
    d.source = "/dev/sdb1";
    d.fstype = "xfs";
    d.size = 2000000000;
    d.avail = 1000000000;
    d.kind = DISK_VOLUME_FIXED;
    gchar* s = preview_summary_remote_disk("box", &d);
    g_assert_nonnull(strstr(s, "Disk: data\n"));
    g_assert_nonnull(strstr(s, "box"));
    g_assert_nonnull(strstr(s, "xfs"));
    g_assert_cmpint(column_of(s, "Mount Folder:", "/mnt/data"), ==, column_of(s, "Device:", "/dev/sdb1"));
    g_assert_nonnull(strstr(s, "Free:"));
    g_free(s);
    d.size = 0;
    s = preview_summary_remote_disk(NULL, &d);
    g_assert_null(strstr(s, "Free:"));
    g_assert_null(strstr(s, "Machine:"));
    g_free(s);
    g_assert_null(preview_summary_remote_disk("box", NULL));
}

static void test_wsl(void) {
    WslDistro d = {0};
    d.name = "Ubuntu";
    d.version = 2;
    d.is_default = TRUE;
    d.running = TRUE;
    gchar* s = preview_summary_wsl(&d);
    g_assert_nonnull(strstr(s, "WSL Distribution: Ubuntu\n"));
    g_assert_nonnull(strstr(s, "Running"));
    g_assert_nonnull(strstr(s, "WSL 2"));
    g_assert_nonnull(strstr(s, "\\\\wsl.localhost\\Ubuntu\\"));
    g_assert_cmpint(column_of(s, "Status:", "Running"), ==, column_of(s, "Version:", "WSL 2"));
    g_free(s);
    d.version = 0;
    d.running = FALSE;
    s = preview_summary_wsl(&d);
    g_assert_null(strstr(s, "Version:"));
    g_assert_nonnull(strstr(s, "Stopped"));
    g_free(s);
    g_assert_null(preview_summary_wsl(NULL));
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

/* The typed lines summary_view draws: icons come from the rows */
static void test_lines(void) {
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* nb = add_row(root, "notebook", "General", "/notes/General", "");
    explorer_row_set_glyph(nb, "notebook");
    ExplorerRow* n = add_row(nb, "note", "plan", "/notes/General/plan.md", "md");
    explorer_row_set_icon_name(n, "plan.md");
    GPtrArray* lines = preview_summary_container_lines(nb);
    const PreviewSummaryLine* title = g_ptr_array_index(lines, 0);
    g_assert_cmpint(title->kind, ==, PREVIEW_SUMMARY_TITLE);
    g_assert_cmpstr(title->glyph, ==, "notebook");
    const PreviewSummaryLine* field = g_ptr_array_index(lines, 1);
    g_assert_cmpint(field->kind, ==, PREVIEW_SUMMARY_FIELD);
    g_assert_cmpstr(field->text, ==, "Notes");
    g_assert_cmpstr(field->detail, ==, "1");
    gboolean seen_item = FALSE;
    for (guint i = 0; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        if (line->kind == PREVIEW_SUMMARY_ITEM) {
            g_assert_cmpstr(line->text, ==, "plan");
            g_assert_cmpstr(line->detail, ==, "Markdown");
            g_assert_null(line->glyph);
            g_assert_cmpstr(line->file_icon, ==, "plan.md");
            g_assert_cmpint(preview_summary_item_width(lines, i), ==, 4);
            seen_item = TRUE;
        }
    }
    g_assert_true(seen_item);
    g_ptr_array_unref(lines);

    /* An empty notebook has no Notes heading, only a note */
    ExplorerRow* empty = add_row(root, "notebook", "Empty", "/notes/Empty", "");
    lines = preview_summary_container_lines(empty);
    for (guint i = 0; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        g_assert_false(line->kind == PREVIEW_SUMMARY_HEADING && g_strcmp0(line->text, "Notes") == 0);
    }
    g_ptr_array_unref(lines);
    g_assert_null(preview_summary_container_lines(n));
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

static void test_saved_search(void) {
    SavedDeepSearch s;
    saved_deep_search_init(&s);
    s.use_current_folder = TRUE;
    s.name = g_strdup("*.c");
    s.family = FILE_FAMILY_CODE;
    s.content = g_strdup("TODO");
    s.content_opts.match_case = TRUE;
    s.content_opts.regex = TRUE;
    s.kind = DEEP_KIND_FILES;
    s.min_size = g_strdup("10k");
    s.after = g_strdup("7d");
    s.before = g_strdup("today");
    s.exclude = g_strdup("build/");
    s.depth = g_strdup("3");
    s.hidden = TRUE;
    s.one_filesystem = FALSE;

    gchar* text = preview_summary_search("Recent TODOs", &s);
    g_assert_nonnull(text);
    g_assert_nonnull(strstr(text, "Saved Search: Recent TODOs\n"));
    g_assert_nonnull(strstr(text, "Current folder"));
    g_assert_nonnull(strstr(text, "*.c"));
    g_assert_nonnull(strstr(text, "Text and code"));
    g_assert_nonnull(strstr(text, "TODO"));
    g_assert_nonnull(strstr(text, "Match case, Regular expression"));
    g_assert_nonnull(strstr(text, "from 10k"));
    g_assert_nonnull(strstr(text, "7d to today"));
    g_assert_nonnull(strstr(text, "build/"));
    g_assert_nonnull(strstr(text, "Hidden entries:"));
    g_assert_nonnull(strstr(text, "Run this search in Deep Search"));
    g_free(text);
    saved_deep_search_clear(&s);
    g_assert_null(preview_summary_search("x", NULL));

    /* Bookmark folder lists a saved search as "Saved search", not its raw URI */
    ExplorerRow* root = explorer_row_new_root();
    ExplorerRow* folder = add_row(root, "bm_folder", "Searches", "", "1");
    add_row(folder, "bookmark", "Large files", "lookinfiles-search://0123abcd", "");
    text = preview_summary_container(folder);
    g_assert_nonnull(strstr(text, "Large files"));
    g_assert_nonnull(strstr(text, "Saved search"));
    g_assert_null(strstr(text, "lookinfiles-search://"));
    g_free(text);
    g_object_unref(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_summary/remote", test_remote);
    g_test_add_func("/preview_summary/local", test_local);
    g_test_add_func("/preview_summary/disk", test_disk);
    g_test_add_func("/preview_summary/remote_disk", test_remote_disk);
    g_test_add_func("/preview_summary/wsl", test_wsl);
    g_test_add_func("/preview_summary/notebook", test_notebook);
    g_test_add_func("/preview_summary/bookmark_folder_and_trash", test_bookmark_folder_and_trash);
    g_test_add_func("/preview_summary/canvas", test_canvas);
    g_test_add_func("/preview_summary/lines", test_lines);
    g_test_add_func("/preview_summary/saved_search", test_saved_search);
    return g_test_run();
}
