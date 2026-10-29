/* XML parts of ZIP-based documents (see zip_xml.h). */
#include "zip_xml.h"

#include <string.h>

G_DEFINE_QUARK(zip-xml-stop, zip_xml_stop)

void zip_xml_stop(GError** error) {
    g_set_error_literal(error, ZIP_XML_STOP_DOMAIN, 0, "stop");
}

gboolean zip_xml_parse(ZipReader* zip, const gchar* part, gsize max_size, gboolean optional,
                       const GMarkupParser* parser, gpointer data, GError** error) {
    if (optional && !zip_reader_has(zip, part)) {
        return FALSE;
    }
    GBytes* bytes = zip_reader_read(zip, part, max_size, error);
    if (bytes == NULL) {
        return FALSE;
    }
    gsize len = 0;
    const gchar* xml = g_bytes_get_data(bytes, &len);
    if (len >= 3 && memcmp(xml, "\xEF\xBB\xBF", 3) == 0) {
        xml += 3;
        len -= 3;
    }
    GError* err = NULL;
    GMarkupParseContext* ctx = g_markup_parse_context_new(parser, 0, data, NULL);
    gboolean ok = g_markup_parse_context_parse(ctx, xml, (gssize)len, &err) &&
                  g_markup_parse_context_end_parse(ctx, &err);
    g_markup_parse_context_free(ctx);
    g_bytes_unref(bytes);
    if (!ok && err != NULL && err->domain == ZIP_XML_STOP_DOMAIN) {
        g_error_free(err);
        return TRUE;
    }
    if (err != NULL) {
        g_propagate_error(error, err);
    }
    return ok;
}

const gchar* zip_xml_local(const gchar* name) {
    const gchar* colon = strrchr(name, ':');
    return colon != NULL ? colon + 1 : name;
}

const gchar* zip_xml_attr(const gchar** names, const gchar** values, const gchar* local) {
    for (gint i = 0; names[i] != NULL; i++) {
        if (strcmp(zip_xml_local(names[i]), local) == 0) {
            return values[i];
        }
    }
    return NULL;
}

gchar* zip_xml_resolve(const gchar* base_dir, const gchar* target) {
    gchar* joined = (target[0] == '/') ? g_strdup(target + 1)
                  : (base_dir[0] == '\0') ? g_strdup(target)
                  : g_strconcat(base_dir, "/", target, NULL);
    gchar** parts = g_strsplit(joined, "/", -1);
    GPtrArray* stack = g_ptr_array_new();
    for (gsize i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0' || strcmp(parts[i], ".") == 0) {
            continue;
        }
        if (strcmp(parts[i], "..") == 0) {
            if (stack->len > 0) {
                g_ptr_array_remove_index(stack, stack->len - 1);
            }
            continue;
        }
        g_ptr_array_add(stack, parts[i]);
    }
    g_ptr_array_add(stack, NULL);
    gchar* out = g_strjoinv("/", (gchar**)stack->pdata);
    g_ptr_array_free(stack, TRUE);
    g_strfreev(parts);
    g_free(joined);
    /* %20 and other escapes in targets */
    gchar* unescaped = g_uri_unescape_string(out, NULL);
    if (unescaped != NULL) {
        g_free(out);
        out = unescaped;
    }
    return out;
}

gchar* zip_xml_dirname(const gchar* part) {
    const gchar* slash = strrchr(part, '/');
    return slash != NULL ? g_strndup(part, (gsize)(slash - part)) : g_strdup("");
}

typedef struct {
    GHashTable* rels;
    const gchar* dir;
} RelsData;

static void rels_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    RelsData* r = data;
    if (strcmp(zip_xml_local(name), "Relationship") != 0) {
        return;
    }
    const gchar* id = zip_xml_attr(names, values, "Id");
    const gchar* target = zip_xml_attr(names, values, "Target");
    const gchar* mode = zip_xml_attr(names, values, "TargetMode");
    if (id != NULL && target != NULL && g_strcmp0(mode, "External") != 0) {
        g_hash_table_insert(r->rels, g_strdup(id), zip_xml_resolve(r->dir, target));
    }
}

GHashTable* zip_xml_relationships(ZipReader* zip, const gchar* part) {
    GHashTable* rels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar* dir = zip_xml_dirname(part);
    const gchar* file = part + strlen(dir) + (dir[0] != '\0');
    gchar* rels_part = dir[0] != '\0' ? g_strdup_printf("%s/_rels/%s.rels", dir, file)
                                      : g_strdup_printf("_rels/%s.rels", file);
    static const GMarkupParser parser = { rels_start, NULL, NULL, NULL, NULL };
    RelsData data = { rels, dir };
    zip_xml_parse(zip, rels_part, 4 * 1024 * 1024, TRUE, &parser, &data, NULL);
    g_free(rels_part);
    g_free(dir);
    return rels;
}
