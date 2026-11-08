/* Unit tests for src/notes/notes_store.c: notebooks on disk and notes.xml */
#include <string.h>
#include "notes/notes_store.h"

#include <glib/gstdio.h>

static void remove_tree(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* name = NULL;
        while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d != NULL) g_dir_close(d);
        g_rmdir(path);
    } else {
        g_remove(path);
    }
}

static void write_file(const gchar* dir, const gchar* name, const gchar* text) {
    g_mkdir_with_parents(dir, 0700);
    gchar* path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(path);
}

static NotebookItem* notebook_at(NotesManager* mgr, guint i) {
    return (NotebookItem*)g_list_nth_data(mgr->notebooks, i);
}

static NoteItem* note_at(NotebookItem* nb, guint i) {
    return (NoteItem*)g_list_nth_data(nb->notes, i);
}

/* An empty root gets a General notebook with a welcome note */
static void test_empty_root(void) {
    gchar* root = g_dir_make_tmp("notes-XXXXXX", NULL);
    NotesManager* mgr = notes_store_load(root);
    g_assert_nonnull(mgr);
    g_assert_cmpuint(g_list_length(mgr->notebooks), ==, 1);
    NotebookItem* gen = notebook_at(mgr, 0);
    g_assert_cmpstr(gen->name, ==, "General");
    g_assert_cmpuint(g_list_length(gen->notes), ==, 1);
    NoteItem* welcome = note_at(gen, 0);
    g_assert_cmpstr(welcome->title, ==, "Welcome Note");
    g_assert_true(g_file_test(welcome->path, G_FILE_TEST_IS_REGULAR));
    g_assert_cmpint(mgr->next_note_id, ==, 1);
    gchar* trash = notes_store_trash_dir(mgr);
    g_assert_true(g_file_test(trash, G_FILE_TEST_IS_DIR));
    g_free(trash);
    notes_store_free(mgr);
    remove_tree(root);
    g_free(root);
}

/* Files on disk become notes, titled without their extension */
static void test_reconcile(void) {
    gchar* root = g_dir_make_tmp("notes-XXXXXX", NULL);
    gchar* work = g_build_filename(root, "Work", NULL);
    write_file(work, "Plan.md", "# Plan\n");
    write_file(work, ".hidden", "x");
    NotesManager* mgr = notes_store_load(root);
    g_assert_cmpuint(g_list_length(mgr->notebooks), ==, 1); /* no General added */
    NotebookItem* nb = notebook_at(mgr, 0);
    g_assert_cmpstr(nb->name, ==, "Work");
    g_assert_cmpuint(g_list_length(nb->notes), ==, 1);
    g_assert_cmpstr(note_at(nb, 0)->filename, ==, "Plan.md");
    g_assert_cmpstr(note_at(nb, 0)->title, ==, "Plan");
    g_assert_cmpint(note_at(nb, 0)->type, ==, NOTE_TYPE_MARKDOWN);
    notes_store_free(mgr);
    remove_tree(root);
    g_free(work);
    g_free(root);
}

/* The manifest keeps the order, titles, icons, expansion and the trash */
static void test_save_load(void) {
    gchar* root = g_dir_make_tmp("notes-XXXXXX", NULL);
    gchar* a = g_build_filename(root, "Alpha", NULL);
    gchar* b = g_build_filename(root, "Beta", NULL);
    write_file(a, "one.txt", "1");
    write_file(a, "two.txt", "2");
    write_file(b, "three.org", "* 3");
    NotesManager* mgr = notes_store_load(root);
    g_assert_cmpuint(g_list_length(mgr->notebooks), ==, 2);

    /* Beta first, Alpha's notes reversed and renamed, Alpha collapsed */
    NotebookItem* alpha = NULL;
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        if (g_strcmp0(((NotebookItem*)l->data)->name, "Alpha") == 0) alpha = l->data;
    }
    g_assert_nonnull(alpha);
    mgr->notebooks = g_list_remove(mgr->notebooks, alpha);
    mgr->notebooks = g_list_append(mgr->notebooks, alpha);
    /* Disk order is not sorted: put two.txt first whatever it was */
    NoteItem* first = note_at(alpha, 0);
    if (g_strcmp0(first->filename, "two.txt") != 0) {
        alpha->notes = g_list_reverse(alpha->notes);
        first = note_at(alpha, 0);
    }
    g_assert_cmpstr(first->filename, ==, "two.txt");
    alpha->expanded = FALSE;
    g_free(first->title);
    first->title = g_strdup("Second <&> note");
    mgr->sort_mode = 2;
    mgr->next_note_id = 7;
    mgr->trash_expanded = TRUE;

    /* A trashed note whose file exists, and one whose file is gone */
    gchar* trash = notes_store_trash_dir(mgr);
    write_file(trash, "old.md", "old");
    gchar* old_path = g_build_filename(trash, "old.md", NULL);
    mgr->trash = g_list_append(mgr->trash, trash_item_new("old.md", "Old", old_path, "Beta", 42,
                                                          NOTE_TYPE_MARKDOWN));
    gchar* gone_path = g_build_filename(trash, "gone.txt", NULL);
    mgr->trash = g_list_append(mgr->trash, trash_item_new("gone.txt", "Gone", gone_path, "Beta", 43,
                                                          NOTE_TYPE_TEXT));
    g_assert_true(notes_store_save(mgr, NULL));
    notes_store_free(mgr);

    mgr = notes_store_load(root);
    g_assert_cmpint(mgr->sort_mode, ==, 2);
    g_assert_cmpint(mgr->next_note_id, ==, 7);
    g_assert_true(mgr->trash_expanded);
    g_assert_cmpuint(g_list_length(mgr->notebooks), ==, 2);
    g_assert_cmpstr(notebook_at(mgr, 0)->name, ==, "Beta");
    alpha = notebook_at(mgr, 1);
    g_assert_cmpstr(alpha->name, ==, "Alpha");
    g_assert_false(alpha->expanded);
    g_assert_cmpuint(g_list_length(alpha->notes), ==, 2);
    g_assert_cmpstr(note_at(alpha, 0)->filename, ==, "two.txt");
    g_assert_cmpstr(note_at(alpha, 0)->title, ==, "Second <&> note");
    g_assert_cmpstr(note_at(alpha, 1)->filename, ==, "one.txt");
    g_assert_cmpuint(g_list_length(mgr->trash), ==, 1);
    TrashNoteItem* t = mgr->trash->data;
    g_assert_cmpstr(t->filename, ==, "old.md");
    g_assert_cmpstr(t->original_nb, ==, "Beta");
    g_assert_cmpint(t->trashed_at, ==, 42);
    g_assert_cmpint(t->type, ==, NOTE_TYPE_MARKDOWN);
    notes_store_free(mgr);

    remove_tree(root);
    g_free(gone_path);
    g_free(old_path);
    g_free(trash);
    g_free(b);
    g_free(a);
    g_free(root);
}

/* A note listed in the manifest but deleted on disk is dropped */
static void test_missing_note(void) {
    gchar* root = g_dir_make_tmp("notes-XXXXXX", NULL);
    gchar* nb_dir = g_build_filename(root, "Work", NULL);
    write_file(nb_dir, "kept.txt", "k");
    gchar* manifest = g_build_filename(root, NOTES_STORE_MANIFEST, NULL);
    g_assert_true(g_file_set_contents(manifest,
        "<notes version=\"1\"><notebook name=\"Work\" icon=\"notebook\" expanded=\"true\">"
        "<note filename=\"lost.txt\" title=\"Lost\" type=\"text\"/>"
        "<note filename=\"kept.txt\" title=\"Kept\" type=\"text\"/>"
        "</notebook><trash/></notes>", -1, NULL));
    NotesManager* mgr = notes_store_load(root);
    NotebookItem* nb = notebook_at(mgr, 0);
    g_assert_cmpuint(g_list_length(nb->notes), ==, 1);
    g_assert_cmpstr(note_at(nb, 0)->title, ==, "Kept");
    notes_store_free(mgr);
    remove_tree(root);
    g_free(manifest);
    g_free(nb_dir);
    g_free(root);
}

/* A malformed manifest is ignored: the notes come back from the disk */
static void test_malformed_manifest(void) {
    gchar* root = g_dir_make_tmp("notes-XXXXXX", NULL);
    gchar* nb_dir = g_build_filename(root, "Work", NULL);
    write_file(nb_dir, "a.txt", "a");
    gchar* manifest = g_build_filename(root, NOTES_STORE_MANIFEST, NULL);
    g_assert_true(g_file_set_contents(manifest, "<notes><notebook name=", -1, NULL));
    g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*malformed*");
    NotesManager* mgr = notes_store_load(root);
    g_test_assert_expected_messages();
    g_assert_cmpuint(g_list_length(mgr->notebooks), ==, 1);
    g_assert_cmpstr(note_at(notebook_at(mgr, 0), 0)->filename, ==, "a.txt");
    notes_store_free(mgr);
    remove_tree(root);
    g_free(manifest);
    g_free(nb_dir);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/notes_store/empty_root", test_empty_root);
    g_test_add_func("/notes_store/reconcile", test_reconcile);
    g_test_add_func("/notes_store/save_load", test_save_load);
    g_test_add_func("/notes_store/missing_note", test_missing_note);
    g_test_add_func("/notes_store/malformed_manifest", test_malformed_manifest);
    return g_test_run();
}
