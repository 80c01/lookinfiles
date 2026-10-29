/* Unit tests for src/spreadsheet.c (.xlsx and .ods reading, number formats). */
#include "spreadsheet.h"

#include <string.h>

static const gchar* cell(const Sheet* s, guint row, guint col) {
    gchar** cells = g_ptr_array_index(s->rows, row);
    return cells[col];
}

static void test_columns(void) {
    gchar* a = spreadsheet_column_name(0);
    gchar* z = spreadsheet_column_name(25);
    gchar* aa = spreadsheet_column_name(26);
    gchar* bz = spreadsheet_column_name(77);
    g_assert_cmpstr(a, ==, "A");
    g_assert_cmpstr(z, ==, "Z");
    g_assert_cmpstr(aa, ==, "AA");
    g_assert_cmpstr(bz, ==, "BZ");
    g_free(a);
    g_free(z);
    g_free(aa);
    g_free(bz);
    g_assert_cmpint(spreadsheet_column_index("C12"), ==, 2);
    g_assert_cmpint(spreadsheet_column_index("AA1"), ==, 26);
    g_assert_cmpint(spreadsheet_column_index("1"), ==, -1);
}

static void check_format(gdouble v, guint id, const gchar* code, const gchar* expected) {
    gchar* s = spreadsheet_format_number(v, id, code);
    g_assert_cmpstr(s, ==, expected);
    g_free(s);
}

static void test_formats(void) {
    check_format(42, 0, NULL, "42");
    check_format(0.1 + 0.2, 0, NULL, "0.3");
    check_format(1234.5, 4, NULL, "1,234.50");
    check_format(0.625, 10, NULL, "62.50%");
    check_format(0.5, 9, NULL, "50%");
    check_format(46324, 14, NULL, "2026-10-29");
    check_format(46324.4375, 22, NULL, "2026-10-29 10:30");
    check_format(0.4375, 20, NULL, "10:30");
    check_format(46324, 164, "yyyy\\-mm\\-dd", "2026-10-29");
    check_format(46324.5, 165, "dd/mm/yyyy hh:mm", "2026-10-29 12:00");
    check_format(12500, 165, "\"$\"#,##0.00", "$12,500.00");
    check_format(-3.456, 166, "0.0", "-3.5");
    check_format(1500, 167, "#,##0 \"€\";[Red]-#,##0 \"€\"", "1,500 €");
}

static void test_xlsx(void) {
    GError* error = NULL;
    g_autoptr(Workbook) book = spreadsheet_read("tests/fixtures/preview/budget.xlsx", 3, 100, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(book->format, ==, "Excel workbook");
    g_assert_cmpuint(book->sheets->len, ==, 2);
    const Sheet* s = g_ptr_array_index(book->sheets, 0);
    g_assert_cmpstr(s->name, ==, "Budget 2026");
    g_assert_true(s->loaded);
    g_assert_cmpuint(s->n_cols, ==, 4);
    g_assert_cmpuint(s->rows->len, ==, 5);  /* row 4 is empty, kept */
    g_assert_cmpstr(cell(s, 0, 0), ==, "Item");
    g_assert_cmpstr(cell(s, 1, 0), ==, "Office rent");   /* rich text runs joined */
    g_assert_cmpstr(cell(s, 1, 1), ==, "$12,500.00");
    g_assert_cmpstr(cell(s, 1, 2), ==, "2026-11-01");
    g_assert_cmpstr(cell(s, 1, 3), ==, "62.50%");
    g_assert_cmpstr(cell(s, 3, 0), ==, "");
    g_assert_cmpstr(cell(s, 4, 1), ==, "$20,000.50");    /* formula result */
    const Sheet* notes = g_ptr_array_index(book->sheets, 1);
    g_assert_cmpstr(cell(notes, 0, 0), ==, "Approved");
    g_assert_cmpstr(cell(notes, 0, 1), ==, "TRUE");
    g_assert_cmpstr(cell(notes, 0, 2), ==, "");
    g_assert_cmpstr(cell(notes, 0, 3), ==, "3.141592654");
    /* Row limit and sheet limit */
    g_autoptr(Workbook) first = spreadsheet_read("tests/fixtures/preview/budget.xlsx", 1, 2, NULL);
    const Sheet* f = g_ptr_array_index(first->sheets, 0);
    g_assert_cmpuint(f->rows->len, ==, 2);
    g_assert_true(f->truncated);
    g_assert_false(((Sheet*)g_ptr_array_index(first->sheets, 1))->loaded);
}

static void test_ods(void) {
    GError* error = NULL;
    g_autoptr(Workbook) book = spreadsheet_read("tests/fixtures/preview/stock.ods", 3, 100, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(book->format, ==, "OpenDocument spreadsheet");
    g_assert_cmpuint(book->sheets->len, ==, 2);
    const Sheet* s = g_ptr_array_index(book->sheets, 0);
    g_assert_cmpstr(s->name, ==, "Inventory");
    g_assert_cmpuint(s->n_cols, ==, 3);
    g_assert_cmpuint(s->rows->len, ==, 4);   /* repeated row twice, trailing empties dropped */
    g_assert_cmpstr(cell(s, 1, 0), ==, "Bolt M6");
    g_assert_cmpstr(cell(s, 1, 2), ==, "0,15 €");
    g_assert_cmpstr(cell(s, 3, 0), ==, "Nut");
    const Sheet* empty = g_ptr_array_index(book->sheets, 1);
    g_assert_cmpuint(empty->rows->len, ==, 0);
}

static void test_not_a_spreadsheet(void) {
    GError* error = NULL;
    g_assert_null(spreadsheet_read("tests/fixtures/preview/sample.zip", 1, 10, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/spreadsheet/columns", test_columns);
    g_test_add_func("/spreadsheet/formats", test_formats);
    g_test_add_func("/spreadsheet/xlsx", test_xlsx);
    g_test_add_func("/spreadsheet/ods", test_ods);
    g_test_add_func("/spreadsheet/not_a_spreadsheet", test_not_a_spreadsheet);
    return g_test_run();
}
