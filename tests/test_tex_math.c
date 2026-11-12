/* Unit tests for src/markup/tex_math.c: TeX math parsed into boxes */
#include <string.h>
#include "markup/tex_math.h"

/* tex parsed, as linear text */
static void check_text(const gchar* tex, const gchar* expected) {
    TexNode* n = tex_math_parse(tex);
    gchar* got = tex_math_to_text(n);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
    tex_node_free(n);
}

static void test_symbols(void) {
    check_text("\\alpha + \\beta = \\Gamma", "α+β=Γ");
    check_text("a \\leq b \\to \\infty", "a≤b→∞");
    check_text("x - y * z", "x−y∗z");
    check_text("\\mathbb{R}^n \\times \\mathcal{L} \\mathfrak{g}", "ℝ^n×ℒ𝔤");
    check_text("\\mathbb{Q} \\mathbb{x}", "ℚ𝕩");
    check_text("\\not= \\not\\in \\neq", "≠∉≠");
    check_text("\\{ x \\mid x > 0 \\}", "{x∣x>0}");
    check_text("\\% \\$ \\#", "%$#");
    check_text("\\foo", "\\foo"); /* unknown: kept */
}

static void test_structures(void) {
    check_text("\\frac{a+b}{c}", "(a+b)/c");
    check_text("\\frac12", "1/2");
    check_text("\\sqrt{x+1}", "√(x+1)");
    check_text("\\sqrt[3]{x}", "[3]√x");
    check_text("x_i^2", "x_i^2");
    check_text("x^{n+1}", "x^(n+1)");
    check_text("f'(x)", "f′(x)");
    check_text("e^{i\\pi} + 1 = 0", "e^(iπ)+1=0");
    check_text("\\binom{n}{k}", "(n k)");
    check_text("\\left( \\frac{1}{2} \\right]", "(1/2]");
    check_text("\\left. x \\right|", "x|");
    check_text("\\hat{x} \\bar{y}", "x\xCC\x82y\xCC\x85");
    check_text("\\text{if } x", "if x");
    check_text("\\sin x \\quad \\lim_{n \\to \\infty}", "sinx lim_(n→∞)");
}

static void test_tables(void) {
    check_text("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", "(a, b; c, d)");
    check_text("\\begin{cases} 1 & x > 0 \\\\ 0 & \\text{else} \\end{cases}", "{1, x>0; 0, else");
    check_text("\\begin{aligned} a &= b \\\\ &= c \\end{aligned}", "a=b; =c");
    check_text("\\begin{equation} E = mc^2 \\end{equation}", "E=mc^2");
    /* & and \\ outside an environment: aligned rows */
    TexNode* n = tex_math_parse("a &= b \\\\ c &= d");
    g_assert_cmpint(n->kind, ==, TEX_TABLE);
    g_assert_cmpint(n->style, ==, TEX_TABLE_ALIGNED);
    g_assert_cmpuint(n->items->len, ==, 2);
    tex_node_free(n);
}

static void test_tree(void) {
    TexNode* n = tex_math_parse("\\sum_{i=1}^n i");
    g_assert_cmpint(n->kind, ==, TEX_ROW);
    TexNode* sum = g_ptr_array_index(n->items, 0);
    g_assert_cmpint(sum->kind, ==, TEX_SCRIPTS);
    g_assert_cmpint(sum->limits, ==, TEX_LIMITS_DISPLAY);
    g_assert_cmpint(sum->body->big, ==, 1);
    g_assert_cmpint(tex_node_class(sum), ==, TEX_OP);
    tex_node_free(n);

    n = tex_math_parse("\\int\\limits_0^1");
    g_assert_cmpint(n->kind, ==, TEX_SCRIPTS);
    g_assert_cmpint(n->limits, ==, TEX_LIMITS_ALWAYS);
    tex_node_free(n);

    /* Letters italic, digits and functions upright */
    n = tex_math_parse("x2\\sin");
    g_assert_cmpuint(((TexNode*)g_ptr_array_index(n->items, 0))->font, ==, TEX_FONT_ITALIC);
    g_assert_cmpuint(((TexNode*)g_ptr_array_index(n->items, 1))->font, ==, 0);
    g_assert_cmpint(((TexNode*)g_ptr_array_index(n->items, 2))->cls, ==, TEX_OP);
    tex_node_free(n);

    n = tex_math_parse("\\mathbf{v}");
    g_assert_cmpuint(n->kind == TEX_ROW ? ((TexNode*)g_ptr_array_index(n->items, 0))->font : n->font, ==,
                     TEX_FONT_BOLD);
    tex_node_free(n);

    n = tex_math_parse("\\bigl( x \\bigr)");
    TexNode* open = g_ptr_array_index(n->items, 0);
    g_assert_true(open->delim);
    g_assert_cmpint(open->big, ==, 1);
    g_assert_cmpint(open->cls, ==, TEX_OPEN);
    tex_node_free(n);
}

static void test_broken(void) {
    /* Never NULL, never stuck */
    const gchar* const BROKEN[] = {
        "", "{", "}", "\\frac", "\\frac{a", "x^", "_", "\\left(", "\\right)", "\\begin{matrix} a &",
        "\\end{matrix}", "\\sqrt[", "\\\\", "&&", "\\", "{{{{", "\\not", "a'''", "\xff\xfe",
        "\\text{", "\\operatorname", "x^^2", "\\big",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(BROKEN); i++) {
        TexNode* n = tex_math_parse(BROKEN[i]);
        g_assert_nonnull(n);
        g_free(tex_math_to_text(n));
        tex_node_free(n);
    }
    /* Deep nesting stops */
    GString* deep = g_string_new(NULL);
    for (gint i = 0; i < 2000; i++) g_string_append(deep, "{x^");
    TexNode* n = tex_math_parse(deep->str);
    g_assert_nonnull(n);
    tex_node_free(n);
    g_string_free(deep, TRUE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tex_math/symbols", test_symbols);
    g_test_add_func("/tex_math/structures", test_structures);
    g_test_add_func("/tex_math/tables", test_tables);
    g_test_add_func("/tex_math/tree", test_tree);
    g_test_add_func("/tex_math/broken", test_broken);
    return g_test_run();
}
