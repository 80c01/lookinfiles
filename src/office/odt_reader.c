/* OpenDocument text drawn page by page (see odt_reader.h) */
#include "office/odt_reader.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/zip_xml.h"
#include "office/doc_pages.h"
#include "office/doc_writer.h"
#include "office/odp_styles.h"

#define MAX_PART (64u * 1024 * 1024)
#define MAX_IMAGE (16u * 1024 * 1024)         /* one picture */
#define MAX_IMAGES_TOTAL (48u * 1024 * 1024)  /* all the pictures */
#define MAX_STORY_TEXT 4096                   /* header, footer */
#define MAX_REPEAT 64                         /* repeated cells */
#define DEFAULT_SIZE 12.0
#define DEFAULT_LABEL_WIDTH 18.0              /* list labels without a width */
#define LINE_BREAK "\xE2\x80\xA8"

typedef struct {
    ZipReader* zip;
    const OdpStyles* styles;
    DocWriter* w;
    gsize max_text;
    gboolean truncated;
    gsize* image_bytes;
    gdouble default_size;
    gboolean last_space;      /* collapse white space: a space written last */
} Ctx;

typedef struct {
    const OdpList* list;
    gint level;               /* 1-based, 0: not in a list */
    gboolean labelled;        /* first paragraph of a list item */
    gint counters[ODP_LIST_LEVELS];
} ListState;

static gboolean full(const Ctx* c) {
    return c->truncated;
}

/* ---- Runs ---- */

/* Run properties from a cascade; *font_out is the family to free */
static void style_run(const Ctx* ctx, const OdpCascade* c, gboolean link, DocRun* r, gchar** font_out) {
    doc_run_init(r);
    gboolean is_family = FALSE;
    const gchar* font = odp_cascade_get_either(c, "style:font-name", "fo:font-family", &is_family);
    *font_out = is_family ? odp_first_family(font) : g_strdup(odp_styles_font(ctx->styles, font));
    r->font = *font_out;
    r->size = odp_cascade_font_size(c, ctx->default_size);
    const gchar* weight = odp_cascade_get(c, "fo:font-weight");
    r->bold = weight != NULL && (strcmp(weight, "bold") == 0 || atoi(weight) >= 600);
    const gchar* style = odp_cascade_get(c, "fo:font-style");
    r->italic = style != NULL && (strcmp(style, "italic") == 0 || strcmp(style, "oblique") == 0);
    const gchar* under = odp_cascade_get(c, "style:text-underline-style");
    r->underline = under != NULL && strcmp(under, "none") != 0;
    const gchar* strike = odp_cascade_get(c, "style:text-line-through-style");
    r->strike = strike != NULL && strcmp(strike, "none") != 0;
    const gchar* transform = odp_cascade_get(c, "fo:text-transform");
    const gchar* variant = odp_cascade_get(c, "fo:font-variant");
    r->caps = g_strcmp0(transform, "uppercase") == 0 || g_strcmp0(variant, "small-caps") == 0;
    const gchar* pos = odp_cascade_get(c, "style:text-position");
    if (pos != NULL) {
        if (g_str_has_prefix(pos, "super") || g_ascii_strtod(pos, NULL) > 0) r->vert = 1;
        else if (g_str_has_prefix(pos, "sub") || g_ascii_strtod(pos, NULL) < 0) r->vert = -1;
    }
    gboolean automatic = FALSE;
    const gchar* color = odp_cascade_get_either(c, "fo:color", "style:use-window-font-color", &automatic);
    if (color != NULL && !automatic) {
        r->color = odp_color(color, DOC_COLOR_NONE);
    }
    const gchar* bg = odp_cascade_get(c, "fo:background-color");
    if (bg != NULL && strcmp(bg, "transparent") != 0) {
        r->highlight = odp_color(bg, DOC_COLOR_NONE);
    }
    r->link = link;
}

static void write_text(Ctx* ctx, const OdpCascade* c, const gchar* text, gboolean collapse, gboolean link) {
    if (full(ctx)) {
        return;
    }
    GString* s = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p++) {
        gboolean space = *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r';
        if (collapse && space) {
            if (!ctx->last_space) {
                g_string_append_c(s, ' ');
            }
            ctx->last_space = TRUE;
        } else {
            g_string_append_c(s, *p);
            ctx->last_space = FALSE;
        }
    }
    if (s->len > 0) {
        DocRun run;
        gchar* font = NULL;
        style_run(ctx, c, link, &run, &font);
        doc_writer_text(ctx->w, s->str, (gssize)s->len, &run);
        g_free(font);
        if (doc_writer_length(ctx->w) >= ctx->max_text) {
            ctx->truncated = TRUE;
        }
    }
    g_string_free(s, TRUE);
}

static void write_picture(Ctx* ctx, const OdpNode* frame) {
    const OdpNode* image = odp_node_child(frame, "draw:image");
    const gchar* href = odp_node_attr(image, "xlink:href");
    if (href == NULL) {
        return;
    }
    gdouble width = odp_length(odp_node_attr(frame, "svg:width"), 0);
    GBytes* bytes = NULL;
    if (strstr(href, "://") == NULL) {
        gchar* part = zip_xml_resolve("", href);
        gint64 size = zip_reader_get_size(ctx->zip, part);
        if (size > 0 && *ctx->image_bytes + (gsize)size <= MAX_IMAGES_TOTAL) {
            bytes = zip_reader_read(ctx->zip, part, MAX_IMAGE, NULL);
            *ctx->image_bytes += bytes != NULL ? g_bytes_get_size(bytes) : 0;
        }
        g_free(part);
    }
    const gchar* slash = strrchr(href, '/');
    doc_writer_image(ctx->w, bytes, width, slash != NULL ? slash + 1 : href);
    if (bytes != NULL) {
        g_bytes_unref(bytes);
    }
    ctx->last_space = FALSE;
}

static void write_inline(Ctx* ctx, const OdpNode* node, const OdpCascade* c, gboolean link) {
    for (guint i = 0; i < node->children->len && !full(ctx); i++) {
        const OdpNode* n = g_ptr_array_index(node->children, i);
        if (n->name == NULL) {
            write_text(ctx, c, n->text, TRUE, link);
        } else if (odp_node_is(n, "text:span")) {
            OdpCascade span = { { 0 }, 0 };
            odp_cascade_add(&span, ctx->styles, "text", odp_node_attr(n, "text:style-name"));
            odp_cascade_append(&span, c);
            write_inline(ctx, n, &span, link);
        } else if (odp_node_is(n, "text:a")) {
            write_inline(ctx, n, c, TRUE);
        } else if (odp_node_is(n, "text:s")) {
            const gchar* count = odp_node_attr(n, "text:c");
            gint k = CLAMP(count != NULL ? atoi(count) : 1, 1, 100);
            gchar* spaces = g_strnfill((gsize)k, ' ');
            write_text(ctx, c, spaces, FALSE, link);
            g_free(spaces);
        } else if (odp_node_is(n, "text:tab")) {
            write_text(ctx, c, "\t", FALSE, link);
        } else if (odp_node_is(n, "text:line-break")) {
            write_text(ctx, c, LINE_BREAK, FALSE, link);
            ctx->last_space = TRUE;
        } else if (odp_node_is(n, "draw:frame")) {
            if (odp_node_child(n, "draw:image") != NULL) {
                write_picture(ctx, n);
            } else {
                write_inline(ctx, n, c, link); /* text boxes: their text in the line */
            }
        } else if (odp_node_is(n, "text:p") || odp_node_is(n, "text:h")) {
            /* A paragraph of a text box */
            if (!ctx->last_space) {
                write_text(ctx, c, " ", FALSE, link);
                ctx->last_space = TRUE;
            }
            OdpCascade inner = { { 0 }, 0 };
            odp_cascade_add(&inner, ctx->styles, "paragraph", odp_node_attr(n, "text:style-name"));
            odp_cascade_append(&inner, c);
            write_inline(ctx, n, &inner, link);
        } else if (odp_node_is(n, "text:note") || odp_node_is(n, "office:annotation") ||
                   odp_node_is(n, "office:annotation-end") || odp_node_is(n, "text:soft-page-break") ||
                   odp_node_is(n, "draw:custom-shape") || odp_node_is(n, "text:tracked-changes")) {
            continue;
        } else {
            write_inline(ctx, n, c, link); /* fields, bookmarks, text:sequence... */
        }
    }
}

/* ---- Paragraphs and lists ---- */

static PreviewAlign parse_align(const gchar* a) {
    if (a == NULL) return PREVIEW_ALIGN_LEFT;
    if (strcmp(a, "center") == 0) return PREVIEW_ALIGN_CENTER;
    if (strcmp(a, "end") == 0 || strcmp(a, "right") == 0) return PREVIEW_ALIGN_RIGHT;
    if (strcmp(a, "justify") == 0) return PREVIEW_ALIGN_JUSTIFY;
    return PREVIEW_ALIGN_LEFT;
}

static gboolean symbol_font(const gchar* font) {
    return font != NULL && (g_ascii_strcasecmp(font, "StarSymbol") == 0 || g_ascii_strcasecmp(font, "OpenSymbol") == 0);
}

/* Label of the list item the paragraph starts (NULL: none), its indents
 * set on para */
static gchar* list_label(ListState* ls, DocPara* para, gdouble margin, gchar** label_font) {
    *label_font = NULL;
    if (ls->level == 0 || !ls->labelled) {
        return NULL;
    }
    gint lv = MIN(ls->level, ODP_LIST_LEVELS) - 1;
    const OdpListLevel* l = ls->list != NULL && ls->list->levels[lv].set ? &ls->list->levels[lv] : NULL;
    gchar* label = NULL;
    if (l != NULL && l->numbered) {
        ls->counters[lv] = ls->counters[lv] == 0 ? (l->start > 0 ? l->start : 1) : ls->counters[lv] + 1;
        label = odp_list_label(l, ls->counters[lv]);
    } else if (l == NULL || l->bullet != NULL) {
        label = g_strdup(l != NULL ? l->bullet : "\xE2\x80\xA2");
        if (l != NULL && !symbol_font(l->font)) {
            *label_font = g_strdup(l->font);
        }
    }
    if (l != NULL && l->label_alignment) {
        para->left = l->margin_left;
        para->first = l->text_indent;
    } else {
        gdouble width = l != NULL && l->min_label_width > 0 ? l->min_label_width : DEFAULT_LABEL_WIDTH;
        para->left = margin + (l != NULL ? l->space_before : 0) + width;
        para->first = -width;
    }
    /* Deeper levels restart their numbering */
    for (gint k = lv + 1; k < ODP_LIST_LEVELS; k++) {
        ls->counters[k] = 0;
    }
    return label;
}

static void write_paragraph(Ctx* ctx, const OdpNode* node, ListState* ls, gboolean body) {
    OdpCascade pc = { { 0 }, 0 };
    odp_cascade_add(&pc, ctx->styles, "paragraph", odp_node_attr(node, "text:style-name"));
    odp_cascade_add_default(&pc, ctx->styles, "paragraph");
    if (body && g_strcmp0(odp_cascade_get_explicit(&pc, "fo:break-before"), "page") == 0) {
        doc_writer_page_break(ctx->w);
    }
    DocPara para;
    doc_para_init(&para);
    para.align = parse_align(odp_cascade_get(&pc, "fo:text-align"));
    gdouble margin = odp_length(odp_cascade_get(&pc, "fo:margin-left"), 0);
    para.left = margin;
    para.right = odp_length(odp_cascade_get(&pc, "fo:margin-right"), 0);
    para.first = odp_length(odp_cascade_get(&pc, "fo:text-indent"), 0);
    para.before = odp_length(odp_cascade_get(&pc, "fo:margin-top"), 0);
    para.after = odp_length(odp_cascade_get(&pc, "fo:margin-bottom"), 0);
    if (odp_node_is(node, "text:h")) {
        const gchar* level = odp_node_attr(node, "text:outline-level");
        para.outline = CLAMP(level != NULL ? atoi(level) : 1, 1, 9);
    }
    gchar* label_font = NULL;
    gchar* label = list_label(ls, &para, margin, &label_font);
    ctx->last_space = TRUE;
    write_inline(ctx, node, &pc, FALSE);
    DocRun label_run;
    gchar* font = NULL;
    style_run(ctx, &pc, FALSE, &label_run, &font);
    if (label_font != NULL) {
        label_run.font = label_font;
    }
    doc_writer_end_paragraph(ctx->w, &para, label, &label_run);
    g_free(font);
    g_free(label_font);
    g_free(label);
    if (body && g_strcmp0(odp_cascade_get_explicit(&pc, "fo:break-after"), "page") == 0) {
        doc_writer_page_break(ctx->w);
    }
}

static void write_blocks(Ctx* ctx, const OdpNode* container, ListState* ls, gboolean body);

static void write_list(Ctx* ctx, const OdpNode* n, ListState* ls, gboolean body) {
    ListState inner = *ls;
    const OdpList* explicit = odp_styles_list(ctx->styles, odp_node_attr(n, "text:style-name"));
    if (explicit != NULL) {
        inner.list = explicit;
    }
    inner.level = ls->level + 1;
    /* A new list numbers from its start unless it continues */
    if (g_strcmp0(odp_node_attr(n, "text:continue-numbering"), "true") != 0 &&
        odp_node_attr(n, "text:continue-list") == NULL) {
        for (gint k = MIN(inner.level, ODP_LIST_LEVELS) - 1; k < ODP_LIST_LEVELS; k++) {
            inner.counters[k] = 0;
        }
    }
    for (guint k = 0; k < n->children->len && !full(ctx); k++) {
        const OdpNode* item = g_ptr_array_index(n->children, k);
        gboolean header = odp_node_is(item, "text:list-header");
        if (!header && !odp_node_is(item, "text:list-item")) {
            continue;
        }
        const gchar* start = odp_node_attr(item, "text:start-value");
        if (start != NULL) {
            inner.counters[MIN(inner.level, ODP_LIST_LEVELS) - 1] = atoi(start) - 1;
        }
        inner.labelled = !header;
        write_blocks(ctx, item, &inner, body);
    }
    memcpy(ls->counters, inner.counters, sizeof ls->counters);
}

static guint attr_count(const OdpNode* n, const gchar* attr) {
    const gchar* v = odp_node_attr(n, attr);
    return v != NULL ? (guint)CLAMP(atoi(v), 1, MAX_REPEAT) : 1;
}

static void write_rows(Ctx* ctx, const OdpNode* group, gboolean header) {
    for (guint r = 0; r < group->children->len && !full(ctx); r++) {
        const OdpNode* row = g_ptr_array_index(group->children, r);
        if (odp_node_is(row, "table:table-header-rows")) {
            write_rows(ctx, row, TRUE);
            continue;
        }
        if (odp_node_is(row, "table:table-rows") || odp_node_is(row, "table:table-row-group")) {
            write_rows(ctx, row, header);
            continue;
        }
        if (!odp_node_is(row, "table:table-row")) {
            continue;
        }
        doc_writer_row_start(ctx->w, header);
        for (guint c = 0; c < row->children->len; c++) {
            const OdpNode* cell = g_ptr_array_index(row->children, c);
            if (!odp_node_is(cell, "table:table-cell")) {
                continue; /* table:covered-table-cell: part of a spanned cell */
            }
            guint repeat = attr_count(cell, "table:number-columns-repeated");
            for (guint k = 0; k < repeat; k++) {
                doc_writer_cell_start(ctx->w, attr_count(cell, "table:number-columns-spanned"));
                ListState none = { NULL, 0, FALSE, { 0 } };
                write_blocks(ctx, cell, &none, FALSE);
                doc_writer_cell_end(ctx->w);
            }
        }
        doc_writer_row_end(ctx->w);
    }
}

static void write_table(Ctx* ctx, const OdpNode* table) {
    doc_writer_table_start(ctx->w);
    write_rows(ctx, table, FALSE);
    doc_writer_table_end(ctx->w);
}

static void write_blocks(Ctx* ctx, const OdpNode* container, ListState* ls, gboolean body) {
    gboolean first = TRUE;
    for (guint i = 0; container != NULL && i < container->children->len && !full(ctx); i++) {
        const OdpNode* n = g_ptr_array_index(container->children, i);
        if (n->name == NULL) {
            continue;
        }
        if (odp_node_is(n, "text:p") || odp_node_is(n, "text:h")) {
            ListState para = *ls;
            para.labelled = ls->labelled && first;
            write_paragraph(ctx, n, &para, body);
            memcpy(ls->counters, para.counters, sizeof ls->counters);
            first = FALSE;
        } else if (odp_node_is(n, "text:list")) {
            write_list(ctx, n, ls, body);
            first = FALSE;
        } else if (odp_node_is(n, "table:table")) {
            write_table(ctx, n);
            first = FALSE;
        } else if (odp_node_is(n, "text:section") || odp_node_is(n, "text:index-body") ||
                   odp_node_is(n, "text:table-of-content") || odp_node_is(n, "text:alphabetical-index") ||
                   odp_node_is(n, "text:illustration-index") || odp_node_is(n, "text:table-index") ||
                   odp_node_is(n, "text:user-index") || odp_node_is(n, "text:bibliography") ||
                   odp_node_is(n, "text:object-index")) {
            write_blocks(ctx, n, ls, body);
        }
        /* Index sources, sequence declarations, forms: nothing shown */
    }
}

/* ---- Document ---- */

static gchar* meta_text(const OdpNode* meta, const gchar* name) {
    const OdpNode* n = meta != NULL ? odp_node_find(meta, name) : NULL;
    gchar* text = n != NULL ? odp_node_text(n) : NULL;
    if (text != NULL) {
        g_strstrip(text);
        if (text[0] == '\0') {
            g_clear_pointer(&text, g_free);
        }
    }
    return text;
}

/* The master page of the text: "Standard", else the first one */
static const gchar* master_name(const OdpStyles* styles) {
    if (g_hash_table_contains(styles->masters, "Standard")) {
        return "Standard";
    }
    return styles->first_master;
}

/* Header or footer of the master page */
static DocModel* read_story(Ctx* base, const OdpNode* master, const gchar* element, const gchar* font) {
    const OdpNode* story = master != NULL ? odp_node_child(master, element) : NULL;
    if (story == NULL || g_strcmp0(odp_node_attr(story, "style:display"), "false") == 0) {
        return NULL;
    }
    Ctx ctx = *base;
    ctx.w = doc_writer_new(font, base->default_size);
    ctx.max_text = MAX_STORY_TEXT;
    ctx.truncated = FALSE;
    ListState none = { NULL, 0, FALSE, { 0 } };
    write_blocks(&ctx, story, &none, FALSE);
    return doc_writer_finish_model(ctx.w);
}

void odt_document_free(OdtDocument* doc) {
    if (doc == NULL) {
        return;
    }
    doc_model_free(doc->body);
    doc_model_free(doc->header);
    doc_model_free(doc->footer);
    g_free(doc->title);
    g_free(doc->author);
    g_free(doc);
}

OdtDocument* odt_reader_read(ZipReader* zip, gsize max_text, GError** error) {
    if (!zip_reader_has(zip, "content.xml")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an OpenDocument file (no content.xml)"));
        return NULL;
    }
    OdpNode* content = odp_node_parse(zip, "content.xml", MAX_PART, FALSE, error);
    if (content == NULL) {
        return NULL;
    }
    OdpNode* styles_root = odp_node_parse(zip, "styles.xml", MAX_PART, TRUE, NULL);
    OdpNode* meta = odp_node_parse(zip, "meta.xml", 1024 * 1024, TRUE, NULL);
    OdpStyles* styles = odp_styles_new();
    if (styles_root != NULL) {
        odp_styles_add(styles, styles_root);
    }
    odp_styles_add(styles, content);

    /* Normal text: the default paragraph style */
    OdpCascade normal = { { 0 }, 0 };
    odp_cascade_add(&normal, styles, "paragraph", "Standard");
    odp_cascade_add_default(&normal, styles, "paragraph");
    gboolean is_family = FALSE;
    const gchar* face = odp_cascade_get_either(&normal, "style:font-name", "fo:font-family", &is_family);
    gchar* font = is_family ? odp_first_family(face) : g_strdup(odp_styles_font(styles, face));
    gsize image_bytes = 0;
    Ctx ctx = { zip, styles, NULL, MAX(max_text, 1), FALSE, &image_bytes, odp_cascade_font_size(&normal, DEFAULT_SIZE),
                TRUE };

    OdtDocument* doc = g_new0(OdtDocument, 1);
    const gchar* master = master_name(styles);
    doc_page_setup_init(&doc->page);
    gdouble margins[4];
    odp_styles_page_size(styles, master, &doc->page.width, &doc->page.height);
    if (odp_styles_page_margins(styles, master, margins)) {
        /* The header and footer are inside the margins */
        doc->page.top = doc->page.header = margins[0];
        doc->page.bottom = doc->page.footer = margins[1];
        doc->page.left = margins[2];
        doc->page.right = margins[3];
    }
    const OdpNode* master_node = master != NULL ? g_hash_table_lookup(styles->masters, master) : NULL;
    doc->header = read_story(&ctx, master_node, "style:header", font);
    doc->footer = read_story(&ctx, master_node, "style:footer", font);

    ctx.w = doc_writer_new(font, ctx.default_size);
    ListState none = { NULL, 0, FALSE, { 0 } };
    write_blocks(&ctx, odp_node_find(content, "office:text"), &none, TRUE);
    doc->body = doc_writer_finish_model(ctx.w);
    doc->truncated = ctx.truncated;
    doc->title = meta_text(meta, "dc:title");
    doc->author = meta_text(meta, "dc:creator");
    if (doc->author == NULL) {
        doc->author = meta_text(meta, "meta:initial-creator");
    }
    g_free(font);
    odp_styles_free(styles);
    odp_node_free(meta);
    odp_node_free(styles_root);
    odp_node_free(content);
    return doc;
}

PreviewDoc* odt_reader_pages(ZipReader* zip, const gchar* name, const gchar* size_text, gsize max_text,
                             gdouble zoom, GError** error) {
    OdtDocument* odt = odt_reader_read(zip, max_text, error);
    if (odt == NULL) {
        return NULL;
    }
    GString* subtitle = g_string_new(_("OpenDocument text"));
    if (odt->author != NULL) {
        g_string_append_printf(subtitle, " · %s", odt->author);
    }
    if (size_text != NULL) {
        g_string_append_printf(subtitle, " · %s", size_text);
    }
    PreviewDoc* doc = doc_pages_preview(odt->title != NULL ? odt->title : (name != NULL ? name : ""), subtitle->str,
                                        &odt->page, odt->header, odt->body, odt->footer,
                                        odt->truncated ? _("Document truncated: only the beginning is shown.") : NULL,
                                        zoom);
    g_string_free(subtitle, TRUE);
    odt_document_free(odt);
    return doc;
}

PreviewDoc* odt_file_pages(const gchar* path, const gchar* name, const gchar* size_text, gsize max_text,
                           gdouble zoom, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    PreviewDoc* doc = odt_reader_pages(zip, name, size_text, max_text, zoom, error);
    zip_reader_free(zip);
    return doc;
}
