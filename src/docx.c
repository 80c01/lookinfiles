/* Word document (.docx) to HTML, for the preview pane */
#include "docx.h"

#include <gio/gio.h>
#include <string.h>

#define MAX_PART (16u * 1024 * 1024)          /* styles, numbering, relationships */
#define MAX_IMAGE (16u * 1024 * 1024)         /* one embedded image */
#define MAX_IMAGES_TOTAL (48u * 1024 * 1024)  /* all embedded images */
#define MAX_LEVELS 9                          /* list levels in Word */
#define MAX_STYLE_CHAIN 16

/* Error raised by the parser callbacks to stop at the text limit */
G_DEFINE_QUARK(docx-stop-quark, docx_stop)

/* ---- Small helpers ------------------------------------------------------ */

static const gchar* attr(const gchar** names, const gchar** values, const gchar* key) {
    for (gsize i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], key) == 0) {
            return values[i];
        }
    }
    return NULL;
}

/* On/off property such as <w:b/> or <w:b w:val="0"/> */
static gboolean attr_on(const gchar** names, const gchar** values) {
    const gchar* v = attr(names, values, "w:val");
    return v == NULL || !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0);
}

static gint attr_int(const gchar** names, const gchar** values, const gchar* key, gint def) {
    const gchar* v = attr(names, values, key);
    if (v == NULL || v[0] == '\0') {
        return def;
    }
    gint64 n = g_ascii_strtoll(v, NULL, 10);
    return (gint)CLAMP(n, -1000000, 1000000);
}

static void append_escaped(GString* out, const gchar* text, gsize len) {
    for (gsize i = 0; i < len; i++) {
        switch (text[i]) {
        case '&': g_string_append(out, "&amp;"); break;
        case '<': g_string_append(out, "&lt;"); break;
        case '>': g_string_append(out, "&gt;"); break;
        default: g_string_append_c(out, text[i]); break;
        }
    }
}

/* Attribute value for the preview's HTML renderer, which reads attributes
 * verbatim up to the closing quote and ends a tag at the first '>' */
static void append_attr_value(GString* out, const gchar* value) {
    for (const gchar* p = value; *p != '\0'; p++) {
        if (*p == '"') {
            g_string_append_c(out, '\'');
        } else if (*p == '<' || *p == '>' || (guchar)*p < 0x20) {
            g_string_append_c(out, ' ');
        } else {
            g_string_append_c(out, *p);
        }
    }
}

/* Parse one XML part of the archive. A missing part is not an error when
 * optional (returns FALSE, error untouched). */
static gboolean parse_part(ZipReader* zip, const gchar* name, gsize max_size, gboolean optional,
                           const GMarkupParser* parser, gpointer data, GError** error) {
    if (optional && !zip_reader_has(zip, name)) {
        return FALSE;
    }
    GBytes* bytes = zip_reader_read(zip, name, max_size, error);
    if (bytes == NULL) {
        return FALSE;
    }
    gsize len = 0;
    const gchar* xml = g_bytes_get_data(bytes, &len);
    if (len >= 3 && memcmp(xml, "\xEF\xBB\xBF", 3) == 0) {
        xml += 3;
        len -= 3;
    }
    GMarkupParseContext* ctx = g_markup_parse_context_new(parser, 0, data, NULL);
    gboolean ok = g_markup_parse_context_parse(ctx, xml, (gssize)len, error) &&
                  g_markup_parse_context_end_parse(ctx, error);
    g_markup_parse_context_free(ctx);
    g_bytes_unref(bytes);
    return ok;
}

/* Part path from a relationship target: relative to base_dir ("" for the
 * package root) or absolute ("/word/media/a.png"); "." and ".." resolved */
static gchar* resolve_part(const gchar* base_dir, const gchar* target) {
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
    gchar* path = g_strjoinv("/", (gchar**)stack->pdata);
    g_ptr_array_free(stack, TRUE);
    g_strfreev(parts);
    g_free(joined);
    return path;
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
    gchar* dir = g_path_get_dirname(part);
    gchar* base = g_path_get_basename(part);
    gchar* rels_name = (strcmp(dir, ".") == 0) ? g_strconcat("_rels/", base, ".rels", NULL)
                                                : g_strconcat(dir, "/_rels/", base, ".rels", NULL);
    static const GMarkupParser parser = { rels_start, NULL, NULL, NULL, NULL };
    parse_part(zip, rels_name, MAX_PART, TRUE, &parser, rels, NULL);
    g_free(rels_name);
    g_free(base);
    g_free(dir);
    return rels;
}

/* Main document part named by the package relationships */
static gchar* main_part_name(ZipReader* zip) {
    GHashTable* rels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, rel_free);
    static const GMarkupParser parser = { rels_start, NULL, NULL, NULL, NULL };
    parse_part(zip, "_rels/.rels", MAX_PART, TRUE, &parser, rels, NULL);
    gchar* main = NULL;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, rels);
    while (main == NULL && g_hash_table_iter_next(&it, NULL, &value)) {
        Rel* r = value;
        if (!r->external && r->type != NULL && g_str_has_suffix(r->type, "/officeDocument")) {
            main = resolve_part("", r->target);
        }
    }
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

/* ---- Styles (styles.xml) ------------------------------------------------ */

typedef struct {
    gchar* based_on;
    gint heading;   /* 1-6, 0 if not a heading */
    gint num_id;    /* -1 if the style sets no numbering */
    gint ilvl;
    gboolean bold;
    gboolean italic;
} Style;

typedef struct {
    GHashTable* styles; /* styleId -> Style* */
    Style* cur;
    gboolean in_ppr;
    gboolean in_rpr;
} StylesParse;

static void style_free(gpointer data) {
    Style* s = data;
    g_free(s->based_on);
    g_free(s);
}

/* Heading level from a style name: "heading 2" -> 2, "Title" -> 1 */
static gint heading_from_style_name(const gchar* name) {
    if (g_ascii_strcasecmp(name, "title") == 0) {
        return 1;
    }
    if (g_ascii_strncasecmp(name, "heading ", 8) == 0 && g_ascii_isdigit(name[8]) && name[9] == '\0') {
        gint n = name[8] - '0';
        return n >= 1 ? MIN(n, 6) : 0;
    }
    return 0;
}

static void styles_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                         const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    StylesParse* sp = data;
    if (strcmp(name, "w:style") == 0) {
        const gchar* id = attr(names, values, "w:styleId");
        sp->cur = NULL;
        if (id != NULL) {
            sp->cur = g_new0(Style, 1);
            sp->cur->num_id = -1;
            g_hash_table_replace(sp->styles, g_strdup(id), sp->cur);
        }
        return;
    }
    Style* s = sp->cur;
    if (s == NULL) {
        return;
    }
    const gchar* val = attr(names, values, "w:val");
    if (strcmp(name, "w:name") == 0 && val != NULL) {
        s->heading = heading_from_style_name(val);
    } else if (strcmp(name, "w:basedOn") == 0 && val != NULL) {
        g_free(s->based_on);
        s->based_on = g_strdup(val);
    } else if (strcmp(name, "w:pPr") == 0) {
        sp->in_ppr = TRUE;
    } else if (strcmp(name, "w:rPr") == 0) {
        sp->in_rpr = TRUE;
    } else if (sp->in_ppr && strcmp(name, "w:outlineLvl") == 0) {
        gint lvl = attr_int(names, values, "w:val", 9);
        if (s->heading == 0 && lvl >= 0 && lvl < 9) {
            s->heading = MIN(lvl + 1, 6);
        }
    } else if (sp->in_ppr && strcmp(name, "w:numId") == 0) {
        s->num_id = attr_int(names, values, "w:val", -1);
    } else if (sp->in_ppr && strcmp(name, "w:ilvl") == 0) {
        s->ilvl = attr_int(names, values, "w:val", 0);
    } else if (sp->in_rpr && strcmp(name, "w:b") == 0) {
        s->bold = attr_on(names, values);
    } else if (sp->in_rpr && strcmp(name, "w:i") == 0) {
        s->italic = attr_on(names, values);
    }
}

static void styles_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    StylesParse* sp = data;
    if (strcmp(name, "w:style") == 0) {
        sp->cur = NULL;
    } else if (strcmp(name, "w:pPr") == 0) {
        sp->in_ppr = FALSE;
    } else if (strcmp(name, "w:rPr") == 0) {
        sp->in_rpr = FALSE;
    }
}

/* ---- Numbering (numbering.xml) ------------------------------------------ */

typedef struct {
    gboolean ordered[MAX_LEVELS];
    gchar type[MAX_LEVELS];   /* HTML <ol type>: '1', 'a', 'A', 'i', 'I' */
    gint start[MAX_LEVELS];
} AbstractNum;

typedef struct {
    GHashTable* abstracts;  /* abstractNumId + 1 -> AbstractNum* */
    GHashTable* nums;       /* numId + 1 -> abstractNumId + 1 */
    AbstractNum* cur;
    gint cur_lvl;
    gint cur_num;           /* numId of the <w:num> being read, -1 outside */
} NumParse;

static void num_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    NumParse* np = data;
    if (strcmp(name, "w:abstractNum") == 0) {
        gint id = attr_int(names, values, "w:abstractNumId", -1);
        np->cur = NULL;
        if (id >= 0) {
            np->cur = g_new0(AbstractNum, 1);
            for (gint l = 0; l < MAX_LEVELS; l++) {
                np->cur->type[l] = '1';
                np->cur->start[l] = 1;
            }
            g_hash_table_replace(np->abstracts, GINT_TO_POINTER(id + 1), np->cur);
        }
        np->cur_lvl = -1;
    } else if (np->cur != NULL && strcmp(name, "w:lvl") == 0) {
        np->cur_lvl = attr_int(names, values, "w:ilvl", -1);
    } else if (np->cur != NULL && np->cur_lvl >= 0 && np->cur_lvl < MAX_LEVELS) {
        const gchar* val = attr(names, values, "w:val");
        if (strcmp(name, "w:start") == 0) {
            np->cur->start[np->cur_lvl] = attr_int(names, values, "w:val", 1);
        } else if (strcmp(name, "w:numFmt") == 0 && val != NULL) {
            gint l = np->cur_lvl;
            np->cur->ordered[l] = strcmp(val, "bullet") != 0 && strcmp(val, "none") != 0;
            np->cur->type[l] = strcmp(val, "lowerLetter") == 0 ? 'a'
                             : strcmp(val, "upperLetter") == 0 ? 'A'
                             : strcmp(val, "lowerRoman") == 0 ? 'i'
                             : strcmp(val, "upperRoman") == 0 ? 'I' : '1';
        }
    } else if (strcmp(name, "w:num") == 0) {
        np->cur_num = attr_int(names, values, "w:numId", -1);
    } else if (np->cur_num >= 0 && strcmp(name, "w:abstractNumId") == 0) {
        gint abs = attr_int(names, values, "w:val", -1);
        if (abs >= 0) {
            g_hash_table_replace(np->nums, GINT_TO_POINTER(np->cur_num + 1), GINT_TO_POINTER(abs + 1));
        }
    }
}

static void num_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    NumParse* np = data;
    if (strcmp(name, "w:abstractNum") == 0) {
        np->cur = NULL;
    } else if (strcmp(name, "w:lvl") == 0) {
        np->cur_lvl = -1;
    } else if (strcmp(name, "w:num") == 0) {
        np->cur_num = -1;
    }
}

/* ---- Document (document.xml) -------------------------------------------- */

typedef struct {
    gboolean ordered;
    gint num_id;
} OpenList;

/* Run formatting, in the order the tags are opened */
enum { RUN_B = 1, RUN_I = 2, RUN_U = 4, RUN_S = 8, RUN_SUP = 16, RUN_SUB = 32 };

typedef struct {
    ZipReader* zip;
    gchar* part_dir;          /* directory of the main part ("word") */
    GHashTable* rels;         /* of the main part */
    GHashTable* styles;
    NumParse num;
    GHashTable* counters;     /* numId + 1 -> gint[MAX_LEVELS]: items seen per level */

    GString* out;
    gsize text_len;
    gsize max_text;
    gboolean truncated;
    gsize image_bytes;

    gint skip_depth;          /* > 0 inside an ignored element */
    gint drawing_depth;       /* > 0 inside a picture */
    gchar* img_rid;
    gchar* img_alt;

    /* Paragraph */
    gboolean in_p;
    gboolean in_ppr;
    GString* para;
    gchar* p_style;
    gint p_num_id;            /* -1 if not set on the paragraph */
    gint p_ilvl;
    gint p_heading;
    gboolean link_open;

    /* Run */
    gboolean in_run;
    gboolean in_rpr;
    guint run_fmt;
    guint run_open;           /* tags written for the current run */
    gboolean run_hidden;
    gboolean in_text;
    gboolean in_math_text;
    gsize math_end;           /* para length after the last math text, to merge runs */

    /* Tables (only the outermost one is a table in the HTML) */
    gint tbl_depth;
    gboolean row_open;
    gboolean row_header;
    gboolean cell_open;
    gboolean cell_is_th;
    gboolean cell_has_text;
    gint grid_span;

    OpenList lists[MAX_LEVELS];
    gint list_depth;
} DocParse;

static const Style* find_style(const DocParse* d, const gchar* id) {
    return id != NULL ? g_hash_table_lookup(d->styles, id) : NULL;
}

static gint style_heading(const DocParse* d, const gchar* id) {
    const Style* s = find_style(d, id);
    for (gint i = 0; s != NULL && i < MAX_STYLE_CHAIN; i++) {
        if (s->heading > 0) {
            return s->heading;
        }
        s = find_style(d, s->based_on);
    }
    return 0;
}

static gboolean style_numbering(const DocParse* d, const gchar* id, gint* num_id, gint* ilvl) {
    const Style* s = find_style(d, id);
    for (gint i = 0; s != NULL && i < MAX_STYLE_CHAIN; i++) {
        if (s->num_id >= 0) {
            *num_id = s->num_id;
            *ilvl = s->ilvl;
            return TRUE;
        }
        s = find_style(d, s->based_on);
    }
    return FALSE;
}

static guint style_run_format(const DocParse* d, const gchar* id) {
    const Style* s = find_style(d, id);
    guint fmt = 0;
    for (gint i = 0; s != NULL && i < MAX_STYLE_CHAIN; i++) {
        fmt |= (s->bold ? RUN_B : 0) | (s->italic ? RUN_I : 0);
        s = find_style(d, s->based_on);
    }
    return fmt;
}

static const AbstractNum* find_numbering(const DocParse* d, gint num_id) {
    gpointer abs = g_hash_table_lookup(d->num.nums, GINT_TO_POINTER(num_id + 1));
    return abs != NULL ? g_hash_table_lookup(d->num.abstracts, abs) : NULL;
}

static void close_lists(DocParse* d, gint keep) {
    while (d->list_depth > keep) {
        d->list_depth--;
        g_string_append(d->out, d->lists[d->list_depth].ordered ? "</ol>\n" : "</ul>\n");
    }
}

static void emit_list_item(DocParse* d, gint num_id, gint ilvl, const gchar* content) {
    const AbstractNum* an = find_numbering(d, num_id);
    gboolean ordered = an != NULL && an->ordered[ilvl];

    /* Word numbers items per list and level, across other paragraphs;
     * an item restarts the numbering of the levels below it */
    gint* counts = g_hash_table_lookup(d->counters, GINT_TO_POINTER(num_id + 1));
    if (counts == NULL) {
        counts = g_new0(gint, MAX_LEVELS);
        g_hash_table_insert(d->counters, GINT_TO_POINTER(num_id + 1), counts);
    }
    counts[ilvl]++;
    for (gint l = ilvl + 1; l < MAX_LEVELS; l++) {
        counts[l] = 0;
    }

    close_lists(d, ilvl + 1);
    if (d->list_depth == ilvl + 1 &&
        (d->lists[ilvl].num_id != num_id || d->lists[ilvl].ordered != ordered)) {
        close_lists(d, ilvl);
    }
    while (d->list_depth <= ilvl) {
        gint l = d->list_depth;
        gboolean o = (l == ilvl) ? ordered : (an != NULL && an->ordered[l]);
        if (o) {
            gint start = an->start[l] + MAX(counts[l], 1) - 1;
            g_string_append_printf(d->out, "<ol type=\"%c\" start=\"%d\">\n", an->type[l], start);
        } else {
            g_string_append(d->out, "<ul>\n");
        }
        d->lists[l].ordered = o;
        d->lists[l].num_id = num_id;
        d->list_depth++;
    }
    g_string_append_printf(d->out, "<li>%s</li>\n", content);
}

static void open_run_tags(DocParse* d) {
    if (d->run_open != 0 || d->run_fmt == 0) {
        return;
    }
    guint f = d->run_fmt;
    if (f & RUN_B) g_string_append(d->para, "<b>");
    if (f & RUN_I) g_string_append(d->para, "<i>");
    if (f & RUN_U) g_string_append(d->para, "<u>");
    if (f & RUN_S) g_string_append(d->para, "<s>");
    if (f & RUN_SUP) g_string_append(d->para, "<sup>");
    else if (f & RUN_SUB) g_string_append(d->para, "<sub>");
    d->run_open = f;
}

static void close_run_tags(DocParse* d) {
    guint f = d->run_open;
    if (f & RUN_SUP) g_string_append(d->para, "</sup>");
    else if (f & RUN_SUB) g_string_append(d->para, "</sub>");
    if (f & RUN_S) g_string_append(d->para, "</s>");
    if (f & RUN_U) g_string_append(d->para, "</u>");
    if (f & RUN_I) g_string_append(d->para, "</i>");
    if (f & RUN_B) g_string_append(d->para, "</b>");
    d->run_open = 0;
}

static void reset_paragraph(DocParse* d) {
    g_string_truncate(d->para, 0);
    d->math_end = 0;
    g_clear_pointer(&d->p_style, g_free);
    d->p_num_id = -1;
    d->p_ilvl = -1;
    d->p_heading = 0;
    d->in_ppr = FALSE;
}

static void end_paragraph(DocParse* d) {
    close_run_tags(d);
    if (d->link_open) {
        g_string_append(d->para, "</a>");
        d->link_open = FALSE;
    }
    d->in_p = FALSE;
    gboolean empty = (d->para->len == 0);

    if (d->tbl_depth > 0) {
        /* Table cells are one line: paragraphs are joined */
        if (!empty) {
            if (d->cell_has_text) {
                g_string_append_c(d->out, ' ');
            }
            g_string_append_len(d->out, d->para->str, (gssize)d->para->len);
            d->cell_has_text = TRUE;
        }
        reset_paragraph(d);
        return;
    }

    gint heading = d->p_heading > 0 ? d->p_heading : style_heading(d, d->p_style);
    gint num_id = -1, ilvl = 0;
    style_numbering(d, d->p_style, &num_id, &ilvl);
    if (d->p_num_id >= 0) {
        num_id = d->p_num_id;
    }
    if (d->p_ilvl >= 0) {
        ilvl = d->p_ilvl;
    }
    ilvl = CLAMP(ilvl, 0, MAX_LEVELS - 1);

    if (heading > 0) {
        close_lists(d, 0);
        if (!empty) {
            g_string_append_printf(d->out, "<h%d>%s</h%d>\n", heading, d->para->str, heading);
        }
    } else if (num_id > 0 && !empty) {
        emit_list_item(d, num_id, ilvl, d->para->str);
    } else if (!empty) {
        close_lists(d, 0);
        g_string_append_printf(d->out, "<p>%s</p>\n", d->para->str);
    }
    reset_paragraph(d);
}

static const gchar* image_mime_type(const gchar* path) {
    const gchar* dot = strrchr(path, '.');
    if (dot == NULL) {
        return NULL;
    }
    static const struct { const gchar* ext; const gchar* mime; } types[] = {
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" }, { ".bmp", "image/bmp" }, { ".tif", "image/tiff" },
        { ".tiff", "image/tiff" }, { ".webp", "image/webp" }, { ".svg", "image/svg+xml" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(types); i++) {
        if (g_ascii_strcasecmp(dot, types[i].ext) == 0) {
            return types[i].mime;
        }
    }
    return NULL;
}

/* Picture as an <img> with a data: URI; formats the preview cannot show
 * (EMF, WMF...) or too large get an empty one, drawn as a placeholder */
static void emit_image(DocParse* d) {
    const Rel* rel = d->img_rid != NULL ? g_hash_table_lookup(d->rels, d->img_rid) : NULL;
    if (rel == NULL || rel->external || d->tbl_depth > 0) {
        return;
    }
    gchar* part = resolve_part(d->part_dir, rel->target);
    gchar* alt = (d->img_alt != NULL && d->img_alt[0] != '\0') ? g_strdup(d->img_alt)
                                                              : g_path_get_basename(part);
    const gchar* mime = image_mime_type(part);
    GBytes* bytes = NULL;
    gint64 size = zip_reader_get_size(d->zip, part);
    if (mime != NULL && size > 0 && size <= MAX_IMAGE &&
        d->image_bytes + (gsize)size <= MAX_IMAGES_TOTAL) {
        bytes = zip_reader_read(d->zip, part, MAX_IMAGE, NULL);
    }

    close_run_tags(d);
    g_string_append(d->para, "<img src=\"");
    if (bytes != NULL) {
        gsize len = 0;
        const guchar* data = g_bytes_get_data(bytes, &len);
        gchar* b64 = g_base64_encode(data, len);
        g_string_append_printf(d->para, "data:%s;base64,%s", mime, b64);
        g_free(b64);
        d->image_bytes += len;
        g_bytes_unref(bytes);
    } else {
        g_string_append(d->para, "data:image/none;base64,");
    }
    g_string_append(d->para, "\" alt=\"");
    append_attr_value(d->para, alt);
    g_string_append(d->para, "\">");
    g_free(alt);
    g_free(part);
}

static gboolean is_skipped_element(const gchar* name) {
    static const gchar* const skipped[] = {
        "mc:Fallback", "w:del", "w:moveFrom", "w:pPrChange", "w:rPrChange",
        "w:sectPrChange", "w:tblPrChange", "w:trPrChange", "w:tcPrChange",
        "w:numberingChange", "w:sdtPr", "w:fldData", "w:sectPr",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(skipped); i++) {
        if (strcmp(name, skipped[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void start_picture_element(DocParse* d, const gchar* name, const gchar** names, const gchar** values) {
    const gchar* v;
    if (strcmp(name, "a:blip") == 0 && (v = attr(names, values, "r:embed")) != NULL) {
        g_free(d->img_rid);
        d->img_rid = g_strdup(v);
    } else if (strcmp(name, "v:imagedata") == 0 && (v = attr(names, values, "r:id")) != NULL) {
        g_free(d->img_rid);
        d->img_rid = g_strdup(v);
    } else if (strcmp(name, "wp:docPr") == 0) {
        v = attr(names, values, "descr");
        if (v == NULL || v[0] == '\0') {
            v = attr(names, values, "title");
        }
        if (v == NULL || v[0] == '\0') {
            v = attr(names, values, "name");
        }
        g_free(d->img_alt);
        d->img_alt = g_strdup(v);
    }
}

static void start_run_property(DocParse* d, const gchar* name, const gchar** names, const gchar** values) {
    const gchar* val = attr(names, values, "w:val");
    guint bit = 0;
    gboolean on = attr_on(names, values);
    if (strcmp(name, "w:b") == 0) {
        bit = RUN_B;
    } else if (strcmp(name, "w:i") == 0) {
        bit = RUN_I;
    } else if (strcmp(name, "w:u") == 0) {
        bit = RUN_U;
        on = (val == NULL || strcmp(val, "none") != 0);
    } else if (strcmp(name, "w:strike") == 0 || strcmp(name, "w:dstrike") == 0) {
        bit = RUN_S;
    } else if (strcmp(name, "w:vertAlign") == 0) {
        d->run_fmt &= ~(guint)(RUN_SUP | RUN_SUB);
        d->run_fmt |= g_strcmp0(val, "superscript") == 0 ? RUN_SUP
                    : g_strcmp0(val, "subscript") == 0 ? RUN_SUB : 0;
    } else if (strcmp(name, "w:rStyle") == 0) {
        d->run_fmt |= style_run_format(d, val);
    } else if (strcmp(name, "w:vanish") == 0) {
        d->run_hidden = on;
    }
    if (bit != 0) {
        d->run_fmt = on ? (d->run_fmt | bit) : (d->run_fmt & ~bit);
    }
}

static void doc_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
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
    if (is_skipped_element(name)) {
        d->skip_depth = 1;
        return;
    }
    if (strcmp(name, "w:drawing") == 0 || strcmp(name, "w:pict") == 0 || strcmp(name, "w:object") == 0) {
        d->drawing_depth = 1;
        g_clear_pointer(&d->img_rid, g_free);
        g_clear_pointer(&d->img_alt, g_free);
        return;
    }

    if (strcmp(name, "w:p") == 0) {
        reset_paragraph(d);
        d->in_p = TRUE;
    } else if (d->in_p && !d->in_run && strcmp(name, "w:pPr") == 0) {
        d->in_ppr = TRUE;
    } else if (d->in_ppr) {
        if (strcmp(name, "w:pStyle") == 0) {
            g_free(d->p_style);
            d->p_style = g_strdup(attr(names, values, "w:val"));
        } else if (strcmp(name, "w:numId") == 0) {
            d->p_num_id = attr_int(names, values, "w:val", -1);
        } else if (strcmp(name, "w:ilvl") == 0) {
            d->p_ilvl = attr_int(names, values, "w:val", 0);
        } else if (strcmp(name, "w:outlineLvl") == 0) {
            gint lvl = attr_int(names, values, "w:val", 9);
            d->p_heading = (lvl >= 0 && lvl < 9) ? MIN(lvl + 1, 6) : 0;
        }
    } else if (strcmp(name, "w:r") == 0) {
        d->in_run = TRUE;
        d->run_fmt = 0;
        d->run_open = 0;
        d->run_hidden = FALSE;
    } else if (d->in_run && strcmp(name, "w:rPr") == 0) {
        d->in_rpr = TRUE;
    } else if (d->in_rpr) {
        start_run_property(d, name, names, values);
    } else if (strcmp(name, "w:t") == 0) {
        d->in_text = TRUE;
    } else if (strcmp(name, "m:t") == 0) {
        d->in_math_text = TRUE;
    } else if (d->in_p && (strcmp(name, "m:sup") == 0 || strcmp(name, "m:sub") == 0)) {
        g_string_append(d->para, strcmp(name, "m:sup") == 0 ? "<sup>" : "<sub>");
    } else if (d->in_p && strcmp(name, "m:den") == 0) {
        g_string_append_c(d->para, '/');
    } else if (d->in_p && strcmp(name, "w:tab") == 0) {
        open_run_tags(d);
        g_string_append(d->para, "&#9;");
    } else if (d->in_p && (strcmp(name, "w:br") == 0 || strcmp(name, "w:cr") == 0)) {
        const gchar* type = attr(names, values, "w:type");
        if (type == NULL || strcmp(type, "textWrapping") == 0) {
            g_string_append(d->para, d->tbl_depth > 0 ? " " : "<br>");
        }
    } else if (d->in_p && strcmp(name, "w:noBreakHyphen") == 0) {
        g_string_append_c(d->para, '-');
    } else if (d->in_p && strcmp(name, "w:hyperlink") == 0) {
        const gchar* rid = attr(names, values, "r:id");
        const Rel* rel = rid != NULL ? g_hash_table_lookup(d->rels, rid) : NULL;
        if (rel != NULL && rel->external && !d->link_open) {
            close_run_tags(d);
            g_string_append(d->para, "<a href=\"");
            append_attr_value(d->para, rel->target);
            g_string_append(d->para, "\">");
            d->link_open = TRUE;
        }
    } else if (strcmp(name, "w:tbl") == 0) {
        d->tbl_depth++;
        if (d->tbl_depth == 1) {
            close_lists(d, 0);
            g_string_append(d->out, "<table>\n");
        }
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tr") == 0) {
        g_string_append(d->out, "<tr>");
        d->row_open = TRUE;
        d->row_header = FALSE;
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tblHeader") == 0) {
        d->row_header = attr_on(names, values);
    } else if (d->tbl_depth == 1 && strcmp(name, "w:tc") == 0) {
        d->cell_is_th = d->row_header;
        g_string_append(d->out, d->cell_is_th ? "<th>" : "<td>");
        d->cell_open = TRUE;
        d->cell_has_text = FALSE;
        d->grid_span = 1;
    } else if (d->tbl_depth == 1 && strcmp(name, "w:gridSpan") == 0) {
        d->grid_span = CLAMP(attr_int(names, values, "w:val", 1), 1, 64);
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

    if (strcmp(name, "w:p") == 0) {
        if (d->in_p) {
            end_paragraph(d);
        }
    } else if (strcmp(name, "w:pPr") == 0) {
        d->in_ppr = FALSE;
    } else if (strcmp(name, "w:r") == 0) {
        close_run_tags(d);
        d->in_run = FALSE;
        d->in_rpr = FALSE;
    } else if (strcmp(name, "w:rPr") == 0) {
        d->in_rpr = FALSE;
    } else if (strcmp(name, "w:t") == 0) {
        d->in_text = FALSE;
    } else if (strcmp(name, "m:t") == 0) {
        d->in_math_text = FALSE;
    } else if (d->in_p && strcmp(name, "m:sup") == 0) {
        g_string_append(d->para, "</sup>");
    } else if (d->in_p && strcmp(name, "m:sub") == 0) {
        g_string_append(d->para, "</sub>");
    } else if (strcmp(name, "w:hyperlink") == 0) {
        if (d->link_open) {
            close_run_tags(d);
            g_string_append(d->para, "</a>");
            d->link_open = FALSE;
        }
    } else if (strcmp(name, "w:tc") == 0 && d->tbl_depth == 1 && d->cell_open) {
        g_string_append(d->out, d->cell_is_th ? "</th>" : "</td>");
        for (gint i = 1; i < d->grid_span; i++) {
            g_string_append(d->out, d->cell_is_th ? "<th></th>" : "<td></td>");
        }
        d->cell_open = FALSE;
    } else if (strcmp(name, "w:tr") == 0 && d->tbl_depth == 1 && d->row_open) {
        g_string_append(d->out, "</tr>\n");
        d->row_open = FALSE;
    } else if (strcmp(name, "w:tbl") == 0 && d->tbl_depth > 0) {
        if (d->tbl_depth == 1) {
            g_string_append(d->out, "</table>\n");
        }
        d->tbl_depth--;
    }
}

static void doc_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    DocParse* d = data;
    if (d->skip_depth > 0 || d->drawing_depth > 0 || !d->in_p || len == 0) {
        return;
    }
    if (d->in_text && !d->run_hidden) {
        open_run_tags(d);
        append_escaped(d->para, text, len);
    } else if (d->in_math_text) {
        /* Math is shown as italic text: consecutive pieces share one <i> */
        if (d->math_end > 0 && d->math_end == d->para->len) {
            g_string_truncate(d->para, d->para->len - 4);
        } else {
            g_string_append(d->para, "<i>");
        }
        append_escaped(d->para, text, len);
        g_string_append(d->para, "</i>");
        d->math_end = d->para->len;
    } else {
        return;
    }
    d->text_len += len;
    if (d->text_len >= d->max_text) {
        d->truncated = TRUE;
        g_set_error_literal(error, docx_stop_quark(), 0, "text limit reached");
    }
}

/* ---- Document properties (docProps/core.xml) ---------------------------- */

typedef struct {
    gboolean in_creator;
    GString* creator;
} CoreParse;

static void core_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                       const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    (void)names;
    (void)values;
    (void)error;
    ((CoreParse*)data)->in_creator = (strcmp(name, "dc:creator") == 0);
}

static void core_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)name;
    (void)error;
    ((CoreParse*)data)->in_creator = FALSE;
}

static void core_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    CoreParse* cp = data;
    if (cp->in_creator) {
        g_string_append_len(cp->creator, text, (gssize)len);
    }
}

static gchar* read_author(ZipReader* zip) {
    CoreParse cp = { FALSE, g_string_new(NULL) };
    static const GMarkupParser parser = { core_start, core_end, core_text, NULL, NULL };
    parse_part(zip, "docProps/core.xml", MAX_PART, TRUE, &parser, &cp, NULL);
    gchar* author = g_strstrip(g_string_free(cp.creator, FALSE));
    if (author[0] == '\0') {
        g_free(author);
        return NULL;
    }
    return author;
}

/* ---- Entry points ------------------------------------------------------- */

gchar* docx_to_html(ZipReader* zip, gsize max_text, GError** error) {
    g_return_val_if_fail(zip != NULL, NULL);
    gchar* main = main_part_name(zip);
    if (main == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Not a Word document");
        return NULL;
    }

    DocParse d;
    memset(&d, 0, sizeof d);
    d.zip = zip;
    d.part_dir = g_path_get_dirname(main);
    if (strcmp(d.part_dir, ".") == 0) {
        d.part_dir[0] = '\0';
    }
    d.rels = read_rels(zip, main);
    d.max_text = MAX(max_text, 1);
    d.out = g_string_new(NULL);
    d.para = g_string_new(NULL);
    d.p_num_id = -1;
    d.p_ilvl = -1;
    d.counters = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    /* Styles and numbering parts: found through the relationships, usual names otherwise */
    gchar* styles_part = NULL;
    gchar* numbering_part = NULL;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, d.rels);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        const Rel* r = value;
        if (r->external || r->type == NULL) {
            continue;
        }
        if (styles_part == NULL && g_str_has_suffix(r->type, "/styles")) {
            styles_part = resolve_part(d.part_dir, r->target);
        } else if (numbering_part == NULL && g_str_has_suffix(r->type, "/numbering")) {
            numbering_part = resolve_part(d.part_dir, r->target);
        }
    }
    if (styles_part == NULL) {
        styles_part = resolve_part(d.part_dir, "styles.xml");
    }
    if (numbering_part == NULL) {
        numbering_part = resolve_part(d.part_dir, "numbering.xml");
    }

    StylesParse sp = { g_hash_table_new_full(g_str_hash, g_str_equal, g_free, style_free), NULL, FALSE, FALSE };
    static const GMarkupParser styles_parser = { styles_start, styles_end, NULL, NULL, NULL };
    parse_part(zip, styles_part, MAX_PART, TRUE, &styles_parser, &sp, NULL);
    d.styles = sp.styles;

    d.num.abstracts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    d.num.nums = g_hash_table_new(g_direct_hash, g_direct_equal);
    d.num.cur_lvl = -1;
    d.num.cur_num = -1;
    static const GMarkupParser num_parser = { num_start, num_end, NULL, NULL, NULL };
    parse_part(zip, numbering_part, MAX_PART, TRUE, &num_parser, &d.num, NULL);

    static const GMarkupParser doc_parser = { doc_start, doc_end, doc_text, NULL, NULL };
    GError* err = NULL;
    gboolean ok = parse_part(zip, main, DOCX_MAX_XML, FALSE, &doc_parser, &d, &err);
    gchar* html = NULL;
    if (ok || g_error_matches(err, docx_stop_quark(), 0)) {
        g_clear_error(&err);
        /* Close what the end of the document (or the text limit) left open */
        if (d.in_p) {
            end_paragraph(&d);
        }
        if (d.tbl_depth > 0) {
            if (d.cell_open) {
                g_string_append(d.out, d.cell_is_th ? "</th>" : "</td>");
            }
            if (d.row_open) {
                g_string_append(d.out, "</tr>\n");
            }
            g_string_append(d.out, "</table>\n");
        }
        close_lists(&d, 0);
        if (d.truncated) {
            g_string_append(d.out, "<hr>\n<p><i>Document truncated: only the beginning is shown.</i></p>\n");
        }

        GString* doc = g_string_new("<html><head>");
        gchar* author = read_author(zip);
        if (author != NULL) {
            g_string_append(doc, "<meta name=\"author\" content=\"");
            append_attr_value(doc, author);
            g_string_append(doc, "\">");
            g_free(author);
        }
        g_string_append(doc, "</head><body>\n");
        g_string_append_len(doc, d.out->str, (gssize)d.out->len);
        g_string_append(doc, "</body></html>\n");
        html = g_string_free(doc, FALSE);
    } else {
        g_propagate_error(error, err);
    }

    g_hash_table_destroy(d.num.abstracts);
    g_hash_table_destroy(d.num.nums);
    g_hash_table_destroy(d.styles);
    g_hash_table_destroy(d.counters);
    g_hash_table_destroy(d.rels);
    g_string_free(d.out, TRUE);
    g_string_free(d.para, TRUE);
    g_free(d.p_style);
    g_free(d.img_rid);
    g_free(d.img_alt);
    g_free(d.part_dir);
    g_free(styles_part);
    g_free(numbering_part);
    g_free(main);
    return html;
}

gchar* docx_file_to_html(const gchar* path, gsize max_text, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    gchar* html = docx_to_html(zip, max_text, error);
    zip_reader_free(zip);
    return html;
}
