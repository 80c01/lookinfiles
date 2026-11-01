#ifndef WEB_ADDRESS_H
#define WEB_ADDRESS_H

/* What is typed in the address bar of a browser tab (browser.h), turned
 * into the URI to load. Pure, tested in tests/test_web_address.c. */
#include <glib.h>

/* Search engine of the words typed in the address bar */
#define WEB_ADDRESS_SEARCH "https://duckduckgo.com/?q="

/* The URI for text (newly allocated), or NULL when text is empty:
 * - a URI with a scheme ("https://...", "about:blank", "file:///...") as is;
 * - a path ("/tmp/a.html", "~/doc.html") as a file URI;
 * - a host ("example.org", "example.org/a", "localhost:8080"): https, or http
 *   for localhost and IPv4 addresses;
 * - anything else (words, spaces): a search with WEB_ADDRESS_SEARCH. */
gchar* web_address_to_uri(const gchar* text);

/* TRUE for an http or https URI: the ones a browser tab opens */
gboolean web_address_is_web(const gchar* uri);

#endif /* WEB_ADDRESS_H */
