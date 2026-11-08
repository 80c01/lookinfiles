/* Address bar text to URI (see web_address.h) */
#include "preview/web_address.h"

#include <string.h>

/* "name:" schemes without "//" kept as typed */
static const gchar* const PLAIN_SCHEMES[] = {"about", "data", "file", "mailto", NULL};

/* A scheme followed by "://", or one of PLAIN_SCHEMES */
static gboolean has_scheme(const gchar* text) {
    const gchar* colon = strchr(text, ':');
    if (colon == NULL || colon == text) return FALSE;
    for (const gchar* p = text; p < colon; p++) {
        if (!g_ascii_isalnum(*p) && *p != '+' && *p != '-' && *p != '.') return FALSE;
    }
    if (g_str_has_prefix(colon, "://")) return TRUE;
    for (gsize i = 0; PLAIN_SCHEMES[i] != NULL; i++) {
        gsize n = strlen(PLAIN_SCHEMES[i]);
        if ((gsize)(colon - text) == n && g_ascii_strncasecmp(text, PLAIN_SCHEMES[i], n) == 0) return TRUE;
    }
    return FALSE;
}

/* The host part of "host[:port][/path]" */
static gchar* host_of(const gchar* text) {
    gsize n = strcspn(text, ":/?#");
    return g_strndup(text, n);
}

static gboolean is_ipv4(const gchar* host) {
    gint dots = 0;
    gint digits = 0;
    for (const gchar* p = host; *p != '\0'; p++) {
        if (*p == '.') {
            if (digits == 0) return FALSE;
            dots++;
            digits = 0;
        } else if (g_ascii_isdigit(*p)) {
            if (++digits > 3) return FALSE;
        } else {
            return FALSE;
        }
    }
    return dots == 3 && digits > 0;
}

/* A host name: letters, digits, '-' and '.', with a dot (or localhost) */
static gboolean looks_like_host(const gchar* host) {
    if (host[0] == '\0') return FALSE;
    if (g_ascii_strcasecmp(host, "localhost") == 0) return TRUE;
    if (host[0] == '.' || host[strlen(host) - 1] == '.') return FALSE;
    gboolean dot = FALSE;
    for (const gchar* p = host; *p != '\0'; p++) {
        if (*p == '.') {
            dot = TRUE;
        } else if (!g_ascii_isalnum(*p) && *p != '-') {
            return FALSE;
        }
    }
    return dot;
}

gchar* web_address_to_uri(const gchar* text) {
    if (text == NULL) return NULL;
    gchar* t = g_strstrip(g_strdup(text));
    gchar* uri = NULL;
    if (t[0] == '\0') {
        uri = NULL;
    } else if (has_scheme(t)) {
        uri = g_strdup(t);
    } else if (t[0] == '/' || g_str_has_prefix(t, "~/") || strcmp(t, "~") == 0) {
        gchar* path = t[0] == '~' ? g_build_filename(g_get_home_dir(), t + 1, NULL) : g_strdup(t);
        uri = g_filename_to_uri(path, NULL, NULL);
        g_free(path);
    } else if (strpbrk(t, " \t") == NULL) {
        gchar* host = host_of(t);
        if (g_ascii_strcasecmp(host, "localhost") == 0 || is_ipv4(host)) {
            uri = g_strconcat("http://", t, NULL);
        } else if (looks_like_host(host)) {
            uri = g_strconcat("https://", t, NULL);
        }
        g_free(host);
    }
    if (uri == NULL && t[0] != '\0') {
        gchar* q = g_uri_escape_string(t, NULL, FALSE);
        uri = g_strconcat(WEB_ADDRESS_SEARCH, q, NULL);
        g_free(q);
    }
    g_free(t);
    return uri;
}

gboolean web_address_is_web(const gchar* uri) {
    if (uri == NULL) return FALSE;
    return g_ascii_strncasecmp(uri, "http://", 7) == 0 || g_ascii_strncasecmp(uri, "https://", 8) == 0;
}
