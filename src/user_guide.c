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
