/* Unit tests for src/web_address.c: address bar text to URI. */
#include "web_address.h"

static void check(const gchar* text, const gchar* expected) {
    gchar* uri = web_address_to_uri(text);
    g_assert_cmpstr(uri, ==, expected);
    g_free(uri);
}

static void test_uris(void) {
    check("https://example.org/a?b=1", "https://example.org/a?b=1");
    check("  http://example.org  ", "http://example.org");
    check("about:blank", "about:blank");
    check("file:///tmp/a.html", "file:///tmp/a.html");
    check("ftp://host/file", "ftp://host/file");
}

static void test_hosts(void) {
    check("example.org", "https://example.org");
    check("example.org/docs/a.html", "https://example.org/docs/a.html");
    check("sub-domain.example.co.uk:8443/x", "https://sub-domain.example.co.uk:8443/x");
    check("localhost:8080", "http://localhost:8080");
    check("localhost", "http://localhost");
    check("127.0.0.1:3000/app", "http://127.0.0.1:3000/app");
}

static void test_paths(void) {
    check("/tmp/a b.html", "file:///tmp/a%20b.html");
    gchar* home = g_build_filename(g_get_home_dir(), "doc.html", NULL);
    gchar* expected = g_filename_to_uri(home, NULL, NULL);
    check("~/doc.html", expected);
    g_free(expected);
    g_free(home);
}

static void test_search(void) {
    check("gtk webkit", WEB_ADDRESS_SEARCH "gtk%20webkit");
    check("word", WEB_ADDRESS_SEARCH "word");
    check("a.", WEB_ADDRESS_SEARCH "a.");
    check("c++ & rust", WEB_ADDRESS_SEARCH "c%2B%2B%20%26%20rust");
    check("localhost:8080 x", WEB_ADDRESS_SEARCH "localhost%3A8080%20x");
}

static void test_empty(void) {
    check(NULL, NULL);
    check("", NULL);
    check("   ", NULL);
}

static void test_is_web(void) {
    g_assert_true(web_address_is_web("https://example.org"));
    g_assert_true(web_address_is_web("HTTP://example.org"));
    g_assert_false(web_address_is_web("file:///tmp"));
    g_assert_false(web_address_is_web("mailto:a@b.c"));
    g_assert_false(web_address_is_web(NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/web_address/uris", test_uris);
    g_test_add_func("/web_address/hosts", test_hosts);
    g_test_add_func("/web_address/paths", test_paths);
    g_test_add_func("/web_address/search", test_search);
    g_test_add_func("/web_address/empty", test_empty);
    g_test_add_func("/web_address/is_web", test_is_web);
    return g_test_run();
}
