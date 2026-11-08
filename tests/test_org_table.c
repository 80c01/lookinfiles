/* Unit tests for src/org/org_table.c: Org-mode table alignment and editing.
 * "‸" marks the cursor. */
#include "org/org_table.h"
#include "org_test_util.h"

typedef gboolean (*Op)(GPtrArray* lines, OrgPos* pos);

static void check(const gchar* before, Op op, gboolean ok, const gchar* after) {
    OrgPos pos;
    GPtrArray* lines = doc_new(before, &pos);
    gboolean r = op(lines, &pos);
    g_assert_cmpint(r, ==, ok);
    gchar* got = doc_text(lines, &pos);
    g_assert_cmpstr(got, ==, after);
    g_free(got);
}

static gboolean op_row_up(GPtrArray* l, OrgPos* p) { return org_table_move_row(l, p, -1, NULL); }
static gboolean op_col_right(GPtrArray* l, OrgPos* p) { return org_table_move_column(l, p, 1, NULL); }
static gboolean op_col_left(GPtrArray* l, OrgPos* p) { return org_table_move_column(l, p, -1, NULL); }

static void test_bounds(void) {
    GPtrArray* l = org_lines_new("text\n| a |\n|---|\n| b |\nafter");
    gint s, e;
    g_assert_true(org_table_bounds(l, 2, &s, &e));
    g_assert_cmpint(s, ==, 1);
    g_assert_cmpint(e, ==, 4);
    g_assert_false(org_table_bounds(l, 0, &s, &e));
    g_ptr_array_unref(l);
}

static void test_align(void) {
    check("|name|qty|\n|-\n|apple‸|3|\n|kiwi|12|", org_table_align, TRUE,
          "| name  | qty |\n|-------+-----|\n| apple‸ |   3 |\n| kiwi  |  12 |");
    /* Missing cells are added, indentation kept */
    check("  | a | b‸\n  | c |", org_table_align, TRUE, "  | a | b‸ |\n  | c |   |");
    /* Wide characters count as one column each */
    check("| é‸ | x |\n| ab | y |", org_table_align, TRUE, "| é‸  | x |\n| ab | y |");
    check("no table‸", org_table_align, FALSE, "no table‸");
}

static void test_next_prev_field(void) {
    check("| a‸ | b |\n| c | d |", org_table_next_field, TRUE, "| a | ‸b |\n| c | d |");
    check("| a | b‸ |\n|---+---|\n| c | d |", org_table_next_field, TRUE, "| a | b |\n|---+---|\n| ‸c | d |");
    /* After the last cell: a new row */
    check("| a | b‸ |", org_table_next_field, TRUE, "| a | b |\n| ‸  |   |");
    /* "|-" becomes a rule */
    check("| a | b |\n|-‸", org_table_next_field, TRUE, "| a | b |\n|---+---|\n| ‸  |   |");
    check("| a | b |\n| c‸ | d |", org_table_prev_field, TRUE, "| a | ‸b |\n| c | d |");
    check("| a‸ | b |", org_table_prev_field, TRUE, "| ‸a | b |");
}

static void test_next_row(void) {
    check("| a | b‸ |\n| c | d |", org_table_next_row, TRUE, "| a | b |\n| c | ‸d |");
    check("| a | b‸ |", org_table_next_row, TRUE, "| a | b |\n|   | ‸  |");
    check("| a‸ | b |\n|---+---|", org_table_next_row, TRUE, "| a | b |\n| ‸  |   |\n|---+---|");
}

static void test_rows_and_columns(void) {
    check("| a |\n| b‸ |", op_row_up, TRUE, "| b‸ |\n| a |");
    check("| a‸ |\n| b |", op_row_up, FALSE, "| a‸ |\n| b |");
    check("| a‸ | bb |\n| c | d |", op_col_right, TRUE, "| bb | a‸ |\n| d  | c |");
    check("| a‸ | b |", op_col_left, FALSE, "| a‸ | b |");
    check("| a |\n| b‸ |", org_table_insert_row, TRUE, "| a |\n| ‸  |\n| b |");
    check("| a |\n| b‸ |\n| c |", org_table_delete_row, TRUE, "| a |\n| ‸c |");
    check("| a | b‸ |", org_table_insert_column, TRUE, "| a | ‸  | b |");
    check("| a | b‸ | c |", org_table_delete_column, TRUE, "| a | ‸c |");
    check("x\n| a‸ |\ny", org_table_delete_column, TRUE, "x\n‸y");
    check("| a‸ |\n| b |", org_table_insert_hline, TRUE, "| a‸ |\n|---|\n| b |");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/org_table/bounds", test_bounds);
    g_test_add_func("/org_table/align", test_align);
    g_test_add_func("/org_table/next_prev_field", test_next_prev_field);
    g_test_add_func("/org_table/next_row", test_next_row);
    g_test_add_func("/org_table/rows_and_columns", test_rows_and_columns);
    return g_test_run();
}
