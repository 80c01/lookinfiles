/* Unit tests for src/preview/web_keys.c: keys a native web view hands back. */
#include "preview/web_keys.h"

static void test_app_shortcuts(void) {
    g_assert_true(web_keys_for_app(GDK_KEY_q, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_w, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_l, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_Tab, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_Page_Down, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_B, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_equal, GDK_CONTROL_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_1, GDK_ALT_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_Left, GDK_ALT_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_Right, GDK_CONTROL_MASK | GDK_ALT_MASK));
    g_assert_true(web_keys_for_app(GDK_KEY_h, GDK_CONTROL_MASK | GDK_ALT_MASK));
}

static void test_function_keys(void) {
    g_assert_true(web_keys_for_app(GDK_KEY_F1, 0));
    g_assert_true(web_keys_for_app(GDK_KEY_F5, 0));
    g_assert_true(web_keys_for_app(GDK_KEY_F6, GDK_SHIFT_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_F3, 0));
    g_assert_false(web_keys_for_app(GDK_KEY_F7, 0));
    g_assert_false(web_keys_for_app(GDK_KEY_F12, 0));
}

static void test_page_keys(void) {
    g_assert_false(web_keys_for_app(GDK_KEY_c, GDK_CONTROL_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_V, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_a, GDK_CONTROL_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_f, GDK_CONTROL_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_Left, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_BackSpace, GDK_CONTROL_MASK));
    g_assert_false(web_keys_for_app(GDK_KEY_Escape, 0));
    g_assert_false(web_keys_for_app(GDK_KEY_Tab, 0));
    g_assert_false(web_keys_for_app(GDK_KEY_Left, 0));
    g_assert_false(web_keys_for_app(GDK_KEY_a, 0));
    g_assert_false(web_keys_for_app(0, GDK_CONTROL_MASK));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/web_keys/app_shortcuts", test_app_shortcuts);
    g_test_add_func("/web_keys/function_keys", test_function_keys);
    g_test_add_func("/web_keys/page_keys", test_page_keys);
    return g_test_run();
}
