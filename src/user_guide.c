/* The user guide built into the binary (see user_guide.h) */
#include "user_guide.h"

#include <gio/gio.h>
#include <string.h>

/* Resource path of the pages (guide.gresource.xml, made by the Makefile) */
#define GUIDE_PREFIX "/org/lookin/guide/"

gchar* user_guide_read(const gchar* page) {
    if (page == NULL || page[0] == '\0' || page[0] == '/' || strstr(page, "..") != NULL) {
        return NULL;
    }
    gchar* path = g_strconcat(GUIDE_PREFIX, page, NULL);
    GBytes* bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    g_free(path);
    if (bytes == NULL) {
        return NULL;
    }
    gsize len = 0;
    const gchar* data = g_bytes_get_data(bytes, &len);
    gchar* text = g_utf8_make_valid(data != NULL ? data : "", (gssize)len);
    g_bytes_unref(bytes);
    return text;
}

gchar* user_guide_title(const gchar* markdown) {
    if (markdown == NULL) {
        return NULL;
    }
    const gchar* line = markdown;
    gboolean in_code = FALSE;
    while (*line != '\0') {
        const gchar* end = strchr(line, '\n');
        gsize len = end != NULL ? (gsize)(end - line) : strlen(line);
        if (len >= 3 && strncmp(line, "```", 3) == 0) {
            in_code = !in_code;
        } else if (!in_code && len > 2 && line[0] == '#' && line[1] == ' ') {
            gchar* title = g_strndup(line + 2, len - 2);
            return g_strstrip(title);
        }
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
    return NULL;
}

/* Add the pages under dir (a resource path ending in '/') to pages */
static void collect_pages(const gchar* dir, GPtrArray* pages) {
    gchar** children = g_resources_enumerate_children(dir, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (children == NULL) {
        return;
    }
    for (gint i = 0; children[i] != NULL; i++) {
        gchar* path = g_strconcat(dir, children[i], NULL);
        if (g_str_has_suffix(children[i], "/")) {
            collect_pages(path, pages);
        } else {
            g_ptr_array_add(pages, g_strdup(path + strlen(GUIDE_PREFIX)));
        }
        g_free(path);
    }
    g_strfreev(children);
}

static gint compare_strings(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

gchar** user_guide_pages(void) {
    GPtrArray* pages = g_ptr_array_new();
    collect_pages(GUIDE_PREFIX, pages);
    g_ptr_array_sort(pages, compare_strings);
    g_ptr_array_add(pages, NULL);
    return (gchar**)g_ptr_array_free(pages, FALSE);
}

/* path joined to the directory of page from, "." and ".." resolved. *above
 * is TRUE when it climbs above docs/ (the root of the guide's names): the
 * extra ".." are dropped from the result. Caller frees. */
static gchar* join_page(const gchar* from, const gchar* path, gboolean* above) {
    gchar* dir = from != NULL ? g_path_get_dirname(from) : g_strdup(".");
    gchar* joined = g_strcmp0(dir, ".") == 0 ? g_strdup(path) : g_strconcat(dir, "/", path, NULL);
    g_free(dir);
    gchar** parts = g_strsplit(joined, "/", -1);
    g_free(joined);
    GPtrArray* out = g_ptr_array_new();
    *above = FALSE;
    for (gint i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0' || strcmp(parts[i], ".") == 0) {
            continue;
        }
        if (strcmp(parts[i], "..") == 0) {
            if (out->len == 0) {
                *above = TRUE;
            } else {
                g_ptr_array_remove_index(out, out->len - 1);
            }
            continue;
        }
        g_ptr_array_add(out, parts[i]);
    }
    g_ptr_array_add(out, NULL);
    gchar* page = g_strjoinv("/", (gchar**)out->pdata);
    g_ptr_array_free(out, TRUE);
    g_strfreev(parts);
    return page;
}

static gboolean page_exists(const gchar* page) {
    gchar* path = g_strconcat(GUIDE_PREFIX, page, NULL);
    gboolean ok = g_resources_get_info(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL);
    g_free(path);
    return ok;
}

UserGuideLink user_guide_resolve_link(const gchar* from, const gchar* href, gchar** out_page, gchar** out_anchor) {
    if (out_page != NULL) *out_page = NULL;
    if (out_anchor != NULL) *out_anchor = NULL;
    if (href == NULL || href[0] == '\0') {
        return USER_GUIDE_LINK_NONE;
    }
    if (g_str_has_prefix(href, "http://") || g_str_has_prefix(href, "https://") ||
        g_str_has_prefix(href, "mailto:")) {
        return USER_GUIDE_LINK_WEB;
    }
    gchar* scheme = g_uri_parse_scheme(href);
    if (scheme != NULL) { /* file:, ftp:... */
        g_free(scheme);
        return USER_GUIDE_LINK_NONE;
    }
    const gchar* hash = strchr(href, '#');
    gchar* path = hash != NULL ? g_strndup(href, (gsize)(hash - href)) : g_strdup(href);
    gboolean above = FALSE;
    gchar* page = path[0] == '\0' ? g_strdup(from) : (path[0] == '/' ? NULL : join_page(from, path, &above));
    g_free(path);
    UserGuideLink kind = USER_GUIDE_LINK_NONE;
    if (page != NULL && !above && page_exists(page)) {
        kind = USER_GUIDE_LINK_PAGE;
        if (out_anchor != NULL && hash != NULL && hash[1] != '\0') {
            *out_anchor = g_strdup(hash + 1);
        }
    }
    if (out_page != NULL && page != NULL) {
        *out_page = g_strdup(page);
    }
    g_free(page);
    return kind;
}

gchar* user_guide_anchor(const gchar* heading) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = heading != NULL ? heading : ""; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == ' ') {
            g_string_append_c(out, '-');
        } else if (c == '-' || c == '_' || g_unichar_isalnum(c)) {
            g_string_append_unichar(out, g_unichar_tolower(c));
        }
    }
    return g_string_free(out, FALSE);
}
