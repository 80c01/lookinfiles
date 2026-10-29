/* PowerPoint presentations to HTML (see pptx.h). */
#include "pptx.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "zip_reader.h"
#include "zip_xml.h"

#define MAX_PART (32u * 1024 * 1024)
#define MAX_IMAGE (4u * 1024 * 1024)

typedef struct {
    GPtrArray* ids;          /* r:id of each slide, in order */
} PresParse;

static void pres_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    PresParse* p = data;
    if (strcmp(zip_xml_local(name), "sldId") == 0) {
        for (gint i = 0; names[i] != NULL; i++) {
            if (g_str_has_suffix(names[i], ":id")) {
                g_ptr_array_add(p->ids, g_strdup(values[i]));
            }
        }
    }
}

typedef struct {
    ZipReader* zip;
    GHashTable* rels;        /* of the slide: images */
    GString* out;
    gsize* text_len;
    gsize max_text;
    gchar* ph_type;          /* placeholder type of the current shape */
    gboolean in_shape;
    gboolean in_text;        /* a:t */
    GString* para;           /* text of the current paragraph */
    gint para_level;
    gint list_depth;         /* open <ul> in the current shape */
    gboolean in_table;
    gboolean in_cell;
    gboolean bold;
    gboolean italic;
} SlideParse;

static void open_lists(SlideParse* s, gint depth) {
    while (s->list_depth < depth) {
        g_string_append(s->out, "<ul>\n");
        s->list_depth++;
    }
    while (s->list_depth > depth) {
        g_string_append(s->out, "</ul>\n");
        s->list_depth--;
    }
}

static gboolean is_title(const gchar* ph) {
    return ph != NULL && (strcmp(ph, "title") == 0 || strcmp(ph, "ctrTitle") == 0);
}

static void end_paragraph(SlideParse* s) {
    gchar* text = g_strstrip(g_strdup(s->para->str));
    g_string_truncate(s->para, 0);
    if (text[0] == '\0') {
        g_free(text);
        return;
    }
    if (s->in_table) {
        g_string_append_printf(s->out, "%s ", text);
    } else if (is_title(s->ph_type)) {
        open_lists(s, 0);
        g_string_append_printf(s->out, "<h3>%s</h3>\n", text);
    } else if (g_strcmp0(s->ph_type, "subTitle") == 0) {
        open_lists(s, 0);
        g_string_append_printf(s->out, "<p><i>%s</i></p>\n", text);
    } else if (s->ph_type != NULL && (strcmp(s->ph_type, "body") == 0 || strcmp(s->ph_type, "obj") == 0 ||
                                      strcmp(s->ph_type, "") == 0)) {
        open_lists(s, s->para_level + 1);
        g_string_append_printf(s->out, "<li>%s</li>\n", text);
    } else {
        open_lists(s, 0);
        g_string_append_printf(s->out, "<p>%s</p>\n", text);
    }
    g_free(text);
}

static void append_picture(SlideParse* s, const gchar* rid) {
    const gchar* part = rid != NULL ? g_hash_table_lookup(s->rels, rid) : NULL;
    if (part == NULL) {
        return;
    }
    GBytes* bytes = zip_reader_read(s->zip, part, MAX_IMAGE, NULL);
    if (bytes == NULL) {
        return;
    }
    const gchar* dot = strrchr(part, '.');
    const gchar* mime = dot == NULL ? "image/png"
                        : (g_ascii_strcasecmp(dot, ".jpg") == 0 || g_ascii_strcasecmp(dot, ".jpeg") == 0) ? "image/jpeg"
                        : g_ascii_strcasecmp(dot, ".gif") == 0 ? "image/gif"
                        : g_ascii_strcasecmp(dot, ".svg") == 0 ? "image/svg+xml" : "image/png";
    gchar* b64 = g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
    open_lists(s, 0);
    g_string_append_printf(s->out, "<p><img src=\"data:%s;base64,%s\"></p>\n", mime, b64);
    g_free(b64);
    g_bytes_unref(bytes);
}

static void slide_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                        const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SlideParse* s = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(name, "p:sp") == 0) {
        s->in_shape = TRUE;
        g_clear_pointer(&s->ph_type, g_free);
    } else if (strcmp(name, "p:ph") == 0 && s->in_shape) {
        const gchar* type = zip_xml_attr(names, values, "type");
        g_free(s->ph_type);
        s->ph_type = g_strdup(type != NULL ? type : "");
    } else if (strcmp(n, "p") == 0 && g_str_has_prefix(name, "a:")) {
        g_string_truncate(s->para, 0);
        s->para_level = 0;
    } else if (strcmp(name, "a:pPr") == 0) {
        const gchar* lvl = zip_xml_attr(names, values, "lvl");
        s->para_level = lvl != NULL ? CLAMP(atoi(lvl), 0, 5) : 0;
    } else if (strcmp(name, "a:rPr") == 0) {
        s->bold = g_strcmp0(zip_xml_attr(names, values, "b"), "1") == 0;
        s->italic = g_strcmp0(zip_xml_attr(names, values, "i"), "1") == 0;
    } else if (strcmp(name, "a:r") == 0) {
        s->bold = s->italic = FALSE;
    } else if (strcmp(name, "a:t") == 0) {
        s->in_text = TRUE;
        if (s->bold) {
            g_string_append(s->para, "<b>");
        }
        if (s->italic) {
            g_string_append(s->para, "<i>");
        }
    } else if (strcmp(name, "a:br") == 0) {
        g_string_append(s->para, "<br>");
    } else if (strcmp(name, "a:tbl") == 0) {
        open_lists(s, 0);
        s->in_table = TRUE;
        g_string_append(s->out, "<table>\n");
    } else if (strcmp(name, "a:tr") == 0) {
        g_string_append(s->out, "<tr>");
    } else if (strcmp(name, "a:tc") == 0) {
        g_string_append(s->out, "<td>");
        s->in_cell = TRUE;
    } else if (strcmp(name, "a:blip") == 0) {
        const gchar* rid = NULL;
        for (gint i = 0; names[i] != NULL; i++) {
            if (g_str_has_suffix(names[i], ":embed")) {
                rid = values[i];
            }
        }
        append_picture(s, rid);
    }
}

static void slide_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    SlideParse* s = data;
    if (strcmp(name, "a:t") == 0) {
        s->in_text = FALSE;
        if (s->italic) {
            g_string_append(s->para, "</i>");
        }
        if (s->bold) {
            g_string_append(s->para, "</b>");
        }
    } else if (strcmp(name, "a:p") == 0) {
        end_paragraph(s);
        if (*s->text_len > s->max_text) {
            zip_xml_stop(error);
        }
    } else if (strcmp(name, "p:sp") == 0) {
        open_lists(s, 0);
        s->in_shape = FALSE;
        g_clear_pointer(&s->ph_type, g_free);
    } else if (strcmp(name, "a:tc") == 0) {
        g_string_append(s->out, "</td>");
        s->in_cell = FALSE;
    } else if (strcmp(name, "a:tr") == 0) {
        g_string_append(s->out, "</tr>\n");
    } else if (strcmp(name, "a:tbl") == 0) {
        g_string_append(s->out, "</table>\n");
        s->in_table = FALSE;
    }
}

static void slide_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SlideParse* s = data;
    if (s->in_text) {
        gchar* escaped = g_markup_escape_text(text, (gssize)len);
        g_string_append(s->para, escaped);
        g_free(escaped);
        *s->text_len += len;
    }
}

gchar* pptx_file_to_html(const gchar* path, gsize max_text, guint* n_slides, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    PresParse pres = { g_ptr_array_new_with_free_func(g_free) };
    static const GMarkupParser pres_parser = { pres_start, NULL, NULL, NULL, NULL };
    if (!zip_xml_parse(zip, "ppt/presentation.xml", MAX_PART, FALSE, &pres_parser, &pres, error)) {
        g_ptr_array_free(pres.ids, TRUE);
        zip_reader_free(zip);
        return NULL;
    }
    GHashTable* rels = zip_xml_relationships(zip, "ppt/presentation.xml");
    GString* out = g_string_new("<html><head></head><body>\n");
    gsize text_len = 0;
    guint shown = 0;
    for (guint i = 0; i < pres.ids->len; i++) {
        const gchar* part = g_hash_table_lookup(rels, g_ptr_array_index(pres.ids, i));
        if (part == NULL) {
            continue;
        }
        if (text_len > max_text) {
            g_string_append(out, "<hr>\n<p><i>Presentation truncated: only the first slides are shown.</i></p>\n");
            break;
        }
        shown++;
        g_string_append_printf(out, "%s<h2>Slide %u</h2>\n", shown > 1 ? "<hr>\n" : "", i + 1);
        SlideParse s = { zip, zip_xml_relationships(zip, part), out, &text_len, max_text, NULL, FALSE, FALSE,
                         g_string_new(NULL), 0, 0, FALSE, FALSE, FALSE, FALSE };
        static const GMarkupParser slide_parser = { slide_start, slide_end, slide_text, NULL, NULL };
        zip_xml_parse(zip, part, MAX_PART, TRUE, &slide_parser, &s, NULL);
        open_lists(&s, 0);
        g_string_free(s.para, TRUE);
        g_free(s.ph_type);
        g_hash_table_destroy(s.rels);
    }
    g_string_append(out, "</body></html>\n");
    if (n_slides != NULL) {
        *n_slides = pres.ids->len;
    }
    g_hash_table_destroy(rels);
    g_ptr_array_free(pres.ids, TRUE);
    zip_reader_free(zip);
    return g_string_free(out, FALSE);
}
