/* Unit tests for src/jupyter/ipynb_doc.c (notebooks held for editing). */
#include "jupyter/ipynb_doc.h"

#include <string.h>

#define FIXTURE "tests/fixtures/jupyter/roundtrip.ipynb"

static IpynbDoc* load_fixture(gchar** text) {
    gsize len = 0;
    g_assert_true(g_file_get_contents(FIXTURE, text, &len, NULL));
    GError* error = NULL;
    IpynbDoc* doc = ipynb_doc_parse(*text, len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(doc);
    return doc;
}

/* A notebook written by Jupyter is written back byte for byte */
static void test_roundtrip(void) {
    gchar* text = NULL;
    IpynbDoc* doc = load_fixture(&text);
    gchar* out = ipynb_doc_to_json(doc);
    g_assert_cmpstr(out, ==, text);
    g_free(out);
    ipynb_doc_free(doc);
    g_free(text);
}

static void test_read_cells(void) {
    gchar* text = NULL;
    IpynbDoc* doc = load_fixture(&text);
    g_assert_cmpstr(ipynb_doc_language(doc), ==, "python");
    g_assert_cmpstr(ipynb_doc_file_extension(doc), ==, ".py");
    g_assert_cmpstr(ipynb_doc_kernel(doc), ==, "Python 3");
    g_assert_cmpuint(ipynb_doc_n_cells(doc), ==, 2);
    g_assert_cmpint(ipynb_doc_cell_type(doc, 0), ==, IPYNB_CELL_MARKDOWN);
    g_assert_cmpint(ipynb_doc_cell_type(doc, 1), ==, IPYNB_CELL_CODE);
    gchar* src = ipynb_doc_cell_source(doc, 0);
    g_assert_cmpstr(src, ==, "# Titre é\n\"quoted\" \\ tab\there");
    g_free(src);
    g_assert_cmpint(ipynb_doc_cell_execution_count(doc, 0), ==, -1);
    g_assert_cmpint(ipynb_doc_cell_execution_count(doc, 1), ==, 3);

    GPtrArray* outs = ipynb_doc_cell_outputs(doc, 1, 0);
    g_assert_cmpuint(outs->len, ==, 3);
    IpynbOutput* o = g_ptr_array_index(outs, 0);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_TEXT);
    g_assert_cmpstr(o->text, ==, "hello\nworld");
    o = g_ptr_array_index(outs, 1);
    g_assert_true(o->result);
    g_assert_cmpstr(o->text, ==, "42");
    o = g_ptr_array_index(outs, 2);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_ERROR);
    g_assert_cmpstr(o->text, ==, "ZeroDivisionError: division by zero");
    g_ptr_array_unref(outs);
    ipynb_doc_free(doc);
    g_free(text);
}

static void test_edit(void) {
    gchar* text = NULL;
    IpynbDoc* doc = load_fixture(&text);
    ipynb_doc_set_source(doc, 1, "a = 1\nb = 2\n");
    gchar* out = ipynb_doc_to_json(doc);
    g_assert_nonnull(strstr(out, "   \"source\": [\n    \"a = 1\\n\",\n    \"b = 2\\n\"\n   ]\n"));
    g_free(out);

    /* Empty source: an empty list */
    ipynb_doc_set_source(doc, 0, "");
    out = ipynb_doc_to_json(doc);
    g_assert_nonnull(strstr(out, "\"metadata\": {},\n   \"source\": []\n  },"));
    g_free(out);

    /* Code to Markdown: no outputs nor execution count; and back */
    ipynb_doc_set_type(doc, 1, IPYNB_CELL_MARKDOWN);
    out = ipynb_doc_to_json(doc);
    g_assert_null(strstr(out, "\"outputs\""));
    g_assert_null(strstr(out, "\"execution_count\""));
    g_free(out);
    ipynb_doc_set_type(doc, 1, IPYNB_CELL_CODE);
    out = ipynb_doc_to_json(doc);
    g_assert_nonnull(strstr(out, "   \"cell_type\": \"code\",\n   \"execution_count\": null,\n   \"id\": \"e5f6a7b8\",\n"
                                 "   \"metadata\": {\n"));
    g_assert_nonnull(strstr(out, "   \"outputs\": [],\n   \"source\": ["));
    g_free(out);
    ipynb_doc_free(doc);
    g_free(text);
}

static void test_cells_and_outputs(void) {
    gchar* text = NULL;
    IpynbDoc* doc = load_fixture(&text);
    g_assert_true(ipynb_doc_clear_outputs(doc, G_MAXUINT));
    g_assert_false(ipynb_doc_clear_outputs(doc, 1));
    g_assert_cmpint(ipynb_doc_cell_execution_count(doc, 1), ==, -1);
    GPtrArray* outs = ipynb_doc_cell_outputs(doc, 1, 0);
    g_assert_cmpuint(outs->len, ==, 0);
    g_ptr_array_unref(outs);

    guint at = ipynb_doc_insert_cell(doc, 99, IPYNB_CELL_RAW);
    g_assert_cmpuint(at, ==, 2);
    g_assert_cmpint(ipynb_doc_cell_type(doc, 2), ==, IPYNB_CELL_RAW);
    ipynb_doc_set_source(doc, 2, "raw");
    ipynb_doc_move_cell(doc, 2, 0);
    gchar* src = ipynb_doc_cell_source(doc, 0);
    g_assert_cmpstr(src, ==, "raw");
    g_free(src);
    /* Taken out and put back: the same cell */
    IpynbCell* cell = ipynb_doc_take_cell(doc, 0);
    g_assert_nonnull(cell);
    g_assert_cmpuint(ipynb_doc_n_cells(doc), ==, 2);
    g_assert_cmpuint(ipynb_doc_put_cell(doc, 1, cell), ==, 1);
    src = ipynb_doc_cell_source(doc, 1);
    g_assert_cmpstr(src, ==, "raw");
    g_free(src);
    g_assert_null(ipynb_doc_take_cell(doc, 9));
    ipynb_cell_free(ipynb_doc_take_cell(doc, 1));
    g_assert_cmpuint(ipynb_doc_n_cells(doc), ==, 2);
    g_assert_cmpint(ipynb_doc_cell_type(doc, 0), ==, IPYNB_CELL_MARKDOWN);
    ipynb_doc_free(doc);
    g_free(text);
}

static void test_new_and_invalid(void) {
    IpynbDoc* doc = ipynb_doc_new();
    g_assert_cmpuint(ipynb_doc_n_cells(doc), ==, 1);
    gchar* out = ipynb_doc_to_json(doc);
    g_assert_true(g_str_has_prefix(out, "{\n \"cells\": [\n  {\n   \"cell_type\": \"code\",\n"
                                        "   \"execution_count\": null,\n   \"id\": \""));
    g_assert_true(g_str_has_suffix(out, "\"metadata\": {},\n \"nbformat\": 4,\n \"nbformat_minor\": 5\n}\n"));
    /* What it writes reads back */
    IpynbDoc* again = ipynb_doc_parse(out, strlen(out), NULL);
    g_assert_nonnull(again);
    g_assert_cmpint(ipynb_doc_cell_type(again, 0), ==, IPYNB_CELL_CODE);
    ipynb_doc_free(again);
    g_free(out);
    ipynb_doc_free(doc);

    GError* error = NULL;
    const gchar* v3 = "{\"nbformat\": 3, \"worksheets\": []}";
    g_assert_null(ipynb_doc_parse(v3, strlen(v3), &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_null(ipynb_doc_parse("[1, 2", 5, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
}

static void test_long_output_and_image(void) {
    GString* nb = g_string_new("{\"nbformat\": 4, \"nbformat_minor\": 2, \"metadata\": {}, \"cells\": ["
                               "{\"cell_type\": \"code\", \"source\": \"\", \"outputs\": ["
                               "{\"output_type\": \"stream\", \"name\": \"stderr\", \"text\": \"");
    for (gint i = 0; i < 10; i++) g_string_append_printf(nb, "line %d\\n", i);
    g_string_append(nb, "\"}, {\"output_type\": \"display_data\", \"data\": {\"image/png\": \"iVBO\\nRw==\\n\", "
                        "\"text/plain\": \"<Figure>\"}}, "
                        "{\"output_type\": \"display_data\", \"data\": {\"text/html\": \"<b>x</b>\"}}, "
                        "{\"output_type\": \"execute_result\", \"data\": {\"text/latex\": \"$\\\\displaystyle \\\\frac{1}{x}$\", "
                        "\"text/plain\": \"1/x\"}}, "
                        "{\"output_type\": \"display_data\", \"data\": {\"text/latex\": \"\\\\begin{tabular}{l}\\\\end{tabular}\", "
                        "\"text/plain\": \"a table\"}}]}]}");
    IpynbDoc* doc = ipynb_doc_parse(nb->str, nb->len, NULL);
    g_assert_nonnull(doc);
    GPtrArray* outs = ipynb_doc_cell_outputs(doc, 0, 3);
    g_assert_cmpuint(outs->len, ==, 5);
    IpynbOutput* o = g_ptr_array_index(outs, 0);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_ERROR);
    g_assert_true(g_str_has_prefix(o->text, "line 0\nline 1\nline 2\n… (7 more lines)"));
    o = g_ptr_array_index(outs, 1);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_IMAGE);
    g_assert_cmpstr(o->mime, ==, "image/png");
    g_assert_cmpuint(g_bytes_get_size(o->image), ==, 4);
    g_assert_cmpmem(g_bytes_get_data(o->image, NULL), 4, "\x89PNG", 4);
    o = g_ptr_array_index(outs, 2);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_TEXT);
    g_assert_nonnull(strstr(o->text, "HTML"));
    o = g_ptr_array_index(outs, 3);
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_MATH);
    g_assert_cmpstr(o->text, ==, "\\displaystyle \\frac{1}{x}");
    g_assert_true(o->result);
    o = g_ptr_array_index(outs, 4); /* not a formula: its plain text */
    g_assert_cmpint(o->kind, ==, IPYNB_OUTPUT_TEXT);
    g_assert_cmpstr(o->text, ==, "a table");
    g_ptr_array_unref(outs);
    /* nbformat 4.2: new cells have no id */
    ipynb_doc_insert_cell(doc, 0, IPYNB_CELL_MARKDOWN);
    gchar* out = ipynb_doc_to_json(doc);
    g_assert_true(g_str_has_prefix(out, "{\n \"nbformat\": 4,\n \"nbformat_minor\": 2,\n \"metadata\": {},\n"
                                        " \"cells\": [\n  {\n   \"cell_type\": \"markdown\",\n   \"metadata\": {},\n"
                                        "   \"source\": []\n  },"));
    g_free(out);
    ipynb_doc_free(doc);
    g_string_free(nb, TRUE);
}

static void test_keys(void) {
    gchar* text = NULL;
    IpynbDoc* doc = load_fixture(&text);
    guint k0 = ipynb_doc_cell_key(doc, 0);
    guint k1 = ipynb_doc_cell_key(doc, 1);
    g_assert_cmpuint(k0, !=, 0);
    g_assert_cmpuint(k0, !=, k1);
    g_assert_cmpuint(ipynb_doc_cell_key(doc, 9), ==, 0);
    guint k2 = ipynb_doc_cell_key(doc, ipynb_doc_insert_cell(doc, 0, IPYNB_CELL_CODE));
    g_assert_cmpint(ipynb_doc_find_cell(doc, k1), ==, 2);
    ipynb_doc_move_cell(doc, 2, 0);
    g_assert_cmpint(ipynb_doc_find_cell(doc, k1), ==, 0);
    g_assert_cmpint(ipynb_doc_find_cell(doc, k2), ==, 1);
    IpynbCell* cell = ipynb_doc_take_cell(doc, 0);
    g_assert_cmpint(ipynb_doc_find_cell(doc, k1), ==, -1);
    ipynb_doc_put_cell(doc, 2, cell);
    g_assert_cmpint(ipynb_doc_find_cell(doc, k1), ==, 2);
    g_assert_cmpint(ipynb_doc_find_cell(doc, 0), ==, -1);
    ipynb_doc_free(doc);
    g_free(text);
}

static void test_append_outputs(void) {
    IpynbDoc* doc = ipynb_doc_new();
    ipynb_doc_set_source(doc, 0, "print(1)");
    const gchar* s1 = "{\"name\": \"stdout\", \"output_type\": \"stream\", \"text\": \"a\\nb\"}";
    const gchar* s2 = "{\"name\": \"stdout\", \"output_type\": \"stream\", \"text\": \"c\\n\"}";
    const gchar* r = "{\"data\": {\"image/png\": \"iVBORw==\", \"text/plain\": \"<Figure>\\nsize\"}, "
                     "\"execution_count\": 4, \"metadata\": {}, \"output_type\": \"execute_result\"}";
    g_assert_true(ipynb_doc_append_output(doc, 0, s1, strlen(s1), NULL));
    g_assert_true(ipynb_doc_append_output(doc, 0, s2, strlen(s2), NULL));
    g_assert_true(ipynb_doc_append_output(doc, 0, r, strlen(r), NULL));
    ipynb_doc_set_execution_count(doc, 0, 4);
    g_assert_cmpint(ipynb_doc_cell_execution_count(doc, 0), ==, 4);
    GPtrArray* outs = ipynb_doc_cell_outputs(doc, 0, 0);
    g_assert_cmpuint(outs->len, ==, 2);
    g_assert_cmpstr(((IpynbOutput*)g_ptr_array_index(outs, 0))->text, ==, "a\nbc");
    g_assert_cmpint(((IpynbOutput*)g_ptr_array_index(outs, 1))->kind, ==, IPYNB_OUTPUT_IMAGE);
    g_ptr_array_unref(outs);
    gchar* out = ipynb_doc_to_json(doc);
    /* Stored as Jupyter stores them: the stream joined, text split in lines,
     * base64 data as one string */
    g_assert_nonnull(strstr(out, "     \"name\": \"stdout\",\n     \"output_type\": \"stream\",\n"
                                 "     \"text\": [\n      \"a\\n\",\n      \"bc\\n\"\n     ]\n"));
    g_assert_nonnull(strstr(out, "      \"image/png\": \"iVBORw==\",\n      \"text/plain\": [\n"
                                 "       \"<Figure>\\n\",\n       \"size\"\n      ]\n"));
    g_assert_nonnull(strstr(out, "   \"execution_count\": 4,\n"));
    g_free(out);

    GError* error = NULL;
    g_assert_false(ipynb_doc_append_output(doc, 0, "[1]", 3, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    ipynb_doc_set_type(doc, 0, IPYNB_CELL_MARKDOWN);
    g_assert_false(ipynb_doc_append_output(doc, 0, s1, strlen(s1), &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    ipynb_doc_free(doc);
}

static void test_kernel_info(void) {
    IpynbDoc* doc = ipynb_doc_new();
    g_assert_null(ipynb_doc_kernel_name(doc));
    const gchar* spec = "{\"display_name\": \"Python 3 (ipykernel)\", \"language\": \"python\", \"name\": \"python3\"}";
    const gchar* info = "{\"file_extension\": \".py\", \"name\": \"python\", \"version\": \"3.12.1\"}";
    g_assert_true(ipynb_doc_set_kernel_info(doc, spec, info));
    g_assert_false(ipynb_doc_set_kernel_info(doc, spec, info));
    g_assert_cmpstr(ipynb_doc_kernel_name(doc), ==, "python3");
    g_assert_cmpstr(ipynb_doc_kernel(doc), ==, "Python 3 (ipykernel)");
    g_assert_cmpstr(ipynb_doc_file_extension(doc), ==, ".py");
    gchar* out = ipynb_doc_to_json(doc);
    g_assert_nonnull(strstr(out, " \"metadata\": {\n  \"kernelspec\": {\n   \"display_name\": \"Python 3 (ipykernel)\",\n"));
    g_free(out);
    ipynb_doc_free(doc);
}

static void test_cell_markdown(void) {
    const gchar* nb = "{\"cells\": [{\"cell_type\": \"markdown\", \"metadata\": {}, \"source\": [\"![p](attachment:shot.png)\"], "
                      "\"attachments\": {\"shot.png\": {\"image/png\": \"iVBORw==\\n\"}}}], "
                      "\"metadata\": {}, \"nbformat\": 4, \"nbformat_minor\": 5}";
    IpynbDoc* doc = ipynb_doc_parse(nb, strlen(nb), NULL);
    g_assert_nonnull(doc);
    gchar* md = ipynb_doc_cell_markdown(doc, 0);
    g_assert_cmpstr(md, ==, "![p](data:image/png;base64,iVBORw==)");
    g_free(md);
    md = ipynb_doc_cell_source(doc, 0);
    g_assert_cmpstr(md, ==, "![p](attachment:shot.png)");
    g_free(md);
    ipynb_doc_free(doc);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ipynb_doc/roundtrip", test_roundtrip);
    g_test_add_func("/ipynb_doc/read_cells", test_read_cells);
    g_test_add_func("/ipynb_doc/edit", test_edit);
    g_test_add_func("/ipynb_doc/cells_and_outputs", test_cells_and_outputs);
    g_test_add_func("/ipynb_doc/new_and_invalid", test_new_and_invalid);
    g_test_add_func("/ipynb_doc/long_output_and_image", test_long_output_and_image);
    g_test_add_func("/ipynb_doc/keys", test_keys);
    g_test_add_func("/ipynb_doc/append_outputs", test_append_outputs);
    g_test_add_func("/ipynb_doc/kernel_info", test_kernel_info);
    g_test_add_func("/ipynb_doc/cell_markdown", test_cell_markdown);
    return g_test_run();
}
