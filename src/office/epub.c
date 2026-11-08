/* EPUB e-books to HTML (see epub.h). */
#include "office/epub.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#include "formats/zip_reader.h"
#include "formats/zip_xml.h"

#define MAX_PART (16u * 1024 * 1024)
#define MAX_IMAGES (12u * 1024 * 1024)
#define MAX_TOC 60

/* --- container.xml --- */

static void container_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                            const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    gchar** opf = data;
    if (*opf == NULL && strcmp(zip_xml_local(name), "rootfile") == 0) {
        *opf = g_strdup(zip_xml_attr(names, values, "full-path"));
    }
}

/* --- Package (OPF) --- */

typedef struct {
    gchar* href;            /* resolved part */
    gchar* media_type;
    gchar* properties;
} Item;

static void item_free(gpointer data) {
    Item* it = data;
    g_free(it->href);
    g_free(it->media_type);
    g_free(it->properties);
    g_free(it);
}

typedef struct {
    gchar* dir;
    GHashTable* items;      /* id -> Item* */
    GPtrArray* spine;       /* item ids */
    gchar* toc_id;          /* EPUB 2 NCX (spine toc attribute) */
    gchar* cover_id;        /* EPUB 2 <meta name="cover"> */
    GHashTable* meta;       /* "title", "creator"... -> text (first) */
    GString* text;
    gchar* current;         /* metadata element being read */
} Opf;

static void opf_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Opf* o = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "item") == 0) {
        const gchar* id = zip_xml_attr(names, values, "id");
        const gchar* href = zip_xml_attr(names, values, "href");
        if (id != NULL && href != NULL) {
            Item* it = g_new0(Item, 1);
            it->href = zip_xml_resolve(o->dir, href);
            it->media_type = g_strdup(zip_xml_attr(names, values, "media-type"));
            it->properties = g_strdup(zip_xml_attr(names, values, "properties"));
            g_hash_table_replace(o->items, g_strdup(id), it);
        }
    } else if (strcmp(n, "itemref") == 0) {
        const gchar* idref = zip_xml_attr(names, values, "idref");
        const gchar* linear = zip_xml_attr(names, values, "linear");
        if (idref != NULL && g_strcmp0(linear, "no") != 0) {
            g_ptr_array_add(o->spine, g_strdup(idref));
        }
    } else if (strcmp(n, "spine") == 0) {
        g_free(o->toc_id);
        o->toc_id = g_strdup(zip_xml_attr(names, values, "toc"));
    } else if (strcmp(n, "meta") == 0 && g_strcmp0(zip_xml_attr(names, values, "name"), "cover") == 0) {
        g_free(o->cover_id);
        o->cover_id = g_strdup(zip_xml_attr(names, values, "content"));
    } else if (g_str_has_prefix(name, "dc:")) {
        g_free(o->current);
        o->current = g_strdup(n);
        g_string_truncate(o->text, 0);
    }
}

static void opf_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Opf* o = data;
    if (o->current != NULL && g_str_has_prefix(name, "dc:")) {
        gchar* value = g_strstrip(g_strdup(o->text->str));
        if (value[0] != '\0' && !g_hash_table_contains(o->meta, o->current)) {
            g_hash_table_insert(o->meta, g_strdup(o->current), value);
        } else {
            g_free(value);
        }
        g_clear_pointer(&o->current, g_free);
    }
}

static void opf_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Opf* o = data;
    if (o->current != NULL) {
        g_string_append_len(o->text, text, (gssize)len);
    }
}

/* --- Table of contents --- */

typedef struct {
    GPtrArray* entries;     /* "<li>..</li>" lines */
    gint depth;             /* <ol> / navPoint depth */
    gboolean in_toc_nav;    /* EPUB 3: inside <nav epub:type="toc"> */
    gboolean in_label;
    GString* label;
    gint nav_depth;
} Toc;

static void toc_add(Toc* t) {
    gchar* label = g_strstrip(g_strdup(t->label->str));
    if (label[0] != '\0' && t->entries->len < MAX_TOC) {
        gchar* escaped = g_markup_escape_text(label, -1);
        GString* line = g_string_new(NULL);
        for (gint i = 1; i < t->depth; i++) {
            g_string_append(line, "&#160;&#160;&#160;&#160;");
        }
        g_string_append(line, escaped);
        g_ptr_array_add(t->entries, g_string_free(line, FALSE));
        g_free(escaped);
    }
    g_free(label);
    g_string_truncate(t->label, 0);
}

static void nav_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Toc* t = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "nav") == 0) {
        const gchar* type = zip_xml_attr(names, values, "type");
        if (type != NULL && strstr(type, "toc") != NULL) {
            t->in_toc_nav = TRUE;
            t->nav_depth = 0;
        }
        if (t->in_toc_nav) {
            t->nav_depth++;
        }
    } else if (t->in_toc_nav && strcmp(n, "ol") == 0) {
        t->depth++;
    } else if (t->in_toc_nav && strcmp(n, "a") == 0) {
        t->in_label = TRUE;
        g_string_truncate(t->label, 0);
    } else if (strcmp(n, "navPoint") == 0) {
        t->depth++;
    } else if (strcmp(n, "text") == 0 && t->depth > 0) {
        t->in_label = TRUE;
        g_string_truncate(t->label, 0);
    }
}

static void nav_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Toc* t = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "nav") == 0 && t->in_toc_nav) {
        if (--t->nav_depth == 0) {
            t->in_toc_nav = FALSE;
        }
    } else if (t->in_toc_nav && strcmp(n, "ol") == 0) {
        t->depth--;
    } else if (t->in_label && (strcmp(n, "a") == 0 || strcmp(n, "text") == 0)) {
        t->in_label = FALSE;
        toc_add(t);
    } else if (strcmp(n, "navPoint") == 0) {
        t->depth--;
    }
}

static void nav_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Toc* t = data;
    if (t->in_label) {
        g_string_append_len(t->label, text, (gssize)len);
    }
}

/* --- Chapters --- */

static const gchar* mime_of(const gchar* part) {
    const gchar* dot = strrchr(part, '.');
    if (dot == NULL) return "image/png";
    if (g_ascii_strcasecmp(dot, ".jpg") == 0 || g_ascii_strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (g_ascii_strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (g_ascii_strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    return "image/png";
}

/* data: URI of an image part, NULL if missing or over the image budget */
static gchar* image_uri(ZipReader* zip, const gchar* part, gsize* budget) {
    gint64 size = zip_reader_get_size(zip, part);
    if (size <= 0 || (guint64)size > *budget) {
        return NULL;
    }
    GBytes* bytes = zip_reader_read(zip, part, *budget, NULL);
    if (bytes == NULL) {
        return NULL;
    }
    *budget -= g_bytes_get_size(bytes);
    gchar* b64 = g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
    gchar* uri = g_strdup_printf("data:%s;base64,%s", mime_of(part), b64);
    g_free(b64);
    g_bytes_unref(bytes);
    return uri;
}

/* Inner HTML of <body>, with image sources ("src", "xlink:href") turned
 * into data: URIs */
static gchar* chapter_body(ZipReader* zip, const gchar* part, gsize* image_budget) {
    GBytes* bytes = zip_reader_read(zip, part, MAX_PART, NULL);
    if (bytes == NULL) {
        return NULL;
    }
    gchar* html = g_utf8_make_valid(g_bytes_get_data(bytes, NULL), (gssize)g_bytes_get_size(bytes));
    g_bytes_unref(bytes);
    const gchar* start = html;
    const gchar* body = strstr(html, "<body");
    if (body != NULL) {
        const gchar* gt = strchr(body, '>');
        start = gt != NULL ? gt + 1 : body;
    }
    const gchar* end = g_strrstr(start, "</body>");
    gchar* inner = end != NULL ? g_strndup(start, (gsize)(end - start)) : g_strdup(start);
    g_free(html);

    gchar* dir = zip_xml_dirname(part);
    GString* out = g_string_new(NULL);
    const gchar* p = inner;
    while (*p != '\0') {
        const gchar* src = NULL;
        gsize attr_len = 0;
        const gchar* a = strstr(p, "src=\"");
        const gchar* b = strstr(p, "xlink:href=\"");
        if (a != NULL && (b == NULL || a < b)) {
            src = a;
            attr_len = 5;
        } else if (b != NULL) {
            src = b;
            attr_len = 12;
        }
        if (src == NULL) {
            g_string_append(out, p);
            break;
        }
        const gchar* value = src + attr_len;
        const gchar* close = strchr(value, '"');
        if (close == NULL) {
            g_string_append(out, p);
            break;
        }
        g_string_append_len(out, p, (gssize)(src - p));
        gchar* target = g_strndup(value, (gsize)(close - value));
        gchar* uri = NULL;
        if (!g_str_has_prefix(target, "data:") && strstr(target, "://") == NULL) {
            gchar* image_part = zip_xml_resolve(dir, target);
            uri = image_uri(zip, image_part, image_budget);
            g_free(image_part);
        }
        g_string_append_printf(out, "src=\"%s\"", uri != NULL ? uri : target);
        g_free(uri);
        g_free(target);
        p = close + 1;
    }
    g_free(dir);
    g_free(inner);
    return g_string_free(out, FALSE);
}

gchar* epub_file_to_html(const gchar* path, gsize max_text, EpubInfo* info, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    gchar* opf_part = NULL;
    static const GMarkupParser container_parser = { container_start, NULL, NULL, NULL, NULL };
    if (!zip_xml_parse(zip, "META-INF/container.xml", 1024 * 1024, FALSE, &container_parser, &opf_part, error) ||
        opf_part == NULL) {
        if (error != NULL && *error == NULL) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an EPUB (no package document)"));
        }
        g_free(opf_part);
        zip_reader_free(zip);
        return NULL;
    }
    Opf o = { zip_xml_dirname(opf_part), g_hash_table_new_full(g_str_hash, g_str_equal, g_free, item_free),
              g_ptr_array_new_with_free_func(g_free), NULL, NULL,
              g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), g_string_new(NULL), NULL };
    static const GMarkupParser opf_parser = { opf_start, opf_end, opf_text, NULL, NULL };
    gchar* html = NULL;
    if (zip_xml_parse(zip, opf_part, MAX_PART, FALSE, &opf_parser, &o, error)) {
        const gchar* title = g_hash_table_lookup(o.meta, "title");
        const gchar* author = g_hash_table_lookup(o.meta, "creator");
        GString* out = g_string_new("<html><head>");
        if (title != NULL) {
            gchar* t = g_markup_escape_text(title, -1);
            g_string_append_printf(out, "<title>%s</title>", t);
            g_free(t);
        }
        if (author != NULL) {
            gchar* a = g_markup_escape_text(author, -1);
            g_string_append_printf(out, "<meta name=\"author\" content=\"%s\">", a);
            g_free(a);
        }
        g_string_append(out, "</head><body>\n");

        /* Cover: EPUB 3 "cover-image" property, or EPUB 2 meta */
        gsize image_budget = MAX_IMAGES;
        const Item* cover = o.cover_id != NULL ? g_hash_table_lookup(o.items, o.cover_id) : NULL;
        GHashTableIter it;
        gpointer key, value;
        const Item* nav = NULL;
        g_hash_table_iter_init(&it, o.items);
        while (g_hash_table_iter_next(&it, &key, &value)) {
            const Item* item = value;
            if (item->properties != NULL && strstr(item->properties, "cover-image") != NULL) {
                cover = item;
            }
            if (item->properties != NULL && strstr(item->properties, "nav") != NULL) {
                nav = item;
            }
        }
        if (cover != NULL && cover->media_type != NULL && g_str_has_prefix(cover->media_type, "image/")) {
            gchar* uri = image_uri(zip, cover->href, &image_budget);
            if (uri != NULL) {
                g_string_append_printf(out, "<p><img src=\"%s\"></p>\n", uri);
                g_free(uri);
            }
        }
        /* Other metadata */
        static const struct { const gchar* key; const gchar* label; } META[] = {
            { "publisher", "Publisher" }, { "date", "Date" }, { "language", "Language" },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(META); i++) {
            const gchar* v = g_hash_table_lookup(o.meta, META[i].key);
            if (v != NULL) {
                gchar* e = g_markup_escape_text(v, -1);
                g_string_append_printf(out, "<p><b>%s:</b> %s</p>\n", META[i].label, e);
                g_free(e);
            }
        }
        const gchar* description = g_hash_table_lookup(o.meta, "description");
        if (description != NULL) {
            /* Often HTML itself */
            g_string_append_printf(out, "<blockquote>%s</blockquote>\n", description);
        }

        /* Table of contents */
        Toc toc = { g_ptr_array_new_with_free_func(g_free), 0, FALSE, FALSE, g_string_new(NULL), 0 };
        static const GMarkupParser nav_parser = { nav_start, nav_end, nav_text, NULL, NULL };
        const Item* ncx = o.toc_id != NULL ? g_hash_table_lookup(o.items, o.toc_id) : NULL;
        if (nav != NULL) {
            zip_xml_parse(zip, nav->href, MAX_PART, TRUE, &nav_parser, &toc, NULL);
        }
        if (toc.entries->len == 0 && ncx != NULL) {
            toc.depth = 0;
            zip_xml_parse(zip, ncx->href, MAX_PART, TRUE, &nav_parser, &toc, NULL);
        }
        if (toc.entries->len > 0) {
            g_string_append_printf(out, "<h2>%s</h2>\n<ul>\n", _("Contents"));
            for (guint i = 0; i < toc.entries->len; i++) {
                g_string_append_printf(out, "<li>%s</li>\n", (gchar*)g_ptr_array_index(toc.entries, i));
            }
            g_string_append(out, "</ul>\n");
        }
        g_ptr_array_free(toc.entries, TRUE);
        g_string_free(toc.label, TRUE);

        /* Chapters in reading order (the navigation document is skipped) */
        gsize text = 0;
        gboolean cut = FALSE;
        for (guint i = 0; i < o.spine->len; i++) {
            const Item* item = g_hash_table_lookup(o.items, g_ptr_array_index(o.spine, i));
            if (item == NULL || item == nav || item == cover) {
                continue;
            }
            if (text > max_text) {
                cut = TRUE;
                break;
            }
            gchar* body = chapter_body(zip, item->href, &image_budget);
            if (body != NULL) {
                g_string_append_printf(out, "<hr>\n%s\n", body);
                text += strlen(body);
                g_free(body);
            }
        }
        if (cut) {
            g_string_append_printf(out, "<hr>\n<p><i>%s</i></p>\n", _("Only the first chapters are shown."));
        }
        g_string_append(out, "</body></html>\n");
        html = g_string_free(out, FALSE);
        if (info != NULL) {
            info->title = g_strdup(title);
            info->author = g_strdup(author);
            info->n_chapters = o.spine->len;
        }
    }
    g_free(o.dir);
    g_hash_table_destroy(o.items);
    g_ptr_array_free(o.spine, TRUE);
    g_free(o.toc_id);
    g_free(o.cover_id);
    g_hash_table_destroy(o.meta);
    g_string_free(o.text, TRUE);
    g_free(o.current);
    g_free(opf_part);
    zip_reader_free(zip);
    return html;
}

void epub_info_clear(EpubInfo* info) {
    g_clear_pointer(&info->title, g_free);
    g_clear_pointer(&info->author, g_free);
}
