/* Unit tests for src/code/code_lint.c (JSON, TOML, YAML, XML built-in diagnostics) */
#include "code/code_lint.h"

#include <string.h>

static void test_source_names(void) {
    g_assert_cmpstr(code_lint_source_name("/a/foo.json"), ==, "json");
    g_assert_cmpstr(code_lint_source_name("/a/foo.toml"), ==, "toml");
    g_assert_cmpstr(code_lint_source_name("/a/foo.yaml"), ==, "yaml");
    g_assert_cmpstr(code_lint_source_name("/a/foo.yml"), ==, "yaml");
    g_assert_cmpstr(code_lint_source_name("/a/foo.xml"), ==, "xml");
    g_assert_null(code_lint_source_name("/a/foo.c"));
    g_assert_null(code_lint_source_name("/a/foo.txt"));
    g_assert_null(code_lint_source_name(NULL));
}

static void test_json(void) {
    const gchar* ok = "{\n  \"a\": 1,\n  \"b\": [true, false]\n}\n";
    GPtrArray* d = code_lint_check("test.json", ok, strlen(ok));
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);

    const gchar* bad = "{\n  \"a\": 1,\n  \"b\": [1, 2\n}\n";
    d = code_lint_check("test.json", bad, strlen(bad));
    g_assert_cmpuint(d->len, ==, 1);
    LspDiagnostic* diag = g_ptr_array_index(d, 0);
    g_assert_cmpint(diag->severity, ==, LSP_SEVERITY_ERROR);
    g_assert_cmpstr(diag->source, ==, "json");
    g_assert_cmpint(diag->line, ==, 3);
    g_assert_nonnull(diag->message);
    g_assert_true(diag->message[0] != '\0');
    g_ptr_array_unref(d);

    /* Blank file: no noisy error */
    d = code_lint_check("empty.json", "   \n", 4);
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);
}

static void test_toml(void) {
    const gchar* ok = "[server]\nport = 8080\n";
    GPtrArray* d = code_lint_check("cfg.toml", ok, strlen(ok));
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);

    const gchar* bad = "[server]\nport = \n";
    d = code_lint_check("cfg.toml", bad, strlen(bad));
    g_assert_cmpuint(d->len, ==, 1);
    LspDiagnostic* diag = g_ptr_array_index(d, 0);
    g_assert_cmpstr(diag->source, ==, "toml");
    g_assert_cmpint(diag->line, ==, 1);
    g_ptr_array_unref(d);
}

static void test_yaml(void) {
    const gchar* ok = "name: test\nitems:\n  - one\n  - two\n";
    GPtrArray* d = code_lint_check("cfg.yaml", ok, strlen(ok));
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);

    const gchar* bad = "a: [1, 2\n";
    d = code_lint_check("cfg.yml", bad, strlen(bad));
    g_assert_cmpuint(d->len, ==, 1);
    LspDiagnostic* diag = g_ptr_array_index(d, 0);
    g_assert_cmpstr(diag->source, ==, "yaml");
    g_ptr_array_unref(d);
}

static void test_xml(void) {
    const gchar* ok = "<root><item id=\"1\">ok</item></root>\n";
    GPtrArray* d = code_lint_check("doc.xml", ok, strlen(ok));
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);

    const gchar* bad = "<root>\n  <item>broken</other>\n</root>\n";
    d = code_lint_check("doc.xml", bad, strlen(bad));
    g_assert_cmpuint(d->len, ==, 1);
    LspDiagnostic* diag = g_ptr_array_index(d, 0);
    g_assert_cmpstr(diag->source, ==, "xml");
    g_assert_cmpint(diag->line, ==, 1);
    g_ptr_array_unref(d);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_lint/source_names", test_source_names);
    g_test_add_func("/code_lint/json", test_json);
    g_test_add_func("/code_lint/toml", test_toml);
    g_test_add_func("/code_lint/yaml", test_yaml);
    g_test_add_func("/code_lint/xml", test_xml);
    return g_test_run();
}
