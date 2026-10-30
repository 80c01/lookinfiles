/* Unit tests for src/code_symbol.c, src/code_scan.c and src/code_scan_c.c:
 * languages, the identifier under the cursor, and the symbol scanners of
 * the built-in code index. */
#include "code_scan.h"

#include <string.h>

/* Symbols as "kind:name@line" (1-based line, "!" for a declaration),
 * separated by spaces */
static gchar* scan(const gchar* text, CodeLang lang, const gchar* only) {
    GPtrArray* syms = code_scan_text(text, strlen(text), lang, only);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < syms->len; i++) {
        CodeSymbol* sym = g_ptr_array_index(syms, i);
        if (s->len > 0) g_string_append_c(s, ' ');
        g_string_append_printf(s, "%s:%s@%d%s", code_symbol_kind_name(sym->kind), sym->name,
                               sym->line + 1, sym->declaration ? "!" : "");
    }
    g_ptr_array_unref(syms);
    return g_string_free(s, FALSE);
}

#define ASSERT_SCAN(text, lang, expected)          \
    do {                                           \
        gchar* got_ = scan((text), (lang), NULL);  \
        g_assert_cmpstr(got_, ==, (expected));     \
        g_free(got_);                              \
    } while (0)

static void test_lang(void) {
    g_assert_cmpint(code_lang_from_name("src/main.c"), ==, CODE_LANG_C);
    g_assert_cmpint(code_lang_from_name("a.h"), ==, CODE_LANG_C);
    g_assert_cmpint(code_lang_from_name("a.hpp"), ==, CODE_LANG_CPP);
    g_assert_cmpint(code_lang_from_name("A.C"), ==, CODE_LANG_CPP);
    g_assert_cmpint(code_lang_from_name("x.TS"), ==, CODE_LANG_JS);
    g_assert_cmpint(code_lang_from_name("/p/Makefile"), ==, CODE_LANG_MAKE);
    g_assert_cmpint(code_lang_from_name("rules.mk"), ==, CODE_LANG_MAKE);
    g_assert_cmpint(code_lang_from_name("notes.txt"), ==, CODE_LANG_NONE);
    g_assert_cmpint(code_lang_from_name(".hidden"), ==, CODE_LANG_NONE);
    g_assert_cmpint(code_lang_family(CODE_LANG_CPP), ==, CODE_LANG_C);
    g_assert_cmpint(code_lang_family(CODE_LANG_JAVA), ==, CODE_LANG_JAVA);
    const gchar* script = "#!/usr/bin/env python3\nprint(1)\n";
    g_assert_cmpint(code_lang_detect("tool", script, -1), ==, CODE_LANG_PYTHON);
    g_assert_cmpint(code_lang_detect("run", "#!/bin/bash -e\n", -1), ==, CODE_LANG_SHELL);
    g_assert_cmpint(code_lang_detect("run", "plain text", -1), ==, CODE_LANG_NONE);
}

static void test_word_at(void) {
    const gchar* line = "  app->viewed_kind = file_kind(x);";
    gint s = 0, e = 0;
    g_assert_true(code_word_at(line, -1, 8, CODE_LANG_C, &s, &e));
    g_assert_cmpint(s, ==, 7);
    g_assert_cmpint(e, ==, 18);
    /* Right after a word */
    g_assert_true(code_word_at(line, -1, 5, CODE_LANG_C, &s, &e));
    g_assert_cmpint(s, ==, 2);
    g_assert_cmpint(e, ==, 5);
    /* Between two non-word characters */
    g_assert_false(code_word_at(line, -1, 1, CODE_LANG_C, &s, &e));
    /* Numbers are not identifiers */
    g_assert_false(code_word_at("x = 42;", -1, 5, CODE_LANG_C, &s, &e));
    /* Language word characters */
    g_assert_true(code_word_at("$(BUILD_DIR)/ui-test", -1, 16, CODE_LANG_MAKE, &s, &e));
    g_assert_cmpint(s, ==, 13);
    g_assert_cmpint(e, ==, 20);
    g_assert_true(code_word_at("const $el = 1", -1, 7, CODE_LANG_JS, &s, &e));
    g_assert_cmpint(s, ==, 6);
    g_assert_true(code_word_at("list.empty? ", -1, 6, CODE_LANG_RUBY, &s, &e));
    g_assert_cmpint(e, ==, 11);
    /* UTF-8 identifiers */
    g_assert_true(code_word_at("été = 1", -1, 0, CODE_LANG_PYTHON, &s, &e));
    g_assert_cmpint(e, ==, 5);
}

static void test_has_word(void) {
    const gchar* t = "int foobar; int foo_x; call(foo);";
    g_assert_true(code_text_has_word(t, strlen(t), "foo", CODE_LANG_C));
    g_assert_false(code_text_has_word(t, strlen(t), "oba", CODE_LANG_C));
    g_assert_false(code_text_has_word("foobar", 6, "foo", CODE_LANG_C));
    g_assert_true(code_text_has_word("foo", 3, "foo", CODE_LANG_C));
}

static void test_c_functions(void) {
    ASSERT_SCAN("static int add(int a, int b) {\n    return a + b;\n}\n"
                "void run(void);\n"
                "gboolean\n"
                "gnu_style (const char *s)\n"
                "{\n  if (s) { return TRUE; }\n  return FALSE;\n}\n",
                CODE_LANG_C, "function:add@1 function:run@4! function:gnu_style@6");
    /* Attributes and macros after the parameters, macro calls before */
    ASSERT_SCAN("void log_msg(const char* fmt, ...) G_GNUC_PRINTF(1, 2);\n"
                "G_DEFINE_TYPE (Foo, foo, G_TYPE_OBJECT)\n"
                "static void foo_init (Foo *self) { }\n"
                "static gchar* name_of(int x) __attribute__((unused));\n",
                CODE_LANG_C, "function:log_msg@1! function:foo_init@3 function:name_of@4!");
    /* Function pointers */
    ASSERT_SCAN("static void (*handler)(int sig);\n"
                "typedef gboolean (*FilterFunc)(gpointer item);\n",
                CODE_LANG_C, "variable:handler@1 type:FilterFunc@2");
}

static void test_c_types(void) {
    ASSERT_SCAN("typedef struct {\n    gchar* path;\n    gint line, column;\n    void (*free_fn)(gpointer);\n"
                "    unsigned flags : 3;\n} JumpItem;\n",
                CODE_LANG_C,
                "field:path@2 field:line@3 field:column@3 field:free_fn@4 field:flags@5 type:JumpItem@6");
    ASSERT_SCAN("struct node {\n  struct node* next;\n  union { int i; float f; };\n};\n"
                "struct node;\nstruct node* make_node(void);\n",
                CODE_LANG_C, "type:node@1 field:next@2 field:i@3 field:f@3 type:node@5! function:make_node@6!");
    ASSERT_SCAN("typedef enum {\n    MODE_A = 0,\n    MODE_B, /* comment */\n    MODE_C\n} Mode;\n"
                "enum color { RED = 1 << 0, GREEN };\n",
                CODE_LANG_C, "constant:MODE_A@2 constant:MODE_B@3 constant:MODE_C@4 type:Mode@5 type:color@6 "
                             "constant:RED@6 constant:GREEN@6");
    ASSERT_SCAN("typedef struct _Foo Foo;\ntypedef unsigned long Size, *SizePtr;\n",
                CODE_LANG_C, "type:Foo@1 type:Size@2 type:SizePtr@2");
}

static void test_c_variables_and_macros(void) {
    ASSERT_SCAN("#include <stdio.h>\n#define MAX_LEN 64\n#  define SQUARE(x) ((x) * (x))\n"
                "static const char* const NAMES[] = {\n    \"a\", \"b\",\n};\n"
                "extern int verbose;\nint count = 0, *ptr, table[4];\n",
                CODE_LANG_C, "macro:MAX_LEN@2 macro:SQUARE@3 variable:NAMES@4 variable:verbose@7! "
                             "variable:count@8 variable:ptr@8 variable:table@8");
    /* Multi-line macro, strings and comments with braces */
    ASSERT_SCAN("#define BLOCK(x) do { \\\n  foo(x); \\\n} while (0)\n"
                "const char* s = \"{ not a block\";\n/* } */ // {\nint after;\n",
                CODE_LANG_C, "macro:BLOCK@1 variable:s@4 variable:after@6");
}

static void test_c_preprocessor_branches(void) {
    /* Only the first branch is read: braces stay balanced */
    ASSERT_SCAN("#ifdef A\nvoid f(int a) {\n#else\nvoid f(void) {\n#endif\n}\nint g(void) { return 1; }\n",
                CODE_LANG_C, "function:f@2 function:g@7");
    ASSERT_SCAN("#ifdef __cplusplus\nextern \"C\" {\n#endif\nvoid api(void);\n#ifdef __cplusplus\n}\n#endif\n",
                CODE_LANG_C, "function:api@4!");
    ASSERT_SCAN("G_BEGIN_DECLS\ntypedef struct _Bar Bar;\nG_END_DECLS\n", CODE_LANG_C, "type:Bar@2");
}

static void test_c_locals_skipped(void) {
    ASSERT_SCAN("int f(void) {\n  int local = 1;\n  struct { int x; } s;\n  return local;\n}\nint g;\n",
                CODE_LANG_C, "function:f@1 variable:g@6");
}

static void test_cpp(void) {
    ASSERT_SCAN("namespace app {\nclass Widget : public Base {\npublic:\n  Widget(int w);\n"
                "  int width() const { return w_; }\nprivate:\n  int w_ = 0;\n  std::map<int, int> cache_;\n};\n"
                "Widget::Widget(int w) : Base(w), w_(w) {}\n"
                "template <typename T> T max_of(T a, T b) { return a > b ? a : b; }\n"
                "enum class Color : int { Red, Green };\nusing Id = unsigned;\n}\n",
                CODE_LANG_CPP,
                "module:app@1 class:Widget@2 method:Widget@4! method:width@5 field:w_@7 field:cache_@8 "
                "method:Widget@10 function:max_of@11 type:Color@12 constant:Red@12 constant:Green@12 "
                "type:Id@13");
    ASSERT_SCAN("int n = 1'000'000;\nconst char* raw = R\"x(a { b)x\";\nint after;\n", CODE_LANG_CPP,
                "variable:n@1 variable:raw@2 variable:after@3");
}

static void test_java_csharp(void) {
    ASSERT_SCAN("package a.b;\nimport java.util.*;\n@Entity\npublic class Shop<T> extends Base implements Api {\n"
                "  private final Map<String, Integer> stock = new HashMap<>();\n"
                "  @Override\n  public String name() { return \"x\"; }\n"
                "  enum Size { SMALL(1), LARGE(2); int v; Size(int v) { this.v = v; } }\n"
                "  interface Listener { void changed(int n); }\n}\n",
                CODE_LANG_JAVA,
                "class:Shop@4 field:stock@5 method:name@7 type:Size@8 constant:SMALL@8 constant:LARGE@8 "
                "field:v@8 method:Size@8 class:Listener@9 method:changed@9!");
    ASSERT_SCAN("using System;\nnamespace App.Core;\n[Serializable]\npublic partial class Cart {\n"
                "  public int Count { get; set; }\n  public void Add(Item i) { }\n}\n",
                CODE_LANG_CSHARP, "module:Core@2 class:Cart@4 field:Count@5 method:Add@6");
}

static void test_python(void) {
    ASSERT_SCAN("import os\nMAX = 3\nclass Shop:\n    \"\"\"Docs.\n    def fake(): pass\n    \"\"\"\n"
                "    def __init__(self):\n        self.items = []\n\n    async def load(self): pass\n"
                "def main(argv: list) -> int:\n    total: int = 0\n    return total\n",
                CODE_LANG_PYTHON,
                "variable:MAX@2 class:Shop@3 method:__init__@7 field:items@8 method:load@10 function:main@11");
}

static void test_shell(void) {
    ASSERT_SCAN("#!/bin/sh\n# helper() {\nhelper() {\n  local dir=\"$1\"\n}\nfunction clean_up {\n"
                "  :\n}\nexport PATH=\"$HOME/bin:$PATH\"\nCOUNT+=1\nif [ \"$a\" = b ]; then :; fi\n",
                CODE_LANG_SHELL,
                "function:helper@3 variable:dir@4 function:clean_up@6 variable:PATH@9 variable:COUNT@10");
}

static void test_js(void) {
    ASSERT_SCAN("// function fake() {}\n/* class Fake {}\n*/\nexport async function load(url) {\n"
                "  const res = await fetch(url);\n}\nexport default class Store {\n"
                "  static create(opts) {\n    if (opts) { }\n  }\n}\n"
                "export interface Item { id: number }\ntype Id = string;\nlet count = 0;\n",
                CODE_LANG_JS,
                "function:load@4 variable:res@5 class:Store@7 method:create@8 type:Item@12 type:Id@13 "
                "variable:count@14");
}

static void test_go(void) {
    ASSERT_SCAN("package main\n\nconst (\n\tA = iota\n\tB\n)\n\ntype Point struct {\n\tX, Y int\n\tlabel string\n}\n"
                "type Shape interface {\n\tArea() float64\n}\n"
                "func (p *Point) Move(dx int) {}\nfunc main() {\n\tx := 1\n}\nvar debug = false\n",
                CODE_LANG_GO,
                "constant:A@4 constant:B@5 type:Point@8 field:X@9 field:label@10 type:Shape@12 "
                "method:Area@13! method:Move@15 function:main@16 variable:debug@19");
}

static void test_rust(void) {
    ASSERT_SCAN("pub struct Config {\n    pub name: String,\n    retries: u32,\n}\nenum State {\n    Idle,\n"
                "    Busy(u32),\n}\nimpl Config {\n    pub fn new() -> Self { todo!() }\n}\n"
                "pub(crate) async fn run() {}\nconst MAX: usize = 4;\nmacro_rules! log { () => {} }\nmod net;\n",
                CODE_LANG_RUST,
                "type:Config@1 field:name@2 field:retries@3 type:State@5 constant:Idle@6 constant:Busy@7 "
                "method:new@10 function:run@12 constant:MAX@13 macro:log@14 module:net@15");
}

static void test_lua_ruby_make(void) {
    ASSERT_SCAN("local M = {}\nfunction M.start(x)\nend\nlocal function helper() end\nM.stop = function() end\n"
                "--[[ function fake() end ]]\nVERSION = \"1\"\n",
                CODE_LANG_LUA,
                "variable:M@1 function:start@2 function:helper@4 function:stop@5 variable:VERSION@7");
    ASSERT_SCAN("module Shop\n  class Cart < Base\n    attr_reader :items\n    LIMIT = 10\n"
                "    def empty?\n    end\n    def self.build; end\n  end\nend\n",
                CODE_LANG_RUBY,
                "module:Shop@1 class:Cart@2 field:items@3 constant:LIMIT@4 method:empty?@5 method:build@7");
    ASSERT_SCAN("CC = gcc\nBUILD_DIR := build\n.PHONY: all test\nall test: $(TARGET)\n\t@echo done\n"
                "$(BUILD_DIR)/%.o: src/%.c\nui-test: all\ndefine HELP\nendef\n",
                CODE_LANG_MAKE,
                "variable:CC@1 variable:BUILD_DIR@2 target:all@4 target:test@4 target:ui-test@7 macro:HELP@8");
}

static void test_only_name(void) {
    gchar* got = scan("int foo;\nint bar(void) { return foo; }\nvoid foo_x(void);\n", CODE_LANG_C, "foo");
    g_assert_cmpstr(got, ==, "variable:foo@1");
    g_free(got);
    got = scan("def a(): pass\n", CODE_LANG_PYTHON, "zzz");
    g_assert_cmpstr(got, ==, "");
    g_free(got);
}

static void test_symbol_text(void) {
    const gchar* src = "\tstatic   int add(int a);\r\n";
    GPtrArray* syms = code_scan_text(src, strlen(src), CODE_LANG_C, NULL);
    g_assert_cmpuint(syms->len, ==, 1);
    CodeSymbol* sym = g_ptr_array_index(syms, 0);
    g_assert_cmpstr(sym->text, ==, "static   int add(int a);");
    g_assert_cmpint(sym->column, ==, 14);
    g_ptr_array_unref(syms);
}

static void test_local_definition(void) {
    const gchar* src = "int total;\n"
                       "int f(int n)\n"
                       "{\n"
                       "    int sum = 0;\n"
                       "    for (int i = 0; i < n; i++) sum += i;\n"
                       "    return sum;\n"
                       "}\n";
    gint line = -1, col = -1;
    /* Cursor on "sum" in the return line: its first use in the function */
    g_assert_true(code_scan_local_definition(src, strlen(src), CODE_LANG_C, "sum", 5, 11, &line, &col));
    g_assert_cmpint(line, ==, 3);
    g_assert_cmpint(col, ==, 8);
    /* Parameter: on the function head line */
    g_assert_true(code_scan_local_definition(src, strlen(src), CODE_LANG_C, "n", 4, 24, &line, &col));
    g_assert_cmpint(line, ==, 1);
    /* Only occurrence: at the cursor itself */
    g_assert_false(code_scan_local_definition(src, strlen(src), CODE_LANG_C, "total", 0, 4, &line, &col));
    /* Not in the scope: first occurrence of the file */
    const gchar* two = "x = 1\ndef g():\n    return x\n";
    g_assert_true(code_scan_local_definition(two, strlen(two), CODE_LANG_PYTHON, "x", 2, 11, &line, &col));
    g_assert_cmpint(line, ==, 0);
    g_assert_cmpint(col, ==, 0);
}

static void test_robustness(void) {
    /* Unbalanced and truncated input must not crash */
    const gchar* bad[] = {
        "}}}} int x;", "struct {", "void f(", "\"unterminated\nint y;", "/* open comment", "#define",
        "enum { A, B", "typedef", "R\"(", "'", "class A : public", "", "\n\n\n",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(bad); i++) {
        for (CodeLang l = CODE_LANG_C; l <= CODE_LANG_MAKE; l++) {
            GPtrArray* syms = code_scan_text(bad[i], strlen(bad[i]), l, NULL);
            g_ptr_array_unref(syms);
        }
    }
    gchar* got = scan("}}}} int x;", CODE_LANG_C, NULL);
    g_assert_cmpstr(got, ==, "variable:x@1");
    g_free(got);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_scan/lang", test_lang);
    g_test_add_func("/code_scan/word_at", test_word_at);
    g_test_add_func("/code_scan/has_word", test_has_word);
    g_test_add_func("/code_scan/c_functions", test_c_functions);
    g_test_add_func("/code_scan/c_types", test_c_types);
    g_test_add_func("/code_scan/c_variables_and_macros", test_c_variables_and_macros);
    g_test_add_func("/code_scan/c_preprocessor_branches", test_c_preprocessor_branches);
    g_test_add_func("/code_scan/c_locals_skipped", test_c_locals_skipped);
    g_test_add_func("/code_scan/cpp", test_cpp);
    g_test_add_func("/code_scan/java_csharp", test_java_csharp);
    g_test_add_func("/code_scan/python", test_python);
    g_test_add_func("/code_scan/shell", test_shell);
    g_test_add_func("/code_scan/js", test_js);
    g_test_add_func("/code_scan/go", test_go);
    g_test_add_func("/code_scan/rust", test_rust);
    g_test_add_func("/code_scan/lua_ruby_make", test_lua_ruby_make);
    g_test_add_func("/code_scan/only_name", test_only_name);
    g_test_add_func("/code_scan/symbol_text", test_symbol_text);
    g_test_add_func("/code_scan/local_definition", test_local_definition);
    g_test_add_func("/code_scan/robustness", test_robustness);
    return g_test_run();
}
