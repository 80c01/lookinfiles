/* Unit tests for src/notebook_icons.c (notebook icon names). */
#include "notebook_icons.h"

static void test_presets_kept(void) {
    for (gsize i = 0; NOTEBOOK_ICONS[i] != NULL; i++) {
        g_assert_cmpstr(notebook_icon_normalize(NOTEBOOK_ICONS[i]), ==, NOTEBOOK_ICONS[i]);
    }
    g_assert_cmpstr(NOTEBOOK_ICONS[0], ==, NOTEBOOK_ICON_DEFAULT);
}

static void test_legacy_emoji(void) {
    g_assert_cmpstr(notebook_icon_normalize("📓"), ==, "notebook");
    g_assert_cmpstr(notebook_icon_normalize("💼"), ==, "folder-documents");
    g_assert_cmpstr(notebook_icon_normalize("☕"), ==, "emoji-food");
}

static void test_unknown(void) {
    g_assert_cmpstr(notebook_icon_normalize(NULL), ==, NOTEBOOK_ICON_DEFAULT);
    g_assert_cmpstr(notebook_icon_normalize(""), ==, NOTEBOOK_ICON_DEFAULT);
    g_assert_cmpstr(notebook_icon_normalize("🦊"), ==, NOTEBOOK_ICON_DEFAULT);
    g_assert_cmpstr(notebook_icon_normalize("../etc"), ==, NOTEBOOK_ICON_DEFAULT);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/notebook_icons/presets_kept", test_presets_kept);
    g_test_add_func("/notebook_icons/legacy_emoji", test_legacy_emoji);
    g_test_add_func("/notebook_icons/unknown", test_unknown);
    return g_test_run();
}
