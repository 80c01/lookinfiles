/* PowerPoint presentations drawn like PowerPoint (see pptx.h) */
#include "pptx.h"

#include <gio/gio.h>
#include <string.h>

#include "pptx_shapes.h"
#include "slide_render.h"
#include "zip_reader.h"
#include "zip_xml.h"

#define MAX_PART (32u * 1024 * 1024)
#define EMU_PER_PT 12700.0
#define MAX_SLIDES 2000

typedef struct {
    GPtrArray* ids;          /* r:id of each slide, in order */
    gdouble width, height;   /* points */
} PresParse;

static void pres_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    PresParse* p = data;
    const gchar* local = zip_xml_local(name);
    if (strcmp(local, "sldId") == 0 && p->ids->len < MAX_SLIDES) {
        for (gint i = 0; names[i] != NULL; i++) {
            if (g_str_has_suffix(names[i], ":id")) {
                g_ptr_array_add(p->ids, g_strdup(values[i]));
            }
        }
    } else if (strcmp(local, "sldSz") == 0) {
        const gchar* cx = zip_xml_attr(names, values, "cx");
        const gchar* cy = zip_xml_attr(names, values, "cy");
        if (cx != NULL && cy != NULL) {
            p->width = g_ascii_strtod(cx, NULL) / EMU_PER_PT;
            p->height = g_ascii_strtod(cy, NULL) / EMU_PER_PT;
        }
    }
}

/* Target of the first relationship of a part whose type ends with suffix */
static gchar* related_part(ZipReader* zip, const gchar* part, const gchar* suffix) {
    gchar* dir = zip_xml_dirname(part);
    gchar* base = g_path_get_basename(part);
    gchar* rels_name = dir[0] == '\0' ? g_strconcat("_rels/", base, ".rels", NULL)
                                      : g_strconcat(dir, "/_rels/", base, ".rels", NULL);
    gchar* found = NULL;
    GBytes* bytes = zip_reader_has(zip, rels_name) ? zip_reader_read(zip, rels_name, MAX_PART, NULL) : NULL;
    if (bytes != NULL) {
        /* Types are full URIs or (in hand-made files) short names */
        gchar* xml = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
        for (gchar* p = strstr(xml, "<Relationship"); p != NULL && found == NULL; p = strstr(p + 1, "<Relationship")) {
            gchar* end = strchr(p, '>');
            if (end == NULL) break;
            gchar* tag = g_strndup(p, (gsize)(end - p));
            gchar* type = strstr(tag, "Type=\"");
            gchar* target = strstr(tag, "Target=\"");
            if (type != NULL && target != NULL) {
                type += 6;
                target += 8;
                gchar* te = strchr(type, '"');
                gchar* ge = strchr(target, '"');
                if (te != NULL && ge != NULL) {
                    gchar* t = g_strndup(type, (gsize)(te - type));
                    if ((g_str_has_suffix(t, suffix) || strcmp(t, suffix + 1) == 0) && strstr(tag, "External") == NULL) {
                        gchar* g = g_strndup(target, (gsize)(ge - target));
                        found = zip_xml_resolve(dir, g);
                        g_free(g);
                    }
                    g_free(t);
                }
            }
            g_free(tag);
        }
        g_free(xml);
        g_bytes_unref(bytes);
    }
    g_free(rels_name);
    g_free(base);
    g_free(dir);
    return found;
}

/* Trees read once per part */
typedef struct {
    ZipReader* zip;
    PptxTheme* theme;
    GHashTable* masters;   /* part -> PptxTree* */
    GHashTable* layouts;   /* part -> PptxTree* */
} Cache;

static const PptxTree* master_of(Cache* c, const gchar* part) {
    PptxTree* t = g_hash_table_lookup(c->masters, part);
    if (t == NULL) {
        if (c->theme == NULL) {
            gchar* theme = related_part(c->zip, part, "/theme");
            c->theme = pptx_theme_read(c->zip, theme);
            g_free(theme);
        }
        t = pptx_tree_read(c->zip, part, c->theme, NULL);
        g_hash_table_insert(c->masters, g_strdup(part), t);
    }
    return t;
}

SlideDeck* pptx_read_deck(const gchar* path, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    PresParse pres = { g_ptr_array_new_with_free_func(g_free), 720, 540 };
    static const GMarkupParser pres_parser = { pres_start, NULL, NULL, NULL, NULL };
    if (!zip_xml_parse(zip, "ppt/presentation.xml", MAX_PART, FALSE, &pres_parser, &pres, error)) {
        g_ptr_array_free(pres.ids, TRUE);
        zip_reader_free(zip);
        return NULL;
    }
    GHashTable* rels = zip_xml_relationships(zip, "ppt/presentation.xml");
    Cache c = { zip, NULL, g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)pptx_tree_free),
                g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)pptx_tree_free) };
    SlideDeck* deck = slide_deck_new(pres.width, pres.height);
    for (guint i = 0; i < pres.ids->len; i++) {
        const gchar* part = g_hash_table_lookup(rels, g_ptr_array_index(pres.ids, i));
        if (part == NULL || !zip_reader_has(zip, part)) {
            continue;
        }
        gchar* layout_part = related_part(zip, part, "/slideLayout");
        gchar* master_part = layout_part != NULL ? related_part(zip, layout_part, "/slideMaster") : NULL;
        const PptxTree* master = master_part != NULL ? master_of(&c, master_part) : NULL;
        if (c.theme == NULL) {
            c.theme = pptx_theme_read(zip, NULL);
        }
        const PptxTree* layout = NULL;
        if (layout_part != NULL) {
            PptxTree* l = g_hash_table_lookup(c.layouts, layout_part);
            if (l == NULL) {
                l = pptx_tree_read(zip, layout_part, c.theme, master);
                g_hash_table_insert(c.layouts, g_strdup(layout_part), l);
            }
            layout = l;
        }
        PptxTree* tree = pptx_tree_read(zip, part, c.theme, master);
        Slide* slide = pptx_build_slide(tree, layout, master, c.theme, deck->width, deck->height);
        pptx_tree_free(tree);
        gchar* notes_part = related_part(zip, part, "/notesSlide");
        if (notes_part != NULL) {
            PptxTree* notes = pptx_tree_read(zip, notes_part, c.theme, master);
            slide->notes = pptx_tree_notes(notes);
            pptx_tree_free(notes);
            g_free(notes_part);
        }
        g_ptr_array_add(deck->slides, slide);
        g_free(layout_part);
        g_free(master_part);
    }
    deck->font = g_strdup(pptx_theme_font(c.theme));
    pptx_theme_free(c.theme);
    g_hash_table_destroy(c.masters);
    g_hash_table_destroy(c.layouts);
    g_hash_table_destroy(rels);
    g_ptr_array_free(pres.ids, TRUE);
    zip_reader_free(zip);
    return deck;
}

PreviewDoc* pptx_file_preview(const gchar* path, const gchar* name, const gchar* size_text, guint* n_slides,
                              GError** error) {
    SlideDeck* deck = pptx_read_deck(path, error);
    if (deck == NULL) {
        return NULL;
    }
    guint n = deck->slides->len;
    if (n_slides != NULL) {
        *n_slides = n;
    }
    GString* subtitle = g_string_new("PowerPoint presentation");
    g_string_append_printf(subtitle, " · %u %s", n, n == 1 ? "slide" : "slides");
    if (size_text != NULL) {
        g_string_append_printf(subtitle, " · %s", size_text);
    }
    PreviewDoc* doc = slide_deck_preview(deck, name != NULL ? name : "", subtitle->str, PPTX_MAX_DRAWN);
    g_string_free(subtitle, TRUE);
    slide_deck_free(deck);
    return doc;
}
