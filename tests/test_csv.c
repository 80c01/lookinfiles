/* Unit tests for src/formats/csv.c (separator sniffing, RFC 4180 parsing, header). */
#include "formats/csv.h"

#include <string.h>

#define PARSE(text, sep, max) csv_parse((text), strlen(text), (sep), (max))

static void test_sniff(void) {
    const gchar* comma = "a,b,c\n1,2,3\n4,5,6\n";
    g_assert_cmpint(csv_sniff_separator(comma, strlen(comma), "x.csv"), ==, ',');
    const gchar* semi = "name;price\n\"a,b\";1,5\nc;2,0\n";
    g_assert_cmpint(csv_sniff_separator(semi, strlen(semi), "x.csv"), ==, ';');
    const gchar* tabs = "a\tb\n1\t2\n";
    g_assert_cmpint(csv_sniff_separator(tabs, strlen(tabs), "x.csv"), ==, '\t');
    g_assert_cmpint(csv_sniff_separator(comma, strlen(comma), "x.tsv"), ==, '\t');
    g_assert_cmpint(csv_sniff_separator("one\ntwo\n", 8, "x.csv"), ==, ',');
    const gchar* pipes = "a|b|c\n1|2|3";
    g_assert_cmpint(csv_sniff_separator(pipes, strlen(pipes), "data.txt"), ==, '|');
}

static void test_parse_quotes(void) {
    g_autoptr(CsvData) d = PARSE("a,\"b,c\",\"say \"\"hi\"\"\"\r\n1,\"two\nlines\",3\n", ',', 10);
    g_assert_cmpuint(d->n_rows, ==, 2);
    g_assert_cmpuint(d->n_cols, ==, 3);
    gchar** r0 = g_ptr_array_index(d->rows, 0);
    g_assert_cmpstr(r0[1], ==, "b,c");
    g_assert_cmpstr(r0[2], ==, "say \"hi\"");
    gchar** r1 = g_ptr_array_index(d->rows, 1);
    g_assert_cmpstr(r1[1], ==, "two\nlines");
    g_assert_null(r1[3]);
}

static void test_parse_edges(void) {
    /* BOM, empty cells, blank lines skipped, last line without a newline */
    g_autoptr(CsvData) d = PARSE("\xEF\xBB\xBFx,,z\n\n1,2\n3", ',', 10);
    g_assert_cmpuint(d->n_rows, ==, 3);
    gchar** r0 = g_ptr_array_index(d->rows, 0);
    g_assert_cmpstr(r0[0], ==, "x");
    g_assert_cmpstr(r0[1], ==, "");
    gchar** r2 = g_ptr_array_index(d->rows, 2);
    g_assert_cmpstr(r2[0], ==, "3");
    /* Unterminated quote: the rest is one cell */
    g_autoptr(CsvData) q = PARSE("a,\"open\nmore", ',', 10);
    g_assert_cmpuint(q->n_rows, ==, 1);
    g_autoptr(CsvData) empty = PARSE("", ',', 10);
    g_assert_cmpuint(empty->n_rows, ==, 0);
}

static void test_max_rows(void) {
    GString* text = g_string_new(NULL);
    for (gint i = 0; i < 100; i++) {
        g_string_append_printf(text, "%d,x\n", i);
    }
    g_autoptr(CsvData) d = csv_parse(text->str, text->len, ',', 5);
    g_assert_cmpuint(d->rows->len, ==, 5);
    g_assert_cmpuint(d->n_rows, ==, 100);
    g_string_free(text, TRUE);
}

static void test_header(void) {
    g_autoptr(CsvData) h = PARSE("name,qty\napple,3\n", ',', 10);
    g_assert_true(csv_has_header(h));
    g_autoptr(CsvData) n = PARSE("1,2\n3,4\n", ',', 10);
    g_assert_false(csv_has_header(n));
    g_autoptr(CsvData) t = PARSE("first,last\nAda,Lovelace\n", ',', 10);
    g_assert_true(csv_has_header(t));
    g_autoptr(CsvData) same = PARSE("x,x\ny,z\n", ',', 10);
    g_assert_false(csv_has_header(same));
    g_autoptr(CsvData) one = PARSE("a,b\n", ',', 10);
    g_assert_false(csv_has_header(one));
    g_assert_cmpstr(csv_separator_name('\t'), ==, "tab");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/csv/sniff", test_sniff);
    g_test_add_func("/csv/parse_quotes", test_parse_quotes);
    g_test_add_func("/csv/parse_edges", test_parse_edges);
    g_test_add_func("/csv/max_rows", test_max_rows);
    g_test_add_func("/csv/header", test_header);
    return g_test_run();
}
