/* OpenDocument text and presentations to HTML (see odf.h). */
#include "odf.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "zip_reader.h"
#include "zip_xml.h"

#define MAX_PART (64u * 1024 * 1024)
#define MAX_IMAGE (4u * 1024 * 1024)

/* --- Styles --- */

enum {
    STYLE_BOLD = 1 << 0,
    STYLE_ITALIC = 1 << 1,
    STYLE_UNDERLINE = 1 << 2,
    STYLE_STRIKE = 1 << 3,
    STYLE_BOLD_SET = 1 << 4,    /* the style sets the weight (normal overrides a parent) */
    STYLE_ITALIC_SET = 1 << 5,
};

typedef struct {
    guint flags;
    gchar* parent;
    gint heading;               /* outline level of a paragraph style, 0 if none */
} OdfStyle;

static void odf_style_free(gpointer data) {
    OdfStyle* s = data;
    g_free(s->parent);
    g_free(s);
}

typedef struct {
    GHashTable* styles;         /* name -> OdfStyle* */
    GHashTable* numbered;       /* list style names whose first level is numbered */
    OdfStyle* current;
    gchar* current_list;
    gint list_level;
} StylesParse;

static void styles_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                         const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    StylesParse* p = data;
    if (strcmp(name, "style:style") == 0) {
        const gchar* sname = zip_xml_attr(names, values, "name");
        if (sname == NULL) {
            return;
        }
        OdfStyle* s = g_new0(OdfStyle, 1);
        s->parent = g_strdup(zip_xml_attr(names, values, "parent-style-name"));
        const gchar* level = zip_xml_attr(names, values, "default-outline-level");
        s->heading = level != NULL ? atoi(level) : 0;
        /* Built-in names: "Title", "Subtitle", "Heading_20_1" */
        if (strcmp(sname, "Title") == 0) {
            s->heading = 1;
        } else if (strcmp(sname, "Subtitle") == 0) {
            s->heading = 2;
        }
        g_hash_table_replace(p->styles, g_strdup(sname), s);
        p->current = s;
    } else if (strcmp(name, "style:text-properties") == 0 && p->current != NULL) {
        const gchar* weight = zip_xml_attr(names, values, "font-weight");
        const gchar* style = zip_xml_attr(names, values, "font-style");
        const gchar* underline = zip_xml_attr(names, values, "text-underline-style");
        const gchar* strike = zip_xml_attr(names, values, "text-line-through-style");
        if (weight != NULL) {
            p->current->flags |= STYLE_BOLD_SET;
            if (strcmp(weight, "bold") == 0 || atoi(weight) >= 600) {
                p->current->flags |= STYLE_BOLD;
            }
        }
        if (style != NULL) {
            p->current->flags |= STYLE_ITALIC_SET;
            if (strcmp(style, "italic") == 0 || strcmp(style, "oblique") == 0) {
                p->current->flags |= STYLE_ITALIC;
            }
        }
        if (underline != NULL && strcmp(underline, "none") != 0) {
            p->current->flags |= STYLE_UNDERLINE;
        }
        if (strike != NULL && strcmp(strike, "none") != 0) {
            p->current->flags |= STYLE_STRIKE;
        }
    } else if (strcmp(name, "text:list-style") == 0) {
        g_free(p->current_list);
        p->current_list = g_strdup(zip_xml_attr(names, values, "name"));
    } else if (strcmp(name, "text:list-level-style-number") == 0 && p->current_list != NULL) {
        const gchar* level = zip_xml_attr(names, values, "level");
        if (level == NULL || strcmp(level, "1") == 0) {
            g_hash_table_add(p->numbered, g_strdup(p->current_list));
        }
    }
}

static void styles_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    StylesParse* p = data;
    if (strcmp(name, "style:style") == 0) {
        p->current = NULL;
    } else if (strcmp(name, "text:list-style") == 0) {
        g_clear_pointer(&p->current_list, g_free);
    }
}

/* Flags of a style with its parents' (a child's explicit setting wins) */
static guint resolve_flags(GHashTable* styles, const gchar* name, gint* heading) {
    guint flags = 0;
    guint set = 0;
    for (gint depth = 0; name != NULL && depth < 16; depth++) {
        OdfStyle* s = g_hash_table_lookup(styles, name);
        if (s == NULL) {
            break;
        }
        if (heading != NULL && *heading == 0 && s->heading > 0) {
            *heading = s->heading;
        }
        if (!(set & STYLE_BOLD_SET) && (s->flags & STYLE_BOLD_SET)) {
            set |= STYLE_BOLD_SET;
            flags |= s->flags & STYLE_BOLD;
        }
        if (!(set & STYLE_ITALIC_SET) && (s->flags & STYLE_ITALIC_SET)) {
            set |= STYLE_ITALIC_SET;
            flags |= s->flags & STYLE_ITALIC;
        }
        flags |= s->flags & (STYLE_UNDERLINE | STYLE_STRIKE);
        name = s->parent;
    }
    return flags;
}

/* --- Body --- */

typedef struct {
    ZipReader* zip;
    GHashTable* styles;
    GHashTable* numbered;
    GString* out;
    gsize max_text;
    gsize text_len;
    gboolean truncated;
    gint skip;                  /* depth inside skipped elements (notes, comments) */
    GPtrArray* closers;         /* closing tags of open elements, in order */
    gint list_depth;
    GPtrArray* list_closers;    /* "</ul>" / "</ol>" of open lists */
    gboolean in_title_frame;    /* presentation title frame */
    gboolean in_header_rows;
    gint cell_depth;            /* inside table cells: paragraphs without <p> */
    guint slides;
    gboolean presentation;
} BodyParse;

static void push_closer(BodyParse* b, const gchar* closer) {
    g_ptr_array_add(b->closers, g_strdup(closer));
}

static void pop_closer(BodyParse* b) {
    if (b->closers->len > 0) {
        gchar* closer = g_ptr_array_steal_index(b->closers, b->closers->len - 1);
        g_string_append(b->out, closer);
        g_free(closer);
    }
}

/* Opening tags for style flags; returns the matching closing tags */
static gchar* open_flags(BodyParse* b, guint flags) {
    GString* close = g_string_new(NULL);
    if (flags & STYLE_BOLD) {
        g_string_append(b->out, "<b>");
        g_string_prepend(close, "</b>");
    }
    if (flags & STYLE_ITALIC) {
        g_string_append(b->out, "<i>");
        g_string_prepend(close, "</i>");
    }
    if (flags & STYLE_UNDERLINE) {
        g_string_append(b->out, "<u>");
        g_string_prepend(close, "</u>");
    }
    if (flags & STYLE_STRIKE) {
        g_string_append(b->out, "<s>");
        g_string_prepend(close, "</s>");
    }
    return g_string_free(close, FALSE);
}

static const gchar* image_mime(const gchar* path) {
    const gchar* dot = strrchr(path, '.');
    if (dot == NULL) return "image/png";
    if (g_ascii_strcasecmp(dot, ".jpg") == 0 || g_ascii_strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (g_ascii_strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (g_ascii_strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (g_ascii_strcasecmp(dot, ".bmp") == 0) return "image/bmp";
    return "image/png";
}

static void append_image(BodyParse* b, const gchar* href) {
    if (href == NULL || strstr(href, "://") != NULL) {
        return;
    }
    gchar* part = zip_xml_resolve("", href);
    GBytes* bytes = zip_reader_read(b->zip, part, MAX_IMAGE, NULL);
    if (bytes != NULL) {
        gchar* b64 = g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
        g_string_append_printf(b->out, "<img src=\"data:%s;base64,%s\">", image_mime(part), b64);
        g_free(b64);
        g_bytes_unref(bytes);
    }
    g_free(part);
}

static void body_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    BodyParse* b = data;
    if (b->skip > 0 || b->truncated) {
        b->skip += b->skip > 0;
        return;
    }
    if (strcmp(name, "text:note") == 0 || strcmp(name, "office:annotation") == 0 ||
        strcmp(name, "text:tracked-changes") == 0 || strcmp(name, "presentation:notes") == 0 ||
        strcmp(name, "text:sequence-decls") == 0 || strcmp(name, "office:forms") == 0) {
        b->skip = 1;
        return;
    }
    const gchar* style = zip_xml_attr(names, values, "style-name");
    if (strcmp(name, "text:h") == 0) {
        const gchar* level = zip_xml_attr(names, values, "outline-level");
        gint n = CLAMP(level != NULL ? atoi(level) : 1, 1, 6);
        g_string_append_printf(b->out, "<h%d>", n);
        gchar* closer = g_strdup_printf("</h%d>\n", n);
        push_closer(b, closer);
        g_free(closer);
    } else if (strcmp(name, "text:p") == 0) {
        gint heading = 0;
        guint flags = resolve_flags(b->styles, style, &heading);
        if (b->in_title_frame) {
            heading = 3;
        }
        gboolean inline_par = b->list_depth > 0 || b->cell_depth > 0;
        if (heading > 0 && !inline_par) {
            gint n = CLAMP(heading, 1, 6);
            g_string_append_printf(b->out, "<h%d>", n);
            gchar* closer = g_strdup_printf("</h%d>\n", n);
            push_closer(b, closer);
            g_free(closer);
        } else {
            g_string_append(b->out, inline_par ? "" : "<p>");
            gchar* inner = open_flags(b, flags);
            gchar* closer = g_strconcat(inner, inline_par ? " " : "</p>\n", NULL);
            push_closer(b, closer);
            g_free(closer);
            g_free(inner);
        }
    } else if (strcmp(name, "text:span") == 0) {
        gchar* closer = open_flags(b, resolve_flags(b->styles, style, NULL));
        push_closer(b, closer);
        g_free(closer);
    } else if (strcmp(name, "text:a") == 0) {
        const gchar* href = zip_xml_attr(names, values, "href");
        gchar* escaped = g_markup_escape_text(href != NULL ? href : "", -1);
        g_string_append_printf(b->out, "<a href=\"%s\">", escaped);
        g_free(escaped);
        push_closer(b, "</a>");
    } else if (strcmp(name, "text:list") == 0) {
        gboolean numbered = style != NULL && g_hash_table_contains(b->numbered, style);
        if (b->list_depth > 0 && b->list_closers->len > 0) {
            numbered = g_str_has_prefix(g_ptr_array_index(b->list_closers, b->list_closers->len - 1), "</ol");
            numbered = numbered || (style != NULL && g_hash_table_contains(b->numbered, style));
        }
        g_string_append(b->out, numbered ? "<ol>\n" : "<ul>\n");
        g_ptr_array_add(b->list_closers, g_strdup(numbered ? "</ol>\n" : "</ul>\n"));
        b->list_depth++;
        push_closer(b, "");
    } else if (strcmp(name, "text:list-item") == 0 || strcmp(name, "text:list-header") == 0) {
        g_string_append(b->out, "<li>");
        push_closer(b, "</li>\n");
    } else if (strcmp(name, "table:table") == 0) {
        g_string_append(b->out, "<table>\n");
        push_closer(b, "</table>\n");
    } else if (strcmp(name, "table:table-header-rows") == 0) {
        b->in_header_rows = TRUE;
        push_closer(b, "");
    } else if (strcmp(name, "table:table-row") == 0) {
        g_string_append(b->out, "<tr>");
        push_closer(b, "</tr>\n");
    } else if (strcmp(name, "table:table-cell") == 0) {
        g_string_append(b->out, b->in_header_rows ? "<th>" : "<td>");
        push_closer(b, b->in_header_rows ? "</th>" : "</td>");
        b->cell_depth++;
    } else if (strcmp(name, "draw:image") == 0) {
        append_image(b, zip_xml_attr(names, values, "href"));
        push_closer(b, "");
    } else if (strcmp(name, "draw:page") == 0) {
        b->slides++;
        gchar* slide = g_strdup_printf(_("Slide %u"), b->slides);
        g_string_append_printf(b->out, "%s<h2>%s</h2>\n", b->slides > 1 ? "<hr>\n" : "", slide);
        g_free(slide);
        push_closer(b, "");
    } else if (strcmp(name, "draw:frame") == 0) {
        const gchar* cls = zip_xml_attr(names, values, "class");
        b->in_title_frame = cls != NULL && (strcmp(cls, "title") == 0 || strcmp(cls, "subtitle") == 0);
        push_closer(b, "");
    } else if (strcmp(name, "text:s") == 0) {
        const gchar* c = zip_xml_attr(names, values, "c");
        for (gint i = 0; i < (c != NULL ? MIN(atoi(c), 100) : 1); i++) {
            g_string_append(b->out, "&#160;");
        }
        push_closer(b, "");
    } else if (strcmp(name, "text:tab") == 0) {
        g_string_append(b->out, "&#160;&#160;&#160;&#160;");
        push_closer(b, "");
    } else if (strcmp(name, "text:line-break") == 0) {
        g_string_append(b->out, "<br>");
        push_closer(b, "");
    } else {
        push_closer(b, "");
    }
}

static void body_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    BodyParse* b = data;
    if (b->skip > 0) {
        b->skip--;
        return;
    }
    if (b->truncated) {
        return;
    }
    if (strcmp(name, "text:list") == 0 && b->list_closers->len > 0) {
        gchar* closer = g_ptr_array_steal_index(b->list_closers, b->list_closers->len - 1);
        g_string_append(b->out, closer);
        g_free(closer);
        b->list_depth--;
    } else if (strcmp(name, "table:table-header-rows") == 0) {
        b->in_header_rows = FALSE;
    } else if (strcmp(name, "draw:frame") == 0) {
        b->in_title_frame = FALSE;
    } else if (strcmp(name, "table:table-cell") == 0) {
        b->cell_depth--;
    }
    pop_closer(b);
}

static void body_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    BodyParse* b = data;
    if (b->skip > 0 || b->truncated) {
        return;
    }
    /* Text between elements of the body is not content (formatting whitespace) */
    const gchar* element = g_markup_parse_context_get_element(ctx);
    if (element == NULL || (strncmp(element, "text:", 5) != 0)) {
        return;
    }
    gchar* escaped = g_markup_escape_text(text, (gssize)len);
    g_string_append(b->out, escaped);
    g_free(escaped);
    b->text_len += len;
    if (b->text_len > b->max_text) {
        b->truncated = TRUE;
        /* Close what is open so the HTML stays balanced */
        while (b->closers->len > 0) {
            pop_closer(b);
        }
        zip_xml_stop(error);
    }
}

/* --- Metadata --- */

typedef struct {
    GString* title;
    GString* creator;
    GString* current;
} MetaParse;

static void meta_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)names; (void)values; (void)error;
    MetaParse* m = data;
    if (strcmp(name, "dc:title") == 0) {
        m->current = m->title;
    } else if (strcmp(name, "dc:creator") == 0 || (strcmp(name, "meta:initial-creator") == 0 && m->creator->len == 0)) {
        g_string_truncate(m->creator, 0);
        m->current = m->creator;
    }
}

static void meta_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)name; (void)error;
    ((MetaParse*)data)->current = NULL;
}

static void meta_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    MetaParse* m = data;
    if (m->current != NULL) {
        g_string_append_len(m->current, text, (gssize)len);
    }
}

gchar* odf_file_to_html(const gchar* path, gsize max_text, OdfInfo* info, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    if (!zip_reader_has(zip, "content.xml")) {
        zip_reader_free(zip);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an OpenDocument file (no content.xml)"));
        return NULL;
    }
    StylesParse sp = { g_hash_table_new_full(g_str_hash, g_str_equal, g_free, odf_style_free),
                       g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL), NULL, NULL, 0 };
    static const GMarkupParser styles_parser = { styles_start, styles_end, NULL, NULL, NULL };
    zip_xml_parse(zip, "styles.xml", MAX_PART, TRUE, &styles_parser, &sp, NULL);
    /* Automatic styles of content.xml override the named ones */
    zip_xml_parse(zip, "content.xml", MAX_PART, FALSE, &styles_parser, &sp, NULL);

    BodyParse b = { zip, sp.styles, sp.numbered, g_string_new(NULL), max_text, 0, FALSE, 0,
                    g_ptr_array_new_with_free_func(g_free), 0, g_ptr_array_new_with_free_func(g_free),
                    FALSE, FALSE, 0, 0, FALSE };
    static const GMarkupParser body_parser = { body_start, body_end, body_text, NULL, NULL };
    gchar* html = NULL;
    if (zip_xml_parse(zip, "content.xml", MAX_PART, FALSE, &body_parser, &b, error)) {
        MetaParse m = { g_string_new(NULL), g_string_new(NULL), NULL };
        static const GMarkupParser meta_parser = { meta_start, meta_end, meta_text, NULL, NULL };
        zip_xml_parse(zip, "meta.xml", 1024 * 1024, TRUE, &meta_parser, &m, NULL);
        GString* doc = g_string_new("<html><head>");
        g_strstrip(m.title->str);
        g_strstrip(m.creator->str);
        if (m.title->str[0] != '\0') {
            gchar* t = g_markup_escape_text(m.title->str, -1);
            g_string_append_printf(doc, "<title>%s</title>", t);
            g_free(t);
        }
        if (m.creator->str[0] != '\0') {
            gchar* a = g_markup_escape_text(m.creator->str, -1);
            g_string_append_printf(doc, "<meta name=\"author\" content=\"%s\">", a);
            g_free(a);
        }
        g_string_append(doc, "</head><body>\n");
        g_string_append_len(doc, b.out->str, (gssize)b.out->len);
        if (b.truncated) {
            g_string_append_printf(doc, "<hr>\n<p><i>%s</i></p>\n", _("Document truncated: only the beginning is shown."));
        }
        g_string_append(doc, "</body></html>\n");
        html = g_string_free(doc, FALSE);
        g_string_free(m.title, TRUE);
        g_string_free(m.creator, TRUE);
        if (info != NULL) {
            info->n_slides = b.slides;
            info->presentation = b.slides > 0;
        }
    }
    g_string_free(b.out, TRUE);
    g_ptr_array_free(b.closers, TRUE);
    g_ptr_array_free(b.list_closers, TRUE);
    g_hash_table_destroy(sp.styles);
    g_hash_table_destroy(sp.numbered);
    g_free(sp.current_list);
    zip_reader_free(zip);
    return html;
}
