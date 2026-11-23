/* Unit tests for src/tasks/task_parse.c: messages in the output of build
 * and test commands. */
#include "tasks/task_parse.h"

#include <string.h>

/* The messages of output (lines split on '\n') run in dir */
static TaskParser* parse(const gchar* dir, const gchar* output) {
    TaskParser* p = task_parser_new(dir);
    gchar** lines = g_strsplit(output, "\n", -1);
    for (gchar** l = lines; *l != NULL; l++) task_parser_feed(p, *l);
    g_strfreev(lines);
    task_parser_finish(p);
    return p;
}

static const TaskMessage* msg(TaskParser* p, guint i) {
    const GPtrArray* msgs = task_parser_messages(p);
    g_assert_cmpuint(i, <, msgs->len);
    return g_ptr_array_index(msgs, i);
}

static guint n_msgs(TaskParser* p) {
    return task_parser_messages(p)->len;
}

static void test_gcc(void) {
    TaskParser* p = parse("/proj",
                          "cc -Wall -c src/main.c -o build/main.o\n"
                          "src/main.c: In function 'main':\n"
                          "src/main.c:12:5: error: 'x' undeclared (first use in this function)\n"
                          "   12 |     x = 1;\n"
                          "      |     ^\n"
                          "src/main.c:12:5: note: each undeclared identifier is reported only once\n"
                          "In file included from src/main.c:3:\n"
                          "                 from src/other.c:1:\n"
                          "src/util.h:7:13: warning: 'helper' defined but not used [-Wunused-function]\n"
                          "src/main.c:20: error: no column\n"
                          "make: *** [Makefile:12: build/main.o] Error 1\n");
    g_assert_cmpuint(n_msgs(p), ==, 4);
    g_assert_cmpint(msg(p, 0)->kind, ==, TASK_MSG_ERROR);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/proj/src/main.c");
    g_assert_cmpint(msg(p, 0)->line, ==, 12);
    g_assert_cmpint(msg(p, 0)->column, ==, 5);
    g_assert_cmpstr(msg(p, 0)->text, ==, "'x' undeclared (first use in this function)");
    g_assert_cmpuint(msg(p, 0)->output_line, ==, 2);
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_NOTE);
    g_assert_cmpint(msg(p, 2)->kind, ==, TASK_MSG_WARNING);
    g_assert_cmpstr(msg(p, 2)->file, ==, "/proj/src/util.h");
    g_assert_cmpint(msg(p, 3)->line, ==, 20);
    g_assert_cmpint(msg(p, 3)->column, ==, 0);
    g_assert_cmpuint(task_parser_count(p, TASK_MSG_ERROR), ==, 2);
    g_assert_cmpuint(task_parser_count(p, TASK_MSG_WARNING), ==, 1);
    g_assert_cmpuint(task_parser_count(p, TASK_MSG_NOTE), ==, 1);
    task_parser_free(p);
}

static void test_make_directories(void) {
    TaskParser* p = parse("/proj",
                          "make -C lib\n"
                          "make[1]: Entering directory '/proj/lib'\n"
                          "util.c:3:1: error: expected ';'\n"
                          "make[2]: Entering directory '/proj/lib/sub'\n"
                          "deep.c:1:1: warning: empty file\n"
                          "make[2]: Leaving directory '/proj/lib/sub'\n"
                          "make[1]: Leaving directory '/proj/lib'\n"
                          "main.c:4:2: error: back at the top\n"
                          "ninja: Entering directory `build'\n"
                          "../src/a.c:9:1: error: from ninja\n");
    g_assert_cmpuint(n_msgs(p), ==, 4);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/proj/lib/util.c");
    g_assert_cmpstr(msg(p, 1)->file, ==, "/proj/lib/sub/deep.c");
    g_assert_cmpstr(msg(p, 2)->file, ==, "/proj/main.c");
    g_assert_cmpstr(msg(p, 3)->file, ==, "/proj/src/a.c");
    task_parser_free(p);
}

static void test_colours_and_repeats(void) {
    TaskParser* p = parse("/proj",
                          "\x1b[01m\x1b[Ksrc/a.c:1:2:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kbad\n"
                          "src/a.c:1:2: error: bad\n"
                          "[ 10%] Building\r[ 20%] Building\rsrc/b.c:3:4: warning: last\r\n");
    g_assert_cmpuint(n_msgs(p), ==, 2);
    g_assert_cmpstr(msg(p, 0)->text, ==, "bad");
    g_assert_cmpstr(msg(p, 1)->file, ==, "/proj/src/b.c");
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_WARNING);
    gchar* clean = task_parse_clean_line("\x1b]8;;file:///x\x07link\x1b]8;;\x07 \x1b[1mbold\x1b[0m");
    g_assert_cmpstr(clean, ==, "link bold");
    g_free(clean);
    task_parser_free(p);
}

static void test_not_places(void) {
    TaskParser* p = parse("/proj",
                          "Started at 12:30:45 today\n"
                          "Note: 12: not a file\n"
                          "  VITE v5.0.0  ready in 300 ms\n"
                          "  ➜  Local:   http://localhost:5173/\n"
                          "Listening on localhost:8080\n"
                          "error: could not compile `demo` (bin \"demo\") due to 1 previous error\n"
                          "gcc: error: unrecognized command-line option '-Wfoo'\n");
    g_assert_cmpuint(n_msgs(p), ==, 0);
    task_parser_free(p);
}

static void test_linker(void) {
    TaskParser* p = parse("/proj",
                          "/usr/bin/ld: /tmp/ccX.o: in function `main':\n"
                          "/usr/bin/ld: main.c:5: undefined reference to `helper'\n"
                          "collect2: error: ld returned 1 exit status\n");
    g_assert_cmpuint(n_msgs(p), ==, 1);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/proj/main.c");
    g_assert_cmpint(msg(p, 0)->line, ==, 5);
    g_assert_cmpstr(msg(p, 0)->text, ==, "undefined reference to `helper'");
    task_parser_free(p);
}

static void test_rust(void) {
    TaskParser* p = parse("/proj",
                          "   Compiling demo v0.1.0 (/proj)\n"
                          "warning: unused variable: `y`\n"
                          " --> src/main.rs:3:9\n"
                          "  |\n"
                          "3 |     let y = 2;\n"
                          "  |         ^ help: if this is intentional, prefix it with an underscore: `_y`\n"
                          "  |\n"
                          "  = note: `#[warn(unused_variables)]` on by default\n"
                          "\n"
                          "error[E0308]: mismatched types\n"
                          " --> src/main.rs:4:18\n"
                          "  |\n"
                          "4 |     let x: i32 = \"a\";\n"
                          "\n"
                          "error: aborting due to 1 previous error\n"
                          "\n"
                          "thread 'main' panicked at src/lib.rs:10:5:\n"
                          "attempt to divide by zero\n"
                          "thread 'old' panicked at 'boom', src/old.rs:2:3\n");
    g_assert_cmpuint(n_msgs(p), ==, 4);
    g_assert_cmpint(msg(p, 0)->kind, ==, TASK_MSG_WARNING);
    g_assert_cmpstr(msg(p, 0)->text, ==, "unused variable: `y`");
    g_assert_cmpstr(msg(p, 0)->file, ==, "/proj/src/main.rs");
    g_assert_cmpint(msg(p, 0)->line, ==, 3);
    g_assert_cmpint(msg(p, 0)->column, ==, 9);
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_ERROR);
    g_assert_cmpstr(msg(p, 1)->text, ==, "mismatched types");
    g_assert_cmpstr(msg(p, 2)->file, ==, "/proj/src/lib.rs");
    g_assert_cmpstr(msg(p, 2)->text, ==, "attempt to divide by zero");
    g_assert_cmpstr(msg(p, 3)->text, ==, "boom");
    g_assert_cmpint(msg(p, 3)->line, ==, 2);
    task_parser_free(p);
}

static void test_tsc_and_msvc(void) {
    TaskParser* p = parse("/web",
                          "src/app.ts(12,5): error TS2322: Type 'string' is not assignable to type 'number'.\n"
                          "src/app.ts:14:1 - warning TS6133: 'x' is declared but its value is never read.\n"
                          "C:\\proj\\main.cpp(7,3): error C2065: 'y': undeclared identifier\n");
    g_assert_cmpuint(n_msgs(p), ==, 3);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/web/src/app.ts");
    g_assert_cmpint(msg(p, 0)->line, ==, 12);
    g_assert_cmpint(msg(p, 0)->column, ==, 5);
    g_assert_cmpstr(msg(p, 0)->text, ==, "TS2322: Type 'string' is not assignable to type 'number'.");
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_WARNING);
    g_assert_cmpint(msg(p, 2)->line, ==, 7);
    task_parser_free(p);
}

static void test_eslint(void) {
    TaskParser* p = parse("/web",
                          "\n"
                          "/web/src/index.js\n"
                          "   3:7   error    'unused' is assigned a value but never used  no-unused-vars\n"
                          "  10:1   warning  Unexpected console statement                no-console\n"
                          "\n"
                          "\xe2\x9c\x96 2 problems (1 error, 1 warning)\n");
    g_assert_cmpuint(n_msgs(p), ==, 2);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/web/src/index.js");
    g_assert_cmpint(msg(p, 0)->line, ==, 3);
    g_assert_cmpint(msg(p, 0)->column, ==, 7);
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_WARNING);
    g_assert_cmpstr(msg(p, 1)->text, ==, "Unexpected console statement                no-console");
    task_parser_free(p);
}

static void test_python(void) {
    TaskParser* p = parse("/py",
                          "Traceback (most recent call last):\n"
                          "  File \"/py/app/main.py\", line 8, in <module>\n"
                          "    run()\n"
                          "  File \"/py/app/main.py\", line 5, in run\n"
                          "    json.loads(x)\n"
                          "  File \"/usr/lib/python3.12/json/__init__.py\", line 346, in loads\n"
                          "    return _default_decoder.decode(s)\n"
                          "ValueError: bad input\n"
                          "tests/test_x.py:12: AssertionError\n"
                          "app/old.py:3: DeprecationWarning: use new()\n");
    g_assert_cmpuint(n_msgs(p), ==, 3);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/py/app/main.py");
    g_assert_cmpint(msg(p, 0)->line, ==, 5);
    g_assert_cmpstr(msg(p, 0)->text, ==, "ValueError: bad input");
    g_assert_cmpstr(msg(p, 1)->file, ==, "/py/tests/test_x.py");
    g_assert_cmpint(msg(p, 1)->kind, ==, TASK_MSG_ERROR);
    g_assert_cmpint(msg(p, 2)->kind, ==, TASK_MSG_WARNING);
    task_parser_free(p);
}

static void test_go(void) {
    TaskParser* p = parse("/go",
                          "# demo\n"
                          "./main.go:7:2: undefined: fmt.Printn\n"
                          "--- FAIL: TestAdd (0.00s)\n"
                          "    add_test.go:9: got 3, want 4\n");
    g_assert_cmpuint(n_msgs(p), ==, 2);
    g_assert_cmpstr(msg(p, 0)->file, ==, "/go/main.go");
    g_assert_cmpstr(msg(p, 0)->text, ==, "undefined: fmt.Printn");
    g_assert_cmpstr(msg(p, 1)->file, ==, "/go/add_test.go");
    g_assert_cmpint(msg(p, 1)->line, ==, 9);
    task_parser_free(p);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/task_parse/gcc", test_gcc);
    g_test_add_func("/task_parse/make_directories", test_make_directories);
    g_test_add_func("/task_parse/colours_and_repeats", test_colours_and_repeats);
    g_test_add_func("/task_parse/not_places", test_not_places);
    g_test_add_func("/task_parse/linker", test_linker);
    g_test_add_func("/task_parse/rust", test_rust);
    g_test_add_func("/task_parse/tsc_and_msvc", test_tsc_and_msvc);
    g_test_add_func("/task_parse/eslint", test_eslint);
    g_test_add_func("/task_parse/python", test_python);
    g_test_add_func("/task_parse/go", test_go);
    return g_test_run();
}
