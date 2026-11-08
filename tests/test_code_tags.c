/* Unit tests for src/code/code_tags.c: ctags JSON lines, and the per-project
 * index with its cache when Universal Ctags is installed (skipped
 * otherwise). */
#include "code/code_index.h"
#include "code/code_tags.h"

#include <glib/gstdio.h>
#include <string.h>

static CodeSymbol* parse(const gchar* line, gchar** path) {
    CodeSymbol* sym = NULL;
    gboolean ok = code_tags_parse_line(line, strlen(line), path, &sym);
    g_assert_true(ok == (sym != NULL));
    return sym;
}

static void test_parse_function(void) {
    gchar* path = NULL;
    CodeSymbol* s = parse("{\"_type\": \"tag\", \"name\": \"append\", \"path\": \"/p/src/a.c\", "
                          "\"pattern\": \"/^static void append(JumpList* list, gint line) {$/\", \"line\": 38, "
                          "\"typeref\": \"typename:void\", \"kind\": \"function\", "
                          "\"signature\": \"(JumpList * list,gint line)\", \"end\": 48}",
                          &path);
    g_assert_nonnull(s);
    g_assert_cmpstr(path, ==, "/p/src/a.c");
    g_assert_cmpstr(s->name, ==, "append");
    g_assert_cmpint(s->kind, ==, CODE_SYM_FUNCTION);
    g_assert_false(s->declaration);
    g_assert_cmpint(s->line, ==, 37);
    g_assert_cmpint(s->column, ==, 12);
    g_assert_cmpstr(s->text, ==, "static void append(JumpList* list, gint line) {");
    g_assert_cmpstr(s->signature, ==, "void append(JumpList * list,gint line)");
    code_symbol_free(s);
    g_free(path);
}

static void test_parse_kinds(void) {
    gchar* path = NULL;
    CodeSymbol* s = parse("{\"_type\": \"tag\", \"name\": \"run\", \"path\": \"a.h\", \"pattern\": \"/^void run(void);$/\", "
                          "\"line\": 3, \"kind\": \"prototype\"}", &path);
    g_assert_cmpint(s->kind, ==, CODE_SYM_FUNCTION);
    g_assert_true(s->declaration);
    code_symbol_free(s);
    g_free(path);
    /* Escaped slashes and a member with a type */
    s = parse("{\"_type\": \"tag\", \"name\": \"items\", \"path\": \"a.c\", "
              "\"pattern\": \"/^    GArray* items;  \\\\/* list *\\\\/$/\", \"line\": 6, "
              "\"typeref\": \"typename:GArray *\", \"kind\": \"member\", \"scope\": \"JumpList\"}", &path);
    g_assert_cmpint(s->kind, ==, CODE_SYM_FIELD);
    g_assert_cmpstr(s->text, ==, "GArray* items;  /* list */");
    g_assert_cmpstr(s->signature, ==, "GArray * items");
    g_assert_cmpint(s->column, ==, 12);
    code_symbol_free(s);
    g_free(path);
    /* Not symbols: pseudo tags, headers, anonymous structs, garbage */
    g_assert_null(parse("{\"_type\": \"ptag\", \"name\": \"JSON_OUTPUT_VERSION\"}", &path));
    g_assert_null(parse("{\"_type\": \"tag\", \"name\": \"stdio.h\", \"path\": \"a.c\", \"line\": 1, "
                        "\"kind\": \"header\"}", &path));
    g_assert_null(parse("{\"_type\": \"tag\", \"name\": \"__anon1a2b\", \"path\": \"a.c\", \"line\": 1, "
                        "\"kind\": \"struct\"}", &path));
    g_assert_null(parse("not json", &path));
    g_assert_null(path);
}

static void write_file(const gchar* path, const gchar* text) {
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
}

static void test_index(void) {
    const gchar* program = code_tags_program();
    if (program == NULL) {
        g_test_skip("Universal Ctags is not installed");
        return;
    }
    gchar* root = g_dir_make_tmp("code_tags_XXXXXX", NULL);
    gchar* a = g_build_filename(root, "a.c", NULL);
    gchar* h = g_build_filename(root, "a.h", NULL);
    gchar* cache = g_build_filename(root, "cache", "index.tags", NULL);
    write_file(a, "#include \"a.h\"\nint total(int n)\n{\n    return n;\n}\n");
    write_file(h, "int total(int n);\nextern int verbose;\n");
    GPtrArray* files = g_ptr_array_new();
    g_ptr_array_add(files, a);
    g_ptr_array_add(files, h);

    CodeTagsIndex* idx = code_tags_index_new(program, cache);
    g_assert_true(code_tags_index_update(idx, files, NULL));
    GPtrArray* hits = code_tags_index_lookup(idx, "total");
    g_assert_cmpuint(hits->len, ==, 2);
    g_ptr_array_unref(hits);
    hits = code_tags_index_lookup(idx, "verbose");
    g_assert_cmpuint(hits->len, ==, 1);
    g_assert_true(((CodeTagsHit*)g_ptr_array_index(hits, 0))->symbol->declaration);
    g_ptr_array_unref(hits);
    hits = code_tags_index_search(idx, "TOT", 10);
    g_assert_cmpuint(hits->len, ==, 2);
    g_ptr_array_unref(hits);
    g_assert_true(g_file_test(cache, G_FILE_TEST_EXISTS));

    /* A changed file is tagged again (size changes) */
    write_file(a, "#include \"a.h\"\nint total(int n)\n{\n    return n;\n}\nint grand_total;\n");
    g_assert_true(code_tags_index_update(idx, files, NULL));
    hits = code_tags_index_lookup(idx, "grand_total");
    g_assert_cmpuint(hits->len, ==, 1);
    g_ptr_array_unref(hits);
    code_tags_index_free(idx);

    /* The cache brings the tags back in a new session */
    idx = code_tags_index_new(program, cache);
    guint nf = 0, ns = 0;
    code_tags_index_stats(idx, &nf, &ns);
    g_assert_cmpuint(nf, ==, 2);
    hits = code_tags_index_lookup(idx, "grand_total");
    g_assert_cmpuint(hits->len, ==, 1);
    g_ptr_array_unref(hits);

    /* code_index with the tags: same ranking as the scanners; an unsaved
     * buffer is scanned instead of its file */
    CodeQuery q = {"total", CODE_LANG_C, FALSE, a, NULL, idx};
    GPtrArray* m = code_index_find(&q, files, NULL, NULL);
    g_assert_cmpuint(m->len, ==, 2);
    g_assert_cmpstr(((CodeMatch*)g_ptr_array_index(m, 0))->path, ==, a);
    g_assert_false(((CodeMatch*)g_ptr_array_index(m, 0))->symbol->declaration);
    g_ptr_array_unref(m);
    GHashTable* texts = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(texts, a, "\n\nint total(int n) { return 0; }\n");
    q.texts = texts;
    m = code_index_find(&q, files, NULL, NULL);
    g_assert_cmpuint(m->len, ==, 2);
    g_assert_cmpint(((CodeMatch*)g_ptr_array_index(m, 0))->symbol->line, ==, 2);
    g_ptr_array_unref(m);
    g_hash_table_destroy(texts);

    /* A removed file leaves the index */
    g_ptr_array_remove(files, h);
    g_assert_true(code_tags_index_update(idx, files, NULL));
    code_tags_index_stats(idx, &nf, &ns);
    g_assert_cmpuint(nf, ==, 1);
    code_tags_index_free(idx);

    g_ptr_array_free(files, TRUE);
    g_unlink(cache);
    gchar* cache_dir = g_path_get_dirname(cache);
    g_rmdir(cache_dir);
    g_free(cache_dir);
    g_unlink(a);
    g_unlink(h);
    g_rmdir(root);
    g_free(cache);
    g_free(h);
    g_free(a);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_tags/parse_function", test_parse_function);
    g_test_add_func("/code_tags/parse_kinds", test_parse_kinds);
    g_test_add_func("/code_tags/index", test_index);
    return g_test_run();
}
