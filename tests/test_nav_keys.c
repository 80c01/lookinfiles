/* Unit tests for src/nav_keys.c: navigation keys of each global mode. */
#include "nav_keys.h"

static NavAction look(NavStyle style, guint keyval, GdkModifierType state) {
    return nav_keys_lookup(style, NAV_PREFIX_NONE, keyval, state, NULL);
}

/* Arrows, Home / End and Page Up / Down work in every mode */
static void test_universal_keys(void) {
    for (gint s = NAV_STYLE_NOTEPAD; s <= NAV_STYLE_EMACS; s++) {
        g_assert_cmpint(look(s, GDK_KEY_Down, 0), ==, NAV_DOWN);
        g_assert_cmpint(look(s, GDK_KEY_Up, 0), ==, NAV_UP);
        g_assert_cmpint(look(s, GDK_KEY_Right, 0), ==, NAV_FORWARD);
        g_assert_cmpint(look(s, GDK_KEY_Left, 0), ==, NAV_BACKWARD);
        g_assert_cmpint(look(s, GDK_KEY_Home, 0), ==, NAV_FIRST);
        g_assert_cmpint(look(s, GDK_KEY_End, 0), ==, NAV_LAST);
        g_assert_cmpint(look(s, GDK_KEY_Page_Down, 0), ==, NAV_PAGE_DOWN);
        g_assert_cmpint(look(s, GDK_KEY_a, 0), ==, NAV_NONE);
        /* Alt+Left cycles the panels at the window level */
        g_assert_cmpint(look(s, GDK_KEY_Left, GDK_ALT_MASK), ==, NAV_NONE);
    }
}

/* Vim letter keys only in Vim mode (strict bindings) */
static void test_vim_letters_strict(void) {
    const guint letters[] = {GDK_KEY_j, GDK_KEY_k, GDK_KEY_h, GDK_KEY_l, GDK_KEY_g,
                             GDK_KEY_z, GDK_KEY_slash, GDK_KEY_n, GDK_KEY_N};
    for (gsize i = 0; i < G_N_ELEMENTS(letters); i++) {
        g_assert_cmpint(look(NAV_STYLE_VIM, letters[i], 0), !=, NAV_NONE);
        g_assert_cmpint(look(NAV_STYLE_NOTEPAD, letters[i], 0), ==, NAV_NONE);
        g_assert_cmpint(look(NAV_STYLE_EMACS, letters[i], 0), ==, NAV_NONE);
    }
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_G, GDK_SHIFT_MASK), ==, NAV_LAST);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_G, GDK_SHIFT_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_slash, 0), ==, NAV_SEARCH);
}

static void test_prefixes(void) {
    NavPrefix next;
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_NONE, GDK_KEY_g, 0, &next), ==, NAV_PENDING);
    g_assert_cmpint(next, ==, NAV_PREFIX_G);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_g, 0, &next), ==, NAV_FIRST);
    g_assert_cmpint(next, ==, NAV_PREFIX_NONE);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_Z, GDK_KEY_t, 0, &next), ==, NAV_TOP);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_NONE, GDK_KEY_t, 0, &next), ==, NAV_OPEN_DEDICATED);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_t, 0), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_t, 0), ==, NAV_NONE);
    /* A modifier pressed alone keeps the prefix */
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_EMACS, NAV_PREFIX_CTRL_X, GDK_KEY_Control_L,
                                    GDK_CONTROL_MASK, &next), ==, NAV_PENDING);
    g_assert_cmpint(next, ==, NAV_PREFIX_CTRL_X);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_NONE, GDK_KEY_Shift_L, 0, &next), ==, NAV_NONE);
}

static void test_vim(void) {
    NavPrefix next;
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_f, GDK_CONTROL_MASK), ==, NAV_PAGE_DOWN);
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_b, GDK_CONTROL_MASK), ==, NAV_PAGE_UP);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, NAV_PREFIX_NONE, GDK_KEY_w, GDK_CONTROL_MASK, &next),
                    ==, NAV_PENDING);
    g_assert_cmpint(next, ==, NAV_PREFIX_CTRL_W);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_w, 0, NULL), ==, NAV_PANEL_NEXT);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_w, GDK_CONTROL_MASK, NULL), ==, NAV_PANEL_NEXT);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_W, GDK_SHIFT_MASK, NULL), ==, NAV_PANEL_PREV);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_h, 0, NULL), ==, NAV_PANEL_PREV);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_VIM, next, GDK_KEY_x, 0, NULL), ==, NAV_CANCEL);
    /* Other modes' chords do nothing */
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_n, GDK_CONTROL_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_F6, 0), ==, NAV_NONE);
}

static void test_emacs(void) {
    NavPrefix next;
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_n, GDK_CONTROL_MASK), ==, NAV_DOWN);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_p, GDK_CONTROL_MASK), ==, NAV_UP);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_f, GDK_CONTROL_MASK), ==, NAV_FORWARD);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_b, GDK_CONTROL_MASK), ==, NAV_BACKWARD);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_v, GDK_CONTROL_MASK), ==, NAV_PAGE_DOWN);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_v, GDK_ALT_MASK), ==, NAV_PAGE_UP);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_less, GDK_ALT_MASK | GDK_SHIFT_MASK), ==, NAV_FIRST);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_greater, GDK_ALT_MASK | GDK_SHIFT_MASK), ==, NAV_LAST);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_l, GDK_CONTROL_MASK), ==, NAV_RECENTER);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_o, GDK_CONTROL_MASK), ==, NAV_DISPLAY_DEDICATED);
    g_assert_cmpint(look(NAV_STYLE_VIM, GDK_KEY_o, GDK_CONTROL_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_o, GDK_CONTROL_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_s, GDK_CONTROL_MASK), ==, NAV_SEARCH);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_r, GDK_CONTROL_MASK), ==, NAV_SEARCH_BACKWARD);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_g, GDK_CONTROL_MASK), ==, NAV_CANCEL);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_EMACS, NAV_PREFIX_NONE, GDK_KEY_x, GDK_CONTROL_MASK, &next),
                    ==, NAV_PENDING);
    g_assert_cmpint(next, ==, NAV_PREFIX_CTRL_X);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_EMACS, next, GDK_KEY_o, 0, NULL), ==, NAV_PANEL_NEXT);
    g_assert_cmpint(nav_keys_lookup(NAV_STYLE_EMACS, next, GDK_KEY_k, 0, NULL), ==, NAV_CANCEL);
    /* Ctrl+A / Ctrl+D / Ctrl+U keep their explorer roles */
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_a, GDK_CONTROL_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_EMACS, GDK_KEY_d, GDK_CONTROL_MASK), ==, NAV_NONE);
}

static void test_notepad(void) {
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_f, GDK_CONTROL_MASK), ==, NAV_SEARCH);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_b, GDK_CONTROL_MASK), ==, NAV_NONE);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_F3, 0), ==, NAV_SEARCH_NEXT);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_F3, GDK_SHIFT_MASK), ==, NAV_SEARCH_PREV);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_Home, GDK_CONTROL_MASK), ==, NAV_FIRST);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_End, GDK_CONTROL_MASK), ==, NAV_LAST);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_F6, 0), ==, NAV_PANEL_NEXT);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_F6, GDK_SHIFT_MASK), ==, NAV_PANEL_PREV);
    g_assert_cmpint(look(NAV_STYLE_NOTEPAD, GDK_KEY_w, GDK_CONTROL_MASK), ==, NAV_NONE);
}

static void test_search_keys(void) {
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_VIM, GDK_KEY_Return, 0), ==, NAV_SEARCH_KEY_ACCEPT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_VIM, GDK_KEY_Escape, 0), ==, NAV_SEARCH_KEY_ACCEPT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_VIM, GDK_KEY_s, GDK_CONTROL_MASK), ==, NAV_SEARCH_KEY_NONE);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_EMACS, GDK_KEY_s, GDK_CONTROL_MASK), ==, NAV_SEARCH_KEY_NEXT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_EMACS, GDK_KEY_r, GDK_CONTROL_MASK), ==, NAV_SEARCH_KEY_PREV);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_EMACS, GDK_KEY_g, GDK_CONTROL_MASK), ==, NAV_SEARCH_KEY_CANCEL);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_EMACS, GDK_KEY_Return, 0), ==, NAV_SEARCH_KEY_ACCEPT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_NOTEPAD, GDK_KEY_Return, 0), ==, NAV_SEARCH_KEY_NEXT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_NOTEPAD, GDK_KEY_Return, GDK_SHIFT_MASK), ==, NAV_SEARCH_KEY_PREV);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_NOTEPAD, GDK_KEY_F3, 0), ==, NAV_SEARCH_KEY_NEXT);
    g_assert_cmpint(nav_keys_search_lookup(NAV_STYLE_NOTEPAD, GDK_KEY_a, 0), ==, NAV_SEARCH_KEY_NONE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nav_keys/universal", test_universal_keys);
    g_test_add_func("/nav_keys/vim_letters_strict", test_vim_letters_strict);
    g_test_add_func("/nav_keys/prefixes", test_prefixes);
    g_test_add_func("/nav_keys/vim", test_vim);
    g_test_add_func("/nav_keys/emacs", test_emacs);
    g_test_add_func("/nav_keys/notepad", test_notepad);
    g_test_add_func("/nav_keys/search", test_search_keys);
    return g_test_run();
}
