/* Unit tests for src/theme/file_type_icons.c (VS Code Seti file icons). */
#include "theme/file_type_icons.h"

static void assert_icon(const gchar* name, const gchar* icon, const gchar* color) {
    FileTypeIcon ft = file_type_icon(name, FALSE);
    g_assert_cmpstr(ft.icon, ==, icon);
    g_assert_cmpstr(ft.color, ==, color);
}

static void test_extensions(void) {
    assert_icon("main.c", "c", "blue");
    assert_icon("main.h", "c", "purple");
    assert_icon("README.MD", "info", "blue");
    assert_icon("notes.md", "markdown", "blue");
    assert_icon("view.cpp", "cpp", "blue");
    assert_icon("view.hpp", "cpp", "purple");
    assert_icon("tool.py", "python", "blue");
    assert_icon("app.js", "javascript", "yellow");
    assert_icon("app.ts", "typescript", "blue");
    assert_icon("data.json", "json", "yellow");
    assert_icon("run.sh", "shell", "green");
}

/* Hand-drawn icons for the formats Seti has none for */
static void test_handmade(void) {
    assert_icon("todo.org", "org", "green");
    assert_icon("done.org_archive", "org", "green");
    assert_icon("board.canvas", "canvas", "purple");
    assert_icon("talk.pptx", "slides", "orange");
    assert_icon("talk.odp", "slides", "orange");
    assert_icon("novel.epub", "book", "yellow");
}

/* Formats Seti leaves generic get the icon of a sibling format */
static void test_aliases(void) {
    assert_icon("letter.odt", "word", "blue");
    assert_icon("budget.ods", "xls", "green");
    assert_icon("cache.sqlite", "db", "pink");
}

static void test_compound_extensions(void) {
    assert_icon("app.spec.ts", "typescript", "orange");
    assert_icon("archive.tar.zip", "zip", "grey_light");
}

static void test_file_names(void) {
    assert_icon("Makefile", "makefile", "orange");
    assert_icon("CMakeLists.txt", "makefile", "blue");
    assert_icon("LICENSE", "license", "yellow");
    assert_icon("Dockerfile", "docker", "blue");
    assert_icon(".gitignore", "git", "ignore");
    assert_icon(".bashrc", "shell", "green");
}

static void test_defaults(void) {
    assert_icon("unknown.zzz", "default", "white");
    assert_icon("noext", "default", "white");
    assert_icon("", "default", "white");
    assert_icon(NULL, "default", "white");
    g_assert_cmpstr(file_type_icon("src", TRUE).icon, ==, "folder");
}

/* Every colour of the table is a known colour (a palette role icon_<color>) */
static void test_colors_known(void) {
    const gchar* names[] = {"a.c", "a.h", "a.md", "a.js", "a.sh", "a.zip", "a.pdf",
                            "a.vue", "a.rs", "a.yml", "a.ds_store", "TODO", NULL};
    for (gsize i = 0; names[i] != NULL; i++) {
        const gchar* color = file_type_icon(names[i], FALSE).color;
        gboolean known = FALSE;
        for (gsize j = 0; FILE_TYPE_ICON_COLORS[j] != NULL; j++) {
            known = known || g_strcmp0(color, FILE_TYPE_ICON_COLORS[j]) == 0;
        }
        g_assert_true(known);
    }
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file_type_icons/extensions", test_extensions);
    g_test_add_func("/file_type_icons/handmade", test_handmade);
    g_test_add_func("/file_type_icons/aliases", test_aliases);
    g_test_add_func("/file_type_icons/compound_extensions", test_compound_extensions);
    g_test_add_func("/file_type_icons/file_names", test_file_names);
    g_test_add_func("/file_type_icons/defaults", test_defaults);
    g_test_add_func("/file_type_icons/colors_known", test_colors_known);
    return g_test_run();
}
