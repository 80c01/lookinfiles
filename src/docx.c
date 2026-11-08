/* Word documents (.docx) shown like Word (see docx.h) */
#include "docx.h"
#include "i18n.h"

#include <gio/gio.h>
#include <string.h>

#include "doc_writer.h"
#include "docx_styles.h"
#include "zip_xml.h"

#define MAX_PART (16u * 1024 * 1024)          /* relationships, headers, properties */
#define MAX_IMAGE (16u * 1024 * 1024)         /* one embedded image */
#define MAX_IMAGES_TOTAL (48u * 1024 * 1024)  /* all embedded images */
#define MAX_HEADER_TEXT 4096
#define EMU_PER_PT 12700.0
#define LINE_BREAK "\xE2\x80\xA8"             /* U+2028: a new line in the same paragraph */

static const gchar* attr(const gchar** names, const gchar** values, const gchar* key) {
    for (gsize i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], key) == 0) {
            return values[i];
        }
    }
    return NULL;
}

static gboolean attr_on(const gchar** names, const gchar** values) {
    const gchar* v = attr(names, values, "w:val");
    return v == NULL || !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0);
}

/* ---- Relationships (_rels/<part>.rels) ----------------------------------- */

typedef struct {
    gchar* type;
    gchar* target;
    gboolean external;
} Rel;

static void rel_free(gpointer data) {
    Rel* r = data;
    g_free(r->type);
    g_free(r->target);
    g_free(r);
}

static void rels_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    if (strcmp(name, "Relationship") != 0) {
        return;
    }
    const gchar* id = attr(names, values, "Id");
    const gchar* target = attr(names, values, "Target");
    if (id == NULL || target == NULL) {
        return;
    }
    Rel* r = g_new0(Rel, 1);
    r->type = g_strdup(attr(names, values, "Type"));
    r->target = g_strdup(target);
    r->external = g_strcmp0(attr(names, values, "TargetMode"), "External") == 0;
    g_hash_table_replace(data, g_strdup(id), r);
}

/* Relationships of a part (empty table if it has none) */
static GHashTable* read_rels(ZipReader* zip, const gchar* part) {
    GHashTable* rels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, rel_free);
    gchar* dir = zip_xml_dirname(part);
    gchar* base = g_path_get_basename(part);
    gchar* rels_name = dir[0] == '\0' ? g_strconcat("_rels/", base, ".rels", NULL)
                                      : g_strconcat(dir, "/_rels/", base, ".rels", NULL);
    static const GMarkupParser parser = { rels_start, NULL, NULL, NULL, NULL };
    zip_xml_parse(zip, rels_name, MAX_PART, TRUE, &parser, rels, NULL);
    g_free(rels_name);
    g_free(base);
    g_free(dir);
    return rels;
}

/* Target part of the first internal relationship of this type ("/styles") */
static gchar* rel_part(GHashTable* rels, const gchar* dir, const gchar* type_suffix) {
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, rels);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        const Rel* r = value;
        if (!r->external && r->type != NULL && g_str_has_suffix(r->type, type_suffix)) {
            return zip_xml_resolve(dir, r->target);
        }
    }
    return NULL;
}

/* Main document part named by the package relationships */
static gchar* main_part_name(ZipReader* zip) {
    GHashTable* rels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, rel_free);
    static const GMarkupParser parser = { rels_start, NULL, NULL, NULL, NULL };
    zip_xml_parse(zip, "_rels/.rels", MAX_PART, TRUE, &parser, rels, NULL);
    gchar* main = rel_part(rels, "", "/officeDocument");
    g_hash_table_destroy(rels);
    if (main == NULL || !zip_reader_has(zip, main)) {
        g_free(main);
        main = zip_reader_has(zip, "word/document.xml") ? g_strdup("word/document.xml") : NULL;
    }
    return main;
}

gboolean docx_is_document(ZipReader* zip) {
    gchar* main = main_part_name(zip);
    gboolean ok = (main != NULL);
    g_free(main);
    return ok;
}

/* ---- Document, headers and footers ---------------------------------------- */

typedef struct {
    ZipReader* zip;
    gchar* part_dir;          /* directory of the part ("word") */
    GHashTable* rels;         /* of the part */
    DocxStyles* styles;
    DocxNumbering* num;
    DocWriter* w;
    gsize max_text;
    gboolean truncated;
    gsize* image_bytes;

    gint skip_depth;          /* > 0 inside an ignored element */
    gint drawing_depth;       /* > 0 inside a picture */
    gchar* img_rid;
    gchar* img_name;
    gdouble img_width;        /* points */

    /* Paragraph */
    gboolean in_p;
    gboolean in_ppr;
    gint ppr_skip;            /* > 0 inside the paragraph mark's rPr or a section */
    gchar* p_style;
    DocxParaProps p_direct;

    /* Run */
    gboolean in_run;
    gboolean in_rpr;
    gchar* r_style;
    DocxRunProps r_direct;
    DocxRunProps r_eff;
    gboolean r_eff_valid;
    gboolean in_text;
    gboolean in_math_text;
    gint math_vert;
    gboolean link;

    /* Tables */
    gint tbl_depth;
    gboolean row_started;
    gboolean row_header;
    gboolean cell_pending;    /* w:tc seen, cell not started (its span may follow) */
    guint grid_span;

    /* Section: first header and footer */
    gboolean in_sect;
    gchar* header_rid;
    gboolean header_default;
    gchar* footer_rid;
    gboolean footer_default;
} DocParse;

static void doc_parse_init(DocParse* d, ZipReader* zip, const gchar* part, DocxStyles* styles,
                           DocxNumbering* num, DocWriter* w, gsize max_text, gsize* image_bytes) {
    memset(d, 0, sizeof *d);
    d->zip = zip;
    d->part_dir = zip_xml_dirname(part);
    d->rels = read_rels(zip, part);
    d->styles = styles;
    d->num = num;
    d->w = w;
    d->max_text = MAX(max_text, 1);
    d->image_bytes = image_bytes;
    docx_run_props_clear(&d->r_direct);
    docx_run_props_clear(&d->r_eff);
}

static void doc_parse_clear(DocParse* d) {
    g_free(d->part_dir);
    g_hash_table_destroy(d->rels);
    g_free(d->img_rid);
    g_free(d->img_name);
    g_free(d->p_style);
    g_free(d->r_style);
    docx_run_props_clear(&d->r_direct);
    docx_run_props_clear(&d->r_eff);
    g_free(d->header_rid);
    g_free(d->footer_rid);
}

static void start_pending_cell(DocParse* d) {
    if (d->tbl_depth != 1 || !d->cell_pending) {
        return;
    }
    if (!d->row_started) {
        doc_writer_row_start(d->w, d->row_header);
        d->row_started = TRUE;
    }
    doc_writer_cell_start(d->w, d->grid_span);
    d->cell_pending = FALSE;
}

/* Paragraph properties are known: a page break before it */
static void end_paragraph_properties(DocParse* d) {
    DocxParaProps eff;
    docx_styles_para(d->styles, d->p_style, &eff);
    docx_para_props_merge(&eff, &d->p_direct);
    if (eff.page_break_before) {
        doc_writer_page_break(d->w);
    }
}

static void end_paragraph(DocParse* d) {
    d->in_p = FALSE;
    DocxParaProps eff;
    docx_styles_para(d->styles, d->p_style, &eff);
    gint num_id = (d->p_direct.set & DOCX_PP_NUM) ? d->p_direct.num_id : eff.num_id;
    gint ilvl = (d->p_direct.set & DOCX_PP_LEVEL) ? d->p_direct.ilvl : eff.ilvl;
    gchar* label = NULL;
    DocxRunProps label_props;
    docx_styles_run(d->styles, d->p_style, NULL, &label_props);
    if (num_id > 0) {
        label = docx_numbering_label(d->num, num_id, ilvl, &eff, &label_props);
    }
    docx_para_props_merge(&eff, &d->p_direct);
    DocPara para;
    docx_para_props_to_para(&eff, &para);
    DocRun label_run;
    docx_run_props_to_run(&label_props, &label_run);
    label_run.underline = label_run.strike = FALSE;
    doc_writer_end_paragraph(d->w, &para, label, &label_run);
    g_free(label);
    docx_run_props_clear(&label_props);
    g_clear_pointer(&d->p_style, g_free);
    memset(&d->p_direct, 0, sizeof d->p_direct);
}

static const gchar* image_mime_type(const gchar* path) {
    const gchar* dot = strrchr(path, '.');
    static const gchar* const shown[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp", ".svg" };
    for (gsize i = 0; dot != NULL && i < G_N_ELEMENTS(shown); i++) {
        if (g_ascii_strcasecmp(dot, shown[i]) == 0) {
            return shown[i];
        }
    }
    return NULL;
}

/* Picture: formats the preview cannot decode (EMF, WMF...) or too large
 * ones are named instead */
static void emit_image(DocParse* d) {
    const Rel* rel = d->img_rid != NULL ? g_hash_table_lookup(d->rels, d->img_rid) : NULL;
    if (rel == NULL || rel->external) {
        return;
    }
    gchar* part = zip_xml_resolve(d->part_dir, rel->target);
    gchar* name = (d->img_name != NULL && d->img_name[0] != '\0') ? g_strdup(d->img_name)
                                                                 : g_path_get_basename(part);
    GBytes* bytes = NULL;
    gint64 size = zip_reader_get_size(d->zip, part);
    if (image_mime_type(part) != NULL && size > 0 && size <= MAX_IMAGE &&
        *d->image_bytes + (gsize)size <= MAX_IMAGES_TOTAL) {
        bytes = zip_reader_read(d->zip, part, MAX_IMAGE, NULL);
    }
    if (bytes != NULL) {
        *d->image_bytes += g_bytes_get_size(bytes);
    }
    start_pending_cell(d);
    doc_writer_image(d->w, bytes, d->img_width, name);
    if (bytes != NULL) {
        g_bytes_unref(bytes);
    }
    g_free(name);
    g_free(part);
}

static gboolean is_skipped_element(const gchar* name) {
    static const gchar* const skipped[] = {
        "mc:Fallback", "w:del", "w:moveFrom", "w:pPrChange", "w:rPrChange",
        "w:sectPrChange", "w:tblPrChange", "w:trPrChange", "w:tcPrChange",
        "w:numberingChange", "w:sdtPr", "w:fldData", "w:instrText", "w:footnoteReference",
        "w:endnoteReference", "w:commentReference",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(skipped); i++) {
        if (strcmp(name, skipped[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* "width:120pt;height:40pt" of a VML shape */
static gdouble vml_width(const gchar* style) {
    const gchar* w = style != NULL ? strstr(style, "width:") : NULL;
    if (w == NULL) {
        return 0;
    }
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(w + 6, &end);
    if (end != NULL && g_str_has_prefix(end, "in")) {
        v *= 72;
    } else if (end != NULL && g_str_has_prefix(end, "px")) {
        v *= 0.75;
    }
    return v;
}

static void start_picture_element(DocParse* d, const gchar* name, const gchar** names, const gchar** values) {
    const gchar* v;
    if (strcmp(name, "a:blip") == 0 && (v = attr(names, values, "r:embed")) != NULL) {
        g_free(d->img_rid);
        d->img_rid = g_strdup(v);
    } else if (strcmp(name, "v:imagedata") == 0 && (v = attr(names, values, "r:id")) != NULL) {
        g_free(d->img_rid);
        d->img_rid = g_strdup(v);
    } else if (strcmp(name, "v:shape") == 0 && d->img_width <= 0) {
        d->img_width = vml_width(attr(names, values, "style"));
    } else if (strcmp(name, "wp:extent") == 0 && (v = attr(names, values, "cx")) != NULL) {
        d->img_width = g_ascii_strtod(v, NULL) / EMU_PER_PT;
    } else if (strcmp(name, "wp:docPr") == 0) {
        v = attr(names, values, "descr");
        if (v == NULL || v[0] == '\0') {
            v = attr(names, values, "title");
        }
        if (v == NULL || v[0] == '\0') {
            v = attr(names, values, "name");
        }
        g_free(d->img_name);
        d->img_name = g_strdup(v);
    }
}

/* Section properties: the first default header and footer */
static void start_section_element(DocParse* d, const gchar* name, const gchar** names, const gchar** values) {
    gboolean header = strcmp(name, "w:headerReference") == 0;
    if (!header && strcmp(name, "w:footerReference") != 0) {
        return;
    }
    const gchar* rid = attr(names, values, "r:id");
    gboolean is_default = g_strcmp0(attr(names, values, "w:type"), "default") == 0;
    gchar** slot = header ? &d->header_rid : &d->footer_rid;
    gboolean* slot_default = header ? &d->header_default : &d->footer_default;
    if (rid != NULL && (*slot == NULL || (is_default && !*slot_default))) {
        g_free(*slot);
        *slot = g_strdup(rid);
        *slot_default = is_default;
    }
}

/* Properties of the current run, with the styles */
static const DocxRunProps* run_props(DocParse* d) {
    if (!d->r_eff_valid) {
        docx_run_props_clear(&d->r_eff);
        docx_styles_run(d->styles, d->p_style, d->r_style, &d->r_eff);
        docx_run_props_merge(&d->r_eff, &d->r_direct);
        d->r_eff_valid = TRUE;
    }
    return &d->r_eff;
}

static void write_text(DocParse* d, const gchar* text, gssize len, gboolean math, GError** error) {
    const DocxRunProps* p = run_props(d);
    if (p->hidden) {
        return;
    }
    start_pending_cell(d);
    DocRun run;
    docx_run_props_to_run(p, &run);
    run.link = d->link;
    if (math) {
        run.italic = TRUE;
        run.vert = d->math_vert;
    }
    doc_writer_text(d->w, text, len, &run);
    if (doc_writer_length(d->w) >= d->max_text) {
        d->truncated = TRUE;
        zip_xml_stop(error);
    }
}

static void start_run(DocParse* d) {
    d->in_run = TRUE;
    g_clear_pointer(&d->r_style, g_free);
    docx_run_props_clear(&d->r_direct);
    d->r_eff_valid = FALSE;
}

static void doc_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    DocParse* d = data;
    if (d->skip_depth > 0) {
        d->skip_depth++;
        return;
    }
    if (d->drawing_depth > 0) {
        d->drawing_depth++;
        start_picture_element(d, name, names, values);
        return;
    }
    if (d->in_sect) {
        start_section_element(d, name, names, values);
        return;
    }
    if (is_skipped_element(name)) {
        d->skip_depth = 1;
        return;
    }
    if (strcmp(name, "w:drawing") == 0 || strcmp(name, "w:pict") == 0 || strcmp(name, "w:object") == 0) {
        d->drawing_depth = 1;
        g_clear_pointer(&d->img_rid, g_free);
        g_clear_pointer(&d->img_name, g_free);
        d->img_width = 0;
        return;
    }
    if (strcmp(name, "w:sectPr") == 0) {
        d->in_sect = TRUE;
        return;
    }
    if (d->in_ppr && d->ppr_skip > 0) {
        d->ppr_skip++;
        return;
    }

    if (strcmp(name, "w:p") == 0) {
        start_pending_cell(d);
        g_clear_pointer(&d->p_style, g_free);
        memset(&d->p_direct, 0, sizeof d->p_direct);
        d->in_p = TRUE;
        d->link = FALSE;
    } else if (d->in_p && !d->in_run && strcmp(name, "w:pPr") == 0) {
        d->in_ppr = TRUE;
    } else if (d->in_ppr) {
        if (strcmp(name, "w:pStyle") == 0) {
            g_free(d->p_style);
            d->p_style = g_strdup(attr(names, values, "w:val"));
        } else if (strcmp(name, "w:rPr") == 0) {
            d->ppr_skip = 1; /* the paragraph mark */
        } else {
            docx_para_property(&d->p_direct, name, names, values);
        }
    } else if (strcmp(name, "w:r") == 0) {
        start_run(d);
    } else if (d->in_run && strcmp(name, "w:rPr") == 0) {
        d->in_rpr = TRUE;
    } else if (d->in_rpr) {
        if (strcmp(name, "w:rStyle") == 0) {
            g_free(d->r_style);
            d->r_style = g_strdup(attr(names, values, "w:val"));
        } else {
            docx_run_property(d->styles, &d->r_direct, name, names, values);
        }
        d->r_eff_valid = FALSE;
    } else if (strcmp(name, "w:t") == 0) {
        d->in_text = TRUE;
    } else if (strcmp(name, "m:t") == 0) {
        d->in_math_text = TRUE;
    } else if (d->in_p && strcmp(name, "m:sup") == 0) {
        d->math_vert = 1;
    } else if (d->in_p && strcmp(name, "m:sub") == 0) {
        d->math_vert = -1;
    } else if (d->in_p && strcmp(name, "m:den") == 0) {
        write_text(d, "/", 1, TRUE, error);
    } else if (d->in_p && (strcmp(name, "w:tab") == 0 || strcmp(name, "w:ptab") == 0)) {
        write_text(d, "\t", 1, FALSE, error);
    } else if (d->in_p && (strcmp(name, "w:br") == 0 || strcmp(name, "w:cr") == 0)) {
        const gchar* type = attr(names, values, "w:type");
        if (g_strcmp0(type, "page") == 0) {
            doc_writer_page_break(d->w);
        } else if (type == NULL || strcmp(type, "textWrapping") == 0) {
            write_text(d, LINE_BREAK, -1, FALSE, error);
        }
    } else if (d->in_p && strcmp(name, "w:noBreakHyphen") == 0) {
        write_text(d, "-", 1, FALSE, error);
    } else if (d->in_p && strcmp(name, "w:sym") == 0) {
        const gchar* code = attr(names, values, "w:char");
        if (code != NULL) {
            gchar utf8[8] = { 0 };
            g_unichar_to_utf8((gunichar)g_ascii_strtoull(code, NULL, 16), utf8);
            gchar* text = preview_symbol_text(utf8, attr(names, values, "w:font"));
            write_text(d, text, -1, FALSE, error);
            g_free(text);
        }
    } else if (d->in_p && strcmp(name, "w:hyperlink") == 0) {
        const gchar* rid = attr(names, values, "r:id");
        const Rel* rel = rid != NULL ? g_hash_table_lookup(d->rels, rid) : NULL;
        d->link = rel != NULL && rel->external;
    } else if (strcmp(name, "w:tbl") == 0) {
        start_pending_cell(d); /* a table nested at the start of a cell */
        d->tbl_depth++;
        doc_writer_table_start(d->w);
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tr") == 0) {
        d->row_started = FALSE;
        d->row_header = FALSE;
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tblHeader") == 0) {
        d->row_header = attr_on(names, values);
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tc") == 0) {
        d->cell_pending = TRUE;
        d->grid_span = 1;
    } else if (d->tbl_depth == 1 && strcmp(name, "w:gridSpan") == 0) {
        const gchar* v = attr(names, values, "w:val");
        d->grid_span = v != NULL ? (guint)CLAMP(g_ascii_strtoll(v, NULL, 10), 1, 64) : 1;
    }
}

static void doc_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    DocParse* d = data;
    if (d->skip_depth > 0) {
        d->skip_depth--;
        return;
    }
    if (d->drawing_depth > 0) {
        d->drawing_depth--;
        if (d->drawing_depth == 0 && d->in_p) {
            emit_image(d);
        }
        return;
    }
    if (d->in_sect) {
        d->in_sect = strcmp(name, "w:sectPr") != 0;
        return;
    }
    if (d->in_ppr && d->ppr_skip > 0) {
        d->ppr_skip--;
        return;
    }

    if (strcmp(name, "w:p") == 0) {
        if (d->in_p) {
            end_paragraph(d);
        }
    } else if (strcmp(name, "w:pPr") == 0 && d->in_ppr) {
        d->in_ppr = FALSE;
        end_paragraph_properties(d);
    } else if (strcmp(name, "w:r") == 0) {
        d->in_run = FALSE;
        d->in_rpr = FALSE;
    } else if (strcmp(name, "w:rPr") == 0) {
        d->in_rpr = FALSE;
    } else if (strcmp(name, "w:t") == 0) {
        d->in_text = FALSE;
    } else if (strcmp(name, "m:t") == 0) {
        d->in_math_text = FALSE;
    } else if (strcmp(name, "m:sup") == 0 || strcmp(name, "m:sub") == 0) {
        d->math_vert = 0;
    } else if (strcmp(name, "w:hyperlink") == 0) {
        d->link = FALSE;
    } else if (strcmp(name, "w:tc") == 0 && d->tbl_depth == 1) {
        start_pending_cell(d);
        doc_writer_cell_end(d->w);
    } else if (strcmp(name, "w:tr") == 0 && d->tbl_depth == 1) {
        doc_writer_row_end(d->w);
        d->row_started = FALSE;
    } else if (strcmp(name, "w:tbl") == 0 && d->tbl_depth > 0) {
        doc_writer_table_end(d->w);
        d->tbl_depth--;
    }
}

static void doc_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    DocParse* d = data;
    if (d->skip_depth > 0 || d->drawing_depth > 0 || !d->in_p || len == 0) {
        return;
    }
    if (d->in_text) {
        write_text(d, text, (gssize)len, FALSE, error);
    } else if (d->in_math_text) {
        write_text(d, text, (gssize)len, TRUE, error);
    }
}

/* Parse a part into the writer; FALSE with error on malformed XML */
static gboolean parse_story(DocParse* d, const gchar* part, gsize max_size, gboolean optional, GError** error) {
    static const GMarkupParser parser = { doc_start, doc_end, doc_text, NULL, NULL };
    return zip_xml_parse(d->zip, part, max_size, optional, &parser, d, error);
}

/* The text of a header or footer part (NULL if it has none) */
static PreviewDoc* read_story(ZipReader* zip, const gchar* part, DocxStyles* styles, DocxNumbering* num,
                              gdouble size, const gchar* font, gsize* image_bytes) {
    DocWriter* w = doc_writer_new(font, size);
    DocParse d;
    doc_parse_init(&d, zip, part, styles, num, w, MAX_HEADER_TEXT, image_bytes);
    parse_story(&d, part, MAX_PART, TRUE, NULL);
    doc_parse_clear(&d);
    return doc_writer_finish(w);
}

/* ---- Document properties (docProps/core.xml) ---------------------------- */

typedef struct {
    GString* field;      /* element being read, NULL elsewhere */
    GString* title;
    GString* creator;
} CoreParse;

static void core_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)names;
    (void)values;
    (void)error;
    CoreParse* cp = data;
    cp->field = strcmp(name, "dc:creator") == 0 ? cp->creator : strcmp(name, "dc:title") == 0 ? cp->title : NULL;
}

static void core_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)name;
    (void)error;
    ((CoreParse*)data)->field = NULL;
}

static void core_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    CoreParse* cp = data;
    if (cp->field != NULL) {
        g_string_append_len(cp->field, text, (gssize)len);
    }
}

static gchar* stripped_or_null(GString* s) {
    gchar* text = g_strstrip(g_string_free(s, FALSE));
    if (text[0] == '\0') {
        g_free(text);
        return NULL;
    }
    return text;
}

/* ---- Entry points ------------------------------------------------------- */

PreviewDoc* docx_to_preview(ZipReader* zip, const gchar* name, const gchar* size_text, gsize max_text,
                            GError** error) {
    g_return_val_if_fail(zip != NULL, NULL);
    gchar* main = main_part_name(zip);
    if (main == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Not a Word document"));
        return NULL;
    }
    gchar* dir = zip_xml_dirname(main);
    GHashTable* rels = read_rels(zip, main);
    gchar* styles_part = rel_part(rels, dir, "/styles");
    gchar* numbering_part = rel_part(rels, dir, "/numbering");
    gchar* theme_part = rel_part(rels, dir, "/theme");
    if (styles_part == NULL) {
        styles_part = zip_xml_resolve(dir, "styles.xml");
    }
    if (numbering_part == NULL) {
        numbering_part = zip_xml_resolve(dir, "numbering.xml");
    }
    DocxStyles* styles = docx_styles_read(zip, styles_part, theme_part);
    DocxNumbering* num = docx_numbering_read(zip, numbering_part, styles);

    /* The size and font of normal text: the preview's own size */
    DocxRunProps normal;
    docx_styles_run(styles, NULL, NULL, &normal);
    gdouble size = normal.size > 0 ? normal.size : 10.0;

    gsize image_bytes = 0;
    DocWriter* w = doc_writer_new(normal.font, size);
    DocParse d;
    doc_parse_init(&d, zip, main, styles, num, w, max_text, &image_bytes);
    PreviewDoc* doc = NULL;
    GError* err = NULL;
    if (parse_story(&d, main, DOCX_MAX_XML, FALSE, &err)) {
        PreviewDoc* body = doc_writer_finish(w);
        w = NULL;
        PreviewDoc* header = NULL;
        PreviewDoc* footer = NULL;
        const Rel* hr = d.header_rid != NULL ? g_hash_table_lookup(d.rels, d.header_rid) : NULL;
        const Rel* fr = d.footer_rid != NULL ? g_hash_table_lookup(d.rels, d.footer_rid) : NULL;
        if (hr != NULL && !hr->external) {
            gchar* part = zip_xml_resolve(dir, hr->target);
            header = read_story(zip, part, styles, num, size, normal.font, &image_bytes);
            g_free(part);
        }
        if (fr != NULL && !fr->external) {
            gchar* part = zip_xml_resolve(dir, fr->target);
            footer = read_story(zip, part, styles, num, size, normal.font, &image_bytes);
            g_free(part);
        }

        CoreParse cp = { NULL, g_string_new(NULL), g_string_new(NULL) };
        static const GMarkupParser core_parser = { core_start, core_end, core_text, NULL, NULL };
        zip_xml_parse(zip, "docProps/core.xml", MAX_PART, TRUE, &core_parser, &cp, NULL);
        gchar* title = stripped_or_null(cp.title);
        gchar* author = stripped_or_null(cp.creator);
        GString* subtitle = g_string_new(_("Word document"));
        if (author != NULL) {
            g_string_append_printf(subtitle, " · %s", author);
        }
        if (size_text != NULL) {
            g_string_append_printf(subtitle, " · %s", size_text);
        }
        doc = doc_writer_page(title != NULL ? title : (name != NULL ? name : ""), subtitle->str, header, body,
                              footer, d.truncated ? _("Document truncated: only the beginning is shown.") : NULL);
        g_string_free(subtitle, TRUE);
        g_free(title);
        g_free(author);
        preview_doc_free(body);
        if (header != NULL) {
            preview_doc_free(header);
        }
        if (footer != NULL) {
            preview_doc_free(footer);
        }
    } else {
        g_propagate_error(error, err);
    }
    if (w != NULL) {
        doc_writer_free(w);
    }
    doc_parse_clear(&d);
    docx_run_props_clear(&normal);
    docx_numbering_free(num);
    docx_styles_free(styles);
    g_hash_table_destroy(rels);
    g_free(styles_part);
    g_free(numbering_part);
    g_free(theme_part);
    g_free(dir);
    g_free(main);
    return doc;
}

PreviewDoc* docx_file_preview(const gchar* path, const gchar* name, const gchar* size_text, gsize max_text,
                              GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    PreviewDoc* doc = docx_to_preview(zip, name, size_text, max_text, error);
    zip_reader_free(zip);
    return doc;
}
